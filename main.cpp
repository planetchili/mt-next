#include <iostream>
#include <random>
#include <array>
#include <ranges>
#include <cmath>
#include <thread>
#include <mutex>
#include <span>
#include <numbers>
#include <fstream>
#include <format>
#include <atomic>
#include "ChiliTimer.h"


constexpr bool ChunkMeasurementEnabled = false;
constexpr size_t WorkerCount = 4;
constexpr size_t ChunkSize = 1'000;
constexpr size_t ChunkCount = 500;
constexpr size_t LightIterations = 100;
constexpr size_t HeavyIterations = 1000;
constexpr double ProbabilityHeavy = .15;

constexpr size_t SubsetSize = ChunkSize / WorkerCount;

static_assert(ChunkSize >= WorkerCount);
static_assert(ChunkSize % WorkerCount == 0);

struct Task
{
    double val;
    bool heavy;
    unsigned int Process() const
    {
        const auto iterations = heavy ? HeavyIterations : LightIterations;
        auto intermediate = val;
        for (size_t i = 0; i < iterations; i++)
        {
            const auto digits = unsigned int(std::abs(std::sin(std::cos(intermediate) * std::numbers::pi) * 10'000'000.)) % 100'000;
            intermediate = double(digits) / 10'000.;
        }
        return unsigned int(std::exp(intermediate));
    }
};

auto GenerateDataset()
{
    std::minstd_rand rne;
    std::uniform_real_distribution vDist{ 0., 2. * std::numbers::pi };
    std::bernoulli_distribution hDist{ ProbabilityHeavy };

    std::vector<std::array<Task, ChunkSize>> chunks(ChunkCount);

    for (auto& chunk : chunks)
    {
        std::ranges::generate(chunk, [&] { return Task{ .val = vDist(rne), .heavy = hDist(rne) }; });
    }

    return chunks;
}

auto GenerateDatasetEven()
{
    std::minstd_rand rne;
    std::uniform_real_distribution vDist{ 0., 2. * std::numbers::pi };

    std::vector<std::array<Task, ChunkSize>> chunks(ChunkCount);

    for (auto& chunk : chunks)
    {
        std::ranges::generate(chunk, [&, acc = 0.]() mutable {
            bool heavy = false;
            if ((acc += ProbabilityHeavy) >= 1.)
            {
                acc -= 1.;
                heavy = true;
            }
            return Task{.val = vDist(rne), .heavy = heavy};
        });
    }

    return chunks;
}

auto GenerateDatasetStacked()
{
    auto chunks = GenerateDatasetEven();

    for (auto& chunk : chunks)
    {
        std::ranges::partition(chunk, std::identity{}, &Task::heavy);
    }

    return chunks;
}

struct ChunkTimingInfo
{
    std::array<float, WorkerCount> timeSpentWorkingPerThread;
    std::array<size_t, WorkerCount> numberOfHeavyItemsPerThread;
    float totalChunkTime;
};

