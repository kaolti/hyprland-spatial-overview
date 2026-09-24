#pragma once

#include <string>
#include <string_view>

// One idea at a time. All stacks every idea except the lens, so the landing
// frame stays an accurate picture of the 100% view.
enum class ECanvasExperiment : int {
    Baseline = 0,
    Landing  = 1,
    Labels   = 2,
    AltTab   = 3,
    Areas    = 4,
    Quiet    = 5,
    Persist  = 6,
    Depth    = 7,
    Lens     = 8,
    All      = 9,
};

namespace SpatialOverview::Experiments {
    void               load();
    bool               setByName(std::string_view name);
    ECanvasExperiment  current();
    bool               on(ECanvasExperiment experiment);
    std::string        id();
    std::string        title();
    std::string        help();
    std::string        statePath();
}
