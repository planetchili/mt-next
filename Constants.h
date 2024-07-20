#pragma once
#include "popl.h"
#include <iostream>

inline size_t WorkerCount = 4;
inline size_t DatasetSize = 2'000;
inline size_t LightIterations = 1'000;
inline size_t HeavyIterations = 10'000;
inline double ProbabilityHeavy = .15;
inline int AsyncSleep = 20;

void ParseCli(int argc, const char** argv)
{
	using namespace popl;
	OptionParser op;
	op.add<Value<size_t>>("", "worker-count", "")->assign_to(&WorkerCount);
	op.add<Value<size_t>>("", "dataset-size", "")->assign_to(&DatasetSize);
	op.add<Value<size_t>>("", "light-iterations", "")->assign_to(&LightIterations);
	op.add<Value<size_t>>("", "heavy-iterations", "")->assign_to(&HeavyIterations);
	op.add<Value<double>>("", "probability-heavy", "")->assign_to(&ProbabilityHeavy);
	op.add<Value<int>>("", "async-sleep", "")->assign_to(&AsyncSleep);
	op.parse(argc, argv);
}