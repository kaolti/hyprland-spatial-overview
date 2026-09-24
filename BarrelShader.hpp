#pragma once

#include "globals.hpp"

#include <array>
#include <string>
#include <vector>

namespace Render {
    class ITexture;
}

namespace SpatialOverview::BarrelShader {

struct SMinimapData {
    std::array<float, 4>              panel{};
    std::array<float, 4>              arrangeButton{};
    std::array<float, 4>              viewport{};
    std::vector<std::array<float, 4>> windows;
    std::vector<float>                windowAlpha; // per window, 1 = normal emphasis
    int                               focusedIndex = -1;
    std::array<float, 3>              activeColor{};
    std::array<float, 3>              inactiveColor{};
    float                             opacity    = 0.F;
    float                             transition = 0.F;
};

// The lens shader in use: distortion:shader_path, or the one built into the
// plugin when that is empty.
std::string shaderPath();
void enable();
void disable();
void updateUniforms(float progress);
bool updateMinimap(const SMinimapData& data);
// Composes a texture over the lensed frame at a fixed screen rectangle
// (device pixels, top-left origin) for the monitor being rendered. Returns
// false when no lens shader is installed, in which case the caller draws the
// texture itself. The texture is bound at RENDER_LAST_MOMENT, just before the
// final pass that samples it (see applyPendingHud).
struct SHudRegion {
    std::array<float, 4> screen{}; // device px, top-left origin
    std::array<float, 4> uv{};     // x, y, w, h in the atlas
};
bool updateHud(const SP<Render::ITexture>& texture, const std::array<float, 4>& rect, float alpha, const SP<Render::ITexture>& atlas = nullptr,
               const std::vector<SHudRegion>& regions = {});
void applyPendingHud();
void discardPendingHud();

}
