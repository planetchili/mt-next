#pragma once

inline constexpr bool ChunkMeasurementEnabled = false;
inline constexpr size_t WorkerCount = 4;
inline constexpr size_t ChunkSize = 8'000;
inline constexpr size_t ChunkCount = 100;
inline constexpr size_t LightIterations = 100;
inline constexpr size_t HeavyIterations = 1'000;
inline constexpr double ProbabilityHeavy = .15;

inline constexpr size_t SubsetSize = ChunkSize / WorkerCount;

static_assert(ChunkSize >= WorkerCount);
static_assert(ChunkSize% WorkerCount == 0);