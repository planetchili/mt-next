#include "Task.h"
#include <condition_variable>
#include <functional>
#include <deque>
#include <optional>
#include <cassert>
#include <ranges>
#include <future>
#include <vector>
#include "ChiliTimer.h"

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

int main(int argc, const char** argv)
{
    using namespace std::chrono_literals;

    ParseCli(argc, argv);

    tk::ThreadPool pool{ WorkerCount };

    ChiliTimer timer;
    auto tasks = GenerateDatasetRandom();
    std::cout << "nTasks: " << tasks.size() << std::endl;
    const auto computeTask = [](const Task& t) {
        return t.Process();
    };
    const auto asyncTask = [] {
        std::this_thread::sleep_for(1ms * AsyncSleep);
    };

    timer.Mark();
    auto futures = tasks | vi::transform([&](const Task& workItem) {
        return pool.Run(asyncTask);
    }) | rn::to<std::vector>();

    for (auto& f : futures) {
        try {
            f.get();
        }
        catch (...) {
            std::cout << "yikes" << std::endl;
        }
    }
    auto time = timer.Peek();

    std::cout << "Time taken: " << time << std::endl;

    return 0;
}