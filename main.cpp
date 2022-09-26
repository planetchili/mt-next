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
#include "ChiliTimer.h"

// experimental settings
constexpr size_t WorkerCount = 4;
constexpr size_t ChunkSize = 100;
constexpr size_t ChunkCount = 100;
constexpr size_t SubsetSize = ChunkSize / WorkerCount;
constexpr size_t LightIterations = 100;
constexpr size_t HeavyIterations = 1'000;
constexpr double ProbabilityHeavy = .02;

static_assert(ChunkSize >= WorkerCount);
static_assert(ChunkSize % WorkerCount == 0);

struct Task
{
    unsigned int val;
    bool heavy;
    unsigned int Process() const
    {
        const auto iterations = heavy ? HeavyIterations : LightIterations;
        double intermediate = 2. * (double(val) / double(std::numeric_limits<unsigned int>::max())) - 1.;
        for (size_t i = 0; i < iterations; i++)
        {
            intermediate = std::sin(std::cos(intermediate));
        }
        return unsigned int((intermediate + 1.) / 2. * double(std::numeric_limits<unsigned int>::max()));
    }
};

std::vector<std::array<Task, ChunkSize>> GenerateDatasets()
{
    std::minstd_rand rne;
    std::bernoulli_distribution hDist{ ProbabilityHeavy };

    std::vector<std::array<Task, ChunkSize>> chunks(ChunkCount);

    for (auto& chunk : chunks)
    {
        std::ranges::generate(chunk, [&] { return Task{ .val = rne(), .heavy = hDist(rne)}; });
    }

    return chunks;
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
private:
    void ProcessData_()
    {
        for (const auto& task : input)
        {
            accumulation += task.Process();
        }
    }
    void Run_()
    {
        std::unique_lock lk{ mtx };
        while (true)
        {
            cv.wait(lk, [this] {return !input.empty() || dying; });
            if (dying)
            {
                break;
            }
            ProcessData_();
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
};

int DoExperiment()
{
    const auto chunks = GenerateDatasets();

    ChiliTimer timer;
    timer.Mark();

    MasterControl mctrl;

    std::vector<std::unique_ptr<Worker>> workerPtrs;
    for (size_t i = 0; i < WorkerCount; i++)
    {
        workerPtrs.push_back(std::make_unique<Worker>(&mctrl));
    }

    for (const auto& chunk : chunks)
    {
        for (size_t iSubset = 0; iSubset < WorkerCount; iSubset++)
        {
            workerPtrs[iSubset]->SetJob(std::span{ &chunk[iSubset * SubsetSize], SubsetSize });
        }
        mctrl.WaitForAllDone();
    }
    const auto t = timer.Peek();

    std::cout << "Processing took " << t << " seconds\n";
    unsigned int finalResult = 0;
    for (const auto& w : workerPtrs)
    {
        finalResult += w->GetResult();
    }
    std::cout << "Result is " << finalResult << std::endl;

    for (auto& w : workerPtrs)
    {
        w->Kill();
    }

    return 0;
}

int main(int argc, char** argv)
{
    return DoExperiment();
}