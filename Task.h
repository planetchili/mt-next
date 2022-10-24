#pragma once
#include <random>
#include <array>
#include <ranges>
#include <cmath>
#include <numbers>
#include "Constants.h"


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

using Dataset = std::vector<std::array<Task, ChunkSize>>;

Dataset GenerateDatasetRandom()
{
    std::minstd_rand rne;
    std::uniform_real_distribution vDist{ 0., 2. * std::numbers::pi };
    std::bernoulli_distribution hDist{ ProbabilityHeavy };

    Dataset chunks(ChunkCount);

    for (auto& chunk : chunks)
    {
        std::ranges::generate(chunk, [&] { return Task{ .val = vDist(rne), .heavy = hDist(rne) }; });
    }

    return chunks;
}

Dataset GenerateDatasetEven()
{
    std::minstd_rand rne;
    std::uniform_real_distribution vDist{ 0., 2. * std::numbers::pi };

    Dataset chunks(ChunkCount);

    for (auto& chunk : chunks)
    {
        std::ranges::generate(chunk, [&, acc = 0.]() mutable {
            bool heavy = false;
            if ((acc += ProbabilityHeavy) >= 1.)
            {
                acc -= 1.;
                heavy = true;
            }
            return Task{ .val = vDist(rne), .heavy = heavy };
            });
    }

    return chunks;
}

Dataset GenerateDatasetStacked()
{
    auto chunks = GenerateDatasetEven();

    for (auto& chunk : chunks)
    {
        std::ranges::partition(chunk, std::identity{}, &Task::heavy);
    }

    return chunks;
}