#pragma once
#include <iostream>
#include <thread>
#include <mutex>
#include <span>
#include <format>
#include "Constants.h"
#include "Task.h"
#include "Timing.h"
#include "ChiliTimer.h"

namespace atq
{
    class MasterControl
    {
    public:
        MasterControl() : lk{ mtx } {}
        void SignalDone()
        {
            bool needsNotification = false;
            {
                std::lock_guard lk{ mtx };
                ++doneCount;
                needsNotification = doneCount == WorkerCount;
            }
            if (needsNotification)
            {
                cv.notify_one();
            }
        }
        void WaitForAllDone()
        {
            cv.wait(lk, [this] { return doneCount == WorkerCount; });
            doneCount = 0;
        }
        void SetChunk(std::span<const Task> chunk)
        {
            idx = 0;
            currentChunk = chunk;
        }
        const Task* GetTask()
        {
            std::lock_guard lck{ mtx };
            const auto i = idx++;
            if (i >= ChunkSize)
            {
                return nullptr;
            }
            return &currentChunk[i];
        }
    private:
        std::condition_variable cv;
        std::mutex mtx;
        std::unique_lock<std::mutex> lk;
        std::span<const Task> currentChunk;
        // shared memory
        int doneCount = 0;
        size_t idx = 0;
    };

    class Worker
    {
    public:
        Worker(MasterControl* pMaster)
            :
            pMaster{ pMaster },
            thread{ &Worker::Run_, this }
        {}
        void StartWork()
        {
            {
                std::lock_guard lk{ mtx };
                working = true;
            }
            cv.notify_one();
        }
        void Kill()
        {
            {
                std::lock_guard lk{ mtx };
                dying = true;
            }
            cv.notify_one();
        }
        unsigned int GetResult() const
        {
            return accumulation;
        }
        size_t GetNumHeavyItemsProcessed() const
        {
            return numHeavyItemsProcessed;
        }
        float GetJobWorkTime() const
        {
            return workTime;
        }
        ~Worker()
        {
            Kill();
        }
    private:
        void ProcessData_()
        {
            if constexpr (ChunkMeasurementEnabled)
            {
                numHeavyItemsProcessed = 0;
            }
            while (auto pTask = pMaster->GetTask())
            {
                accumulation += pTask->Process();
                if constexpr (ChunkMeasurementEnabled)
                {
                    numHeavyItemsProcessed += pTask->heavy ? 1 : 0;
                }
            }
        }
        void Run_()
        {
            std::unique_lock lk{ mtx };
            while (true)
            {
                ChiliTimer timer;
                cv.wait(lk, [this] {return working || dying; });
                if (dying)
                {
                    break;
                }

                if constexpr (ChunkMeasurementEnabled)
                {
                    timer.Mark();
                }
                ProcessData_();
                if constexpr (ChunkMeasurementEnabled)
                {
                    workTime = timer.Peek();
                }

                working = false;
                pMaster->SignalDone();
            }
        }
        MasterControl* pMaster;
        std::jthread thread;
        std::condition_variable cv;
        std::mutex mtx;
        // shared memory
        unsigned int accumulation = 0;
        float workTime = -1.f;
        size_t numHeavyItemsProcessed = 0;
        bool dying = false;
        bool working = false;
    };

    int DoExperiment(Dataset chunks)
    {
        ChiliTimer chunkTimer;
        std::vector<ChunkTimingInfo> timings;
        timings.reserve(ChunkCount);

        ChiliTimer totalTimer;
        totalTimer.Mark();

        MasterControl mctrl;

        std::vector<std::unique_ptr<Worker>> workerPtrs(WorkerCount);
        std::ranges::generate(workerPtrs, [pMctrl = &mctrl] { return std::make_unique<Worker>(pMctrl); });

        for (auto& chunk : chunks)
        {
            if constexpr (ChunkMeasurementEnabled)
            {
                chunkTimer.Mark();
            }
            mctrl.SetChunk(chunk);
            for (auto& pWorker : workerPtrs)
            {
                pWorker->StartWork();
            }
            mctrl.WaitForAllDone();

            if constexpr (ChunkMeasurementEnabled)
            {
                timings.push_back(ChunkTimingInfo{ .totalChunkTime = chunkTimer.Peek() });
                for (size_t i = 0; i < WorkerCount; i++)
                {
                    auto& cur = timings.back();
                    cur.numberOfHeavyItemsPerThread[i] = workerPtrs[i]->GetNumHeavyItemsProcessed();
                    cur.timeSpentWorkingPerThread[i] = workerPtrs[i]->GetJobWorkTime();
                }
            }
        }

        const auto t = totalTimer.Peek();

        std::cout << "Processing took " << t << " seconds\n";

        unsigned int finalResult = 0;
        for (const auto& w : workerPtrs)
        {
            finalResult += w->GetResult();
        }
        std::cout << "Result is " << finalResult << std::endl;


        if constexpr (ChunkMeasurementEnabled)
        {
            WriteCSV(timings);
        }

        return 0;
    }
}