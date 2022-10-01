#include <iostream>
#include <random>
#include <array>
#include <ranges>
#include <limits>
#include <cmath>
#include <thread>
#include <mutex>
#include <span>
#include <algorithm>
#include <numeric>
#include <numbers>
#include <fstream>
#include <format>
#include <functional>
#include "ChiliTimer.h"

// experimental settings
constexpr size_t WorkerCount = 4;
constexpr size_t ChunkSize = 8'000;
constexpr size_t ChunkCount = 100;
constexpr size_t SubsetSize = ChunkSize / WorkerCount;
constexpr size_t LightIterations = 100;
constexpr size_t HeavyIterations = 1'000;
constexpr double ProbabilityHeavy = .15;

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
            const auto digits = unsigned int(std::abs(std::sin(std::cos(intermediate)) * 10'000'000.)) % 100'000;
            intermediate = double(digits) / 10'000.;
        }
        return unsigned int(std::exp(intermediate));
    }
};

struct ChunkTimingInfo
{
    std::array<float, WorkerCount> timeSpentWorkingPerThread;
    std::array<size_t, WorkerCount> numberOfHeavyItemsPerThread;
    float totalChunkTime;
};

std::vector<std::array<Task, ChunkSize>> GenerateDatasetRandom()
{
    std::minstd_rand rne;
    std::bernoulli_distribution hDist{ ProbabilityHeavy };
    std::uniform_real_distribution rDist{ 0., std::numbers::pi };

    std::vector<std::array<Task, ChunkSize>> chunks(ChunkCount);

    for (auto& chunk : chunks)
    {
        std::ranges::generate(chunk, [&] { return Task{ .val = rDist(rne), .heavy = hDist(rne)}; });
    }

    return chunks;
}

std::vector<std::array<Task, ChunkSize>> GenerateDatasetEvenly()
{
    std::minstd_rand rne;
    std::uniform_real_distribution rDist{ 0., std::numbers::pi };

    const auto everyNth = int(1. / ProbabilityHeavy);

    std::vector<std::array<Task, ChunkSize>> chunks(ChunkCount);

    for (auto& chunk : chunks)
    {
        std::ranges::generate(chunk, [&, i = 0]() mutable {
            const auto isHeavy = i++ % everyNth == 0;
            return Task{ .val = rDist(rne), .heavy = isHeavy };
        });
    }

    return chunks;
}

std::vector<std::array<Task, ChunkSize>> GenerateDatasetStacked()
{
    auto data = GenerateDatasetEvenly();

    for (auto& chunk : data)
    {
        std::ranges::partition(chunk, std::identity{}, &Task::heavy);
    }

    return data;
}

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
            if (doneCount == WorkerCount)
            {
                needsNotification = true;
            }
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
private:
    std::condition_variable cv;
    std::mutex mtx;
    std::unique_lock<std::mutex> lk;
    // shared memory
    int doneCount = 0;
};

class Worker
{
public:
    Worker(MasterControl* pMaster)
        :
        pMaster{ pMaster },
        thread{ &Worker::Run_, this }
    {}
    void SetJob(std::span<const Task> data)
    {
        {
            std::lock_guard lk{ mtx };
            input = data;
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
    float GetJobWorkTime() const
    {
        return workTime;
    }
    size_t GetNumHeavyItemsProcessed() const
    {
        return numHeavyItemsProcessed;
    }
    ~Worker()
    {
        Kill();
    }
private:
    void ProcessData_()
    {
        numHeavyItemsProcessed = 0;
        for (const auto& task : input)
        {
            accumulation += task.Process();
            numHeavyItemsProcessed += task.heavy ? 1 : 0;
        }
    }
    void Run_()
    {
        std::unique_lock lk{ mtx };
        while (true)
        {
            ChiliTimer timer;
            cv.wait(lk, [this] {return !input.empty() || dying; });
            if (dying)
            {
                break;
            }

            timer.Mark();
            ProcessData_();
            workTime = timer.Peek();

            input = {};
            pMaster->SignalDone();
        }
    }
    MasterControl* pMaster;
    std::jthread thread;
    std::condition_variable cv;
    std::mutex mtx;
    // shared memory
    std::span<const Task> input;
    unsigned int accumulation = 0;
    bool dying = false;
    float workTime = -1.f;
    size_t numHeavyItemsProcessed;
};

int DoExperiment(bool stacked)
{
    const auto chunks = [=] {
        if (stacked) {
            return GenerateDatasetStacked();
        }
        else {
            return GenerateDatasetEvenly();
        }
    }();

    ChiliTimer totalTimer;
    totalTimer.Mark();

    MasterControl mctrl;

    std::vector<std::unique_ptr<Worker>> workerPtrs;
    for (size_t i = 0; i < WorkerCount; i++)
    {
        workerPtrs.push_back(std::make_unique<Worker>(&mctrl));
    }

    std::vector<ChunkTimingInfo> timings;
    timings.reserve(ChunkCount);

    ChiliTimer chunkTimer;
    for (const auto& chunk : chunks)
    {
        chunkTimer.Mark();
        for (size_t iSubset = 0; iSubset < WorkerCount; iSubset++)
        {
            workerPtrs[iSubset]->SetJob(std::span{ &chunk[iSubset * SubsetSize], SubsetSize });
        }
        mctrl.WaitForAllDone();
        const auto chunkTime = chunkTimer.Peek();

        timings.push_back({});
        for (size_t i = 0; i < WorkerCount; i++)
        {
            timings.back().numberOfHeavyItemsPerThread[i] = workerPtrs[i]->GetNumHeavyItemsProcessed();
            timings.back().timeSpentWorkingPerThread[i] = workerPtrs[i]->GetJobWorkTime();
        }
        timings.back().totalChunkTime = chunkTime;
    }
    const auto t = totalTimer.Peek();
    
    std::cout << "Processing took " << t << " seconds\n";
    unsigned int finalResult = 0;
    for (const auto& w : workerPtrs)
    {
        finalResult += w->GetResult();
    }
    std::cout << "Result is " << finalResult << std::endl;

    // output csv of chunk timings
    // worktime, idletime, numberofheavies x workers + total time, total heavies
    std::ofstream csv{ "timings.csv", std::ios_base::trunc };
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

    return 0;
}

int main(int argc, char** argv)
{
    using namespace std::string_literals;
    bool stacked = false;
    if (argc > 1 && argv[1] == "--stacked"s) {
        stacked = true;
    }
    return DoExperiment(stacked);
}