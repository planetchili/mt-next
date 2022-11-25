#include "popl.h"
#include "Preassigned.h"
#include "Queued.h"
#include "AtomicQueued.h"
#include "Task.h"
#include <condition_variable>
#include <functional>

namespace tk
{
    using Task = std::function<void()>;

    class ThreadPool
    {
    public:
        void Run(Task task)
        {
            if (auto i = std::ranges::find_if(workers_, [](const auto& w) {return !w->IsBusy();});
                i != workers_.end()) {
                (*i)->Run(std::move(task));
            }
            else {
                workers_.push_back(std::make_unique<Worker>());
                workers_.back()->Run(std::move(task));
            }
        }
        bool IsRunningTasks()
        {
            return std::ranges::any_of(workers_, [](const auto& w) {return w->IsBusy(); });
        }

    private:
        // types
        class Worker
        {
        public:
            Worker() : thread_(&Worker::RunKernel_, this) {}
            bool IsBusy() const
            {
                return busy_;
            }
            void Run(Task task)
            {
                task_ = std::move(task);
                busy_ = true;
                cv_.notify_one();
            }
        private:
            // functions
            void RunKernel_()
            {
                std::unique_lock lk{ mtx_ };
                auto st = thread_.get_stop_token();
                while (cv_.wait(lk, st, [this]() -> bool { return busy_; })) {
                    task_();
                    task_ = {};
                    busy_ = false;
                }
            }
            // data
            std::atomic<bool> busy_ = false;
            std::condition_variable_any cv_;
            std::mutex mtx_;
            Task task_;
            std::jthread thread_;
        };
        // data
        std::vector<std::unique_ptr<Worker>> workers_;
    };
}

int main(int argc, char** argv)
{
    tk::ThreadPool pool;
    pool.Run([] {std::cout << "HI" << std::endl; });
    pool.Run([] {std::cout << "LO" << std::endl; });
    return 0;
}