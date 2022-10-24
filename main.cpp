#include "popl.h"
#include "Preassigned.h"



int main(int argc, char** argv)
{
    using namespace popl;

    OptionParser op("Allowed options");
    auto stacked = op.add<Switch>("", "stacked", "Generate a stacked dataset");
    op.parse(argc, argv);

    return pre::DoExperiment(stacked->is_set());
}