#include "thesis/resolve/ViewSupport.h"

namespace thesis::resolve {

const char* unavailableView(ShadingMode mode, const ViewSupport& support)
{
    if (showsWork(mode) && !support.countsWork)
        return "This tracer does not count work.";
    if (mode == ShadingMode::WorkStructure && !support.countsStructureSteps)
        return "This tracer does not report the steps of its search structure.";
    if (mode == ShadingMode::WorkCycles && !support.countsCycles)
        return "The clock cycles of this tracer are not valid: its rays may move between SMs, whose clocks differ.";
    if (!support.voxelModel &&
        (mode == ShadingMode::Normal || mode == ShadingMode::Faces || mode == ShadingMode::WorkCells ||
         mode == ShadingMode::WorkRuns || mode == ShadingMode::WorkHitCell))
        return "Needs a voxel model.";
    if ((mode == ShadingMode::WorkMipmap || mode == ShadingMode::WorkSkipLevel) && !support.skipsEmptySpace)
        return "Needs an encoding with a mipmap.";
    return nullptr;
}

} // namespace thesis::resolve
