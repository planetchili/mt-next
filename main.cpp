#include <iostream>
#include <random>
#include <array>
#include <ranges>
#include <limits>
#include <cmath>
#include <thread>
#include <mutex>
#include <span>
#include "ChiliTimer.h"

constexpr size_t DATASET_SIZE = 50'000'000;

void ProcessDataset(std::span<int> arr, int& sum)
{
    for (auto x : arr)
    {
        constexpr auto limit = (double)std::numeric_limits<int>::max();
        const auto y = (double)x / limit;
        sum += int(std::sin(std::cos(y)) * limit);
    }
}

std::vector<std::array<int, DATASET_SIZE>> GenerateDatasets()
{
    std::minstd_rand rne;
    std::vector<std::array<int, DATASET_SIZE>> datasets{ 4 };

    for (auto& arr : datasets)
    {
        std::ranges::generate(arr, rne);
    }

    return datasets;
}

int DoBiggie()
{
    auto datasets = GenerateDatasets();

    std::vector<std::thread> workers;
    ChiliTimer timer;

    struct Value
    {
        int v = 0;
        char padding[60];
    };
    Value sum[4];

    timer.Mark();
    for (size_t i = 0; i < 4; i++)
    {
        workers.push_back(std::thread{ ProcessDataset, std::span{ datasets[i] }, std::ref(sum[i].v) });
    }

    for (auto& worker : workers)
    {
        worker.join();
    }
    const auto t = timer.Peek();

    std::cout << "Processing took " << t << " seconds\n";
    std::cout << "Result is " << (sum[0].v + sum[1].v + sum[2].v + sum[3].v) << std::endl;

    return 0;
}

class MasterControl
{
public:
    MasterControl(int workerCount) : lk{ mtx }, workerCount{ workerCount } {}
    void SignalDone()
    {
        {
            std::lock_guard lk{ mtx };
            ++doneCount;
        }
        if (doneCount == workerCount)
        {
            cv.notify_one();
        }
    }
    void WaitForAllDone()
    {
        cv.wait(lk, [this] { return doneCount == workerCount; });
        doneCount = 0;
    }
private:
    std::condition_variable cv;
    std::mutex mtx;
    std::unique_lock<std::mutex> lk;
    int workerCount;
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
    void SetJob(std::span<int> data, int* pOut)
    {
        {
            std::lock_guard lk{ mtx };
            input = data;
            pOutput = pOut;
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
private:
    void Run_()
    {
        std::unique_lock lk{ mtx };
        while (true)
        {
            cv.wait(lk, [this] {return pOutput != nullptr || dying; });
            if (dying)
            {
                break;
            }
            ProcessDataset(input, *pOutput);
            pOutput = nullptr;
            input = {};
            pMaster->SignalDone();
        }
    }
    MasterControl* pMaster;
    std::jthread thread;
    std::condition_variable cv;
    std::mutex mtx;
    // shared memory
    std::span<int> input;
    int* pOutput = nullptr;
    bool dying = false;
};

int DoSmallies()
{
    auto datasets = GenerateDatasets();

    struct Value
    {
        int v = 0;
        char padding[60];
    };
    Value sum[4];

    ChiliTimer timer;
    timer.Mark();

    constexpr size_t workerCount = 4;
    MasterControl mctrl{ workerCount };
    std::vector<std::unique_ptr<Worker>> workerPtrs;
    for (size_t i = 0; i < workerCount; i++)
    {
        workerPtrs.push_back(std::make_unique<Worker>(&mctrl));
    }

    constexpr const auto subsetSize = DATASET_SIZE / 10'000;
    for (size_t i = 0; i < DATASET_SIZE; i += subsetSize)
    {
        for (size_t j = 0; j < 4; j++)
        {
            workerPtrs[j]->SetJob(std::span{ &datasets[j][i], subsetSize }, &sum[j].v);
        }
        mctrl.WaitForAllDone();
    }
    const auto t = timer.Peek();

    std::cout << "Processing took " << t << " seconds\n";
    std::cout << "Result is " << sum[0].v + sum[1].v + sum[2].v + sum[3].v << std::endl;

    for (auto& w : workerPtrs)
    {
        w->Kill();
    }

    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string{ argv[1] } == "--smol")
    {
        return DoSmallies();
    }
    return DoBiggie();
}