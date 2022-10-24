#include "popl.h"
#include "Preassigned.h"
#include "Queued.h"
#include "Task.h"



int main(int argc, char** argv)
{
    using namespace popl;

    // define and parse cli options
    OptionParser op("Allowed options");
    auto stacked = op.add<Switch>("", "stacked", "Generate a stacked dataset");
    auto even = op.add<Switch>("", "even", "Generate an even");
    auto queued = op.add<Switch>("", "queued", "Used queued approach");
    op.parse(argc, argv);

    // generate dataset
    Dataset data;
    if (stacked->is_set()) {
        data = GenerateDatasetStacked();
    }
    else if (even->is_set()) {
        data = GenerateDatasetEven();
    }
    else {
        data = GenerateDatasetRandom();
    }

    // run experiment
    if (queued->is_set()) {
        return que::DoExperiment(std::move(data));
    }
    else {
        return pre::DoExperiment(std::move(data));
    }
}