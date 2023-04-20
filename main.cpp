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
#include <ranges>
#include <variant>
#include <future>

namespace rn = std::ranges;
namespace vi = rn::views;

namespace tk
{
    class ThreadPool
    {
        using Task = std::move_only_function<void()>;
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
            using ReturnType = std::invoke_result_t<F, A...>;
            auto pak = std::packaged_task<ReturnType()>{ std::bind(
                std::forward<F>(function), std::forward<A>(args)...
            ) };
            auto future = pak.get_future();
            Task task{ [pak = std::move(pak)]() mutable { pak(); } };
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
    {
        const auto spitt = [](int milliseconds) {
            if (milliseconds && milliseconds % 100 == 0) {
                throw std::runtime_error{ "wwee" };
            }
            std::this_thread::sleep_for(1ms * milliseconds);
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            return ss.str();
        };
        auto futures = vi::iota(0, 40) |
            vi::transform([&](int i) { return pool.Run(spitt, i * 25); }) |
            rn::to<std::vector>();
        for (auto& f : futures) {
            try {
                std::cout << "<<< " << f.get() << " >>>" << std::endl;
            }
            catch (...) {
                std::cout << "yikes" << std::endl;
            }
        }
    }


    auto future = pool.Run([] { std::this_thread::sleep_for(2000ms); return 69; });
    while (future.wait_for(0ms) != std::future_status::ready) {
        std::this_thread::sleep_for(250ms);
        std::cout << "Waiting..." << std::endl;
    }
    std::cout << "Task ready! Value is: " << future.get() << std::endl;

    return 0;
}