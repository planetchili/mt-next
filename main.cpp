#include "popl.h"
#include "Preassigned.h"
#include "Queued.h"
#include "AtomicQueued.h"
#include "Task.h"
#include <condition_variable>
#include <functional>
#include <deque>
#include <optional>
#include <semaphore>
#include <cassert>

namespace tk
{
    template<typename T>
    class SharedState
    {
    public:
        template<typename R>
        void Set(R&& result)
        {
            if (!result_) {
                result_ = std::forward<R>(result);
                readySignal_.release();
            }
        }
        T Get()
        {
            readySignal_.acquire();
            return std::move(*result_);
        }
    private:
        std::binary_semaphore readySignal_{ 0 };
        std::optional<T> result_;
    };

    template<>
    class SharedState<void>
    {
    public:
        void Set()
        {
            if (!complete_) {
                complete_ = true;
                readySignal_.release();
            }
        }
        void Get()
        {
            readySignal_.acquire();
        }
    private:
        std::binary_semaphore readySignal_{ 0 };
        bool complete_ = false;
    };

    template<typename T>
    class Promise;

    template<typename T>
    class Future
    {
        friend class Promise<T>;
    public:
        T Get()
        {
            assert(!resultAcquired);
            resultAcquired = true;
            return pState_->Get();
        }
    private:
        // functions
        Future(std::shared_ptr<SharedState<T>> pState) : pState_{ pState } {}
        // data
        bool resultAcquired = false;
        std::shared_ptr<SharedState<T>> pState_;
    };


    template<typename T>
    class Promise
    {
    public:
        Promise() : pState_{ std::make_shared<SharedState<T>>() } {}
        template<typename...R>
        void Set(R&&...result)
        {
            pState_->Set(std::forward<R>(result)...);
        }        
        Future<T> GetFuture()
        {
            assert(futureAvailable);
            futureAvailable = false;
            return { pState_ };
        }
    private:
        bool futureAvailable = true;
        std::shared_ptr<SharedState<T>> pState_;
    };

    class Task
    {
    public:
        Task() = default;
        Task(const Task&) = delete;
        Task(Task&& donor) noexcept : executor_{ std::move(donor.executor_) } {}
        Task& operator=(const Task&) = delete;
        Task& operator=(Task&& rhs) noexcept
        {
            executor_ = std::move(rhs.executor_);
            return *this;
        }
        void operator()()
        {
            executor_();
        }
        operator bool() const
        {
            return (bool)executor_;
        }
        template<typename F, typename...A>
        static auto Make(F&& function, A&&...args)
        {
            Promise<std::invoke_result_t<F, A...>> promise;
            auto future = promise.GetFuture();
            return std::make_pair(
                Task{ std::forward<F>(function), std::move(promise), std::forward<A>(args)... },
                std::move(future)
            );
        }

    private:
        // functions
        template<typename F, typename P, typename...A>
        Task(F&& function, P&& promise, A&&...args)
        {
            executor_ = [
                function = std::forward<F>(function),
                promise = std::forward<P>(promise),
                ...args = std::forward<A>(args)
            ]() mutable {
                if constexpr (std::is_void_v<std::invoke_result_t<F, A...>>) {
                    function(std::forward<A>(args)...);
                    promise.Set();
                }
                else {
                    promise.Set(function(std::forward<A>(args)...));
                }
            };
        }
        // data
        std::function<void()> executor_;
    };

    class ThreadPool
    {
    public:
        ThreadPool(size_t numWorkers)
        {
            workers_.reserve(numWorkers);
            for (size_t i = 0; i < numWorkers; i++) {
                workers_.emplace_back(this);
            }
        }
        template<typename F, typename...A>
        auto Run(F&& function, A&&...args)
        {
            auto [task, future] = Task::Make(std::forward<F>(function), std::forward<A>(args)...);
            {
                std::lock_guard lk{ taskQueueMtx_ };
                tasks_.push_back(std::move(task));
            }
            taskQueueCv_.notify_one();
            return future;
        }
        void WaitForAllDone()
        {
            std::unique_lock lk{ taskQueueMtx_ };
            allDoneCv_.wait(lk, [this] {return tasks_.empty(); });
        }
        ~ThreadPool()
        {
            for (auto& w : workers_) {
                w.RequestStop();
            }
        }

    private:
        // functions
        Task GetTask_(std::stop_token& st)
        {
            Task task;
            std::unique_lock lk{ taskQueueMtx_ };
            taskQueueCv_.wait(lk, st, [this] {return !tasks_.empty(); });
            if (!st.stop_requested()) {
                task = std::move(tasks_.front());
                tasks_.pop_front();
                if (tasks_.empty()) {
                    allDoneCv_.notify_all();
                }
            }
            return task;
        }
        // types
        class Worker_
        {
        public:
            Worker_(ThreadPool* pool) : pool_{ pool }, thread_(std::bind_front(&Worker_::RunKernel_, this)) {}
            void RequestStop()
            {
                thread_.request_stop();
            }
        private:
            // functions
            void RunKernel_(std::stop_token st)
            {
                while (auto task = pool_->GetTask_(st)) {
                    task();
                }
            }
            // data
            ThreadPool* pool_;
            std::jthread thread_;
        };
        // data
        std::mutex taskQueueMtx_;
        std::condition_variable_any taskQueueCv_;
        std::condition_variable allDoneCv_;
        std::deque<Task> tasks_;
        std::vector<Worker_> workers_;
    };
}

int main(int argc, char** argv)
{
    using namespace std::chrono_literals;

    tk::ThreadPool pool{ 4 };
    const auto spitt = [] {
        std::this_thread::sleep_for(100ms);
        std::ostringstream ss;
        ss << std::this_thread::get_id();
        std::cout << std::format("<< {} >>\n", ss.str()) << std::flush;
    };
    for (int i = 0; i < 40; i++) {
        pool.Run(spitt);
    }
    pool.WaitForAllDone();

    tk::Promise<int> prom;
    auto fut = prom.GetFuture();
    std::thread{ [](tk::Promise<int> p) {
        std::this_thread::sleep_for(1'500ms);
        p.Set(69);
    }, std::move(prom) }.detach();
    std::cout << fut.Get() << std::endl;

    auto [task, future] = tk::Task::Make([](int x) {
        std::this_thread::sleep_for(1'500ms);
        return x + 42000;
    }, 69);
    std::thread{ std::move(task) }.detach();
    std::cout << future.Get() << std::endl;

    return 0;
}