//class MasterControl
//{
//public:
//    MasterControl() : lk{ mtx } {}
//    void SignalDone()
//    {
//        bool needsNotification = false;
//        {
//            std::lock_guard lk{ mtx };
//            ++doneCount;
//            needsNotification = doneCount == WorkerCount;
//        }
//        if (needsNotification)
//        {
//            cv.notify_one();
//        }
//    }
//    void WaitForAllDone()
//    {
//        cv.wait(lk, [this] { return doneCount == WorkerCount; });
//        doneCount = 0;
//    }
//private:
//    std::condition_variable cv;
//    std::mutex mtx;
//    std::unique_lock<std::mutex> lk;
//    // shared memory
//    int doneCount = 0;
//};
//
//class Worker
//{
//public:
//    Worker(MasterControl* pMaster)
//        :
//        pMaster{ pMaster },
//        thread{ &Worker::Run_, this }
//    {}
//    void SetJob(std::span<const Task> data)
//    {
//        {
//            std::lock_guard lk{ mtx };
//            input = data;
//        }
//        cv.notify_one();
//    }
//    void Kill()
//    {
//        {
//            std::lock_guard lk{ mtx };
//            dying = true;
//        }
//        cv.notify_one();
//    }
//    unsigned int GetResult() const
//    {
//        return accumulation;
//    }
//    size_t GetNumHeavyItemsProcessed() const
//    {
//        return numHeavyItemsProcessed;
//    }
//    float GetJobWorkTime() const
//    {
//        return workTime;
//    }
//    ~Worker()
//    {
//        Kill();
//    }
//private:
//    void ProcessData_()
//    {
//        if constexpr (ChunkMeasurementEnabled)
//        {
//            numHeavyItemsProcessed = 0;
//        }
//        for (auto& task : input)
//        {
//            accumulation += task.Process();
//            if constexpr (ChunkMeasurementEnabled)
//            {
//                numHeavyItemsProcessed += task.heavy ? 1 : 0;
//            }
//        }
//    }
//    void Run_()
//    {
//        std::unique_lock lk{ mtx };
//        while (true)
//        {
//            ChiliTimer timer;
//            cv.wait(lk, [this] {return !input.empty() || dying; });
//            if (dying)
//            {
//                break;
//            }
//
//            if constexpr (ChunkMeasurementEnabled)
//            {
//                timer.Mark();
//            }
//            ProcessData_();
//            if constexpr (ChunkMeasurementEnabled)
//            {
//                workTime = timer.Peek();
//            }
//
//            input = {};
//            pMaster->SignalDone();
//        }
//    }
//    MasterControl* pMaster;
//    std::jthread thread;
//    std::condition_variable cv;
//    std::mutex mtx;
//    // shared memory
//    std::span<const Task> input;
//    unsigned int accumulation = 0;
//    float workTime = -1.f;
//    size_t numHeavyItemsProcessed = 0;
//    bool dying = false;
//};
//
//int DoExperiment(bool stacked)
//{
//    const auto chunks = stacked ? GenerateDatasetStacked() : GenerateDatasetEven();
//
//    ChiliTimer chunkTimer;
//    std::vector<ChunkTimingInfo> timings;
//    timings.reserve(ChunkCount);
//
//    ChiliTimer totalTimer;
//    totalTimer.Mark();
//
//    MasterControl mctrl;
//
//    std::vector<std::unique_ptr<Worker>> workerPtrs(WorkerCount);
//    std::ranges::generate(workerPtrs, [pMctrl = &mctrl] { return std::make_unique<Worker>(pMctrl); });
//
//    for (auto& chunk : chunks)
//    {
//        if constexpr (ChunkMeasurementEnabled)
//        {
//            chunkTimer.Mark();
//        }
//        for (size_t iSubset = 0; iSubset < WorkerCount; iSubset++)
//        {
//            workerPtrs[iSubset]->SetJob(std::span{ &chunk[iSubset * SubsetSize], SubsetSize });
//        }
//        mctrl.WaitForAllDone();
//
//        if constexpr (ChunkMeasurementEnabled)
//        {
//            timings.push_back(ChunkTimingInfo{ .totalChunkTime = chunkTimer.Peek() });
//            for (size_t i = 0; i < WorkerCount; i++)
//            {
//                auto& cur = timings.back();
//                cur.numberOfHeavyItemsPerThread[i] = workerPtrs[i]->GetNumHeavyItemsProcessed();
//                cur.timeSpentWorkingPerThread[i] = workerPtrs[i]->GetJobWorkTime();
//            }
//        }
//    }
//
//    const auto t = totalTimer.Peek();
//
//    std::cout << "Processing took " << t << " seconds\n";
//
//    unsigned int finalResult = 0;
//    for (const auto& w : workerPtrs)
//    {
//        finalResult += w->GetResult();
//    }
//    std::cout << "Result is " << finalResult << std::endl;
//
//
//    if constexpr (ChunkMeasurementEnabled)
//    {
//        std::ofstream csv{ "timings.csv", std::ios_base::trunc };
//        // csv header
//        for (size_t i = 0; i < WorkerCount; i++)
//        {
//            csv << std::format("work_{0:},idle_{0:},heavy_{0:},", i);
//        }
//        csv << "chunktime,total_idle,total_heavy\n";
//
//        for (const auto& chunk : timings)
//        {
//            float totalIdle = 0.f;
//            size_t totalHeavy = 0;
//            for (size_t i = 0; i < WorkerCount; i++)
//            {
//                const auto idle = chunk.totalChunkTime - chunk.timeSpentWorkingPerThread[i];
//                const auto heavy = chunk.numberOfHeavyItemsPerThread[i];
//                csv << std::format("{},{},{},", chunk.timeSpentWorkingPerThread[i], idle, heavy);
//                totalIdle += idle;
//                totalHeavy += heavy;
//            }
//            csv << std::format("{},{},{}\n", chunk.totalChunkTime, totalIdle, totalHeavy);
//        }
//    }
//
//    return 0;
//}


class MasterControlQueued
{
public:
    MasterControlQueued() : lk{ mtx } {}
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
    __declspec(noinline) const Task* GetTask()
    {
        //std::lock_guard lock{ mtx };
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

class WorkerQueued
{
public:
    WorkerQueued(MasterControlQueued* pMaster)
        :
        pMaster{ pMaster },
        thread{ &WorkerQueued::Run_, this }
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
    ~WorkerQueued()
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
    MasterControlQueued* pMaster;
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

int DoExperimentQueued(bool stacked)
{
    const auto chunks = stacked ? GenerateDatasetStacked() : GenerateDatasetEven();

    ChiliTimer chunkTimer;
    std::vector<ChunkTimingInfo> timings;
    timings.reserve(ChunkCount);

    ChiliTimer totalTimer;
    totalTimer.Mark();

    MasterControlQueued mctrl;

    std::vector<std::unique_ptr<WorkerQueued>> workerPtrs(WorkerCount);
    std::ranges::generate(workerPtrs, [pMctrl = &mctrl] { return std::make_unique<WorkerQueued>(pMctrl); });

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
        std::ofstream csv{ "timings.csv", std::ios_base::trunc };
        // csv header
        for (size_t i = 0; i < WorkerCount; i++)
        {
            csv << std::format("work_{0:},idle_{0:},heavy_{0:},", i);
        }
        csv << "chunktime,total_idle,total_heavy\n";

        for (const auto& chunk : timings)
        {
            float totalIdle = 0.f;
            size_t totalHeavy = 0;
            for (size_t i = 0; i < WorkerCount; i++)
            {
                const auto idle = chunk.totalChunkTime - chunk.timeSpentWorkingPerThread[i];
                const auto heavy = chunk.numberOfHeavyItemsPerThread[i];
                csv << std::format("{},{},{},", chunk.timeSpentWorkingPerThread[i], idle, heavy);
                totalIdle += idle;
                totalHeavy += heavy;
            }
            csv << std::format("{},{},{}\n", chunk.totalChunkTime, totalIdle, totalHeavy);
        }
    }

    return 0;
}

int main(int argc, char** argv)
{
    using namespace std::string_literals;
    bool stacked = false;
    if (argc > 1 && argv[1] == "--stacked"s) {
        stacked = true;
    }
    return DoExperimentQueued(stacked);
}