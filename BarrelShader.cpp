#include "BarrelShader.hpp"

#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <format>
#include <fstream>
#include <any>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#define private public
#define protected public
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#undef protected
#undef private

#include "Config.hpp"
#include "Tuning.hpp"

namespace SpatialOverview::BarrelShader {
namespace {

int         users = 0;
bool        installed = false;
GLuint      installedProgram = 0;
std::string installedPath;

struct SPendingHud {
    SP<Render::ITexture>    texture;
    std::array<float, 4>    rect{};
    float                   alpha = 0.F;
    SP<Render::ITexture>    atlas;
    std::vector<SHudRegion> regions;
    PHLMONITORREF           monitor;
} pendingHud;

constexpr GLint HUD_TEXTURE_UNIT   = 7;
constexpr GLint ATLAS_TEXTURE_UNIT = 6;
constexpr int   MAX_HUD_REGIONS    = 4;

void bindOnUnit(const SP<Render::ITexture>& texture, GLint unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    texture->bind();
    texture->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    texture->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    texture->setTexParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    texture->setTexParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// The lens ships inside the plugin, so it needs no path on disk. Hyprland
// loads screen shaders from files, so the built-in source is written to the
// runtime directory under a name derived from its content (a changed shader
// gets a new name, which makes Hyprland compile it afresh).
const char BUILT_IN_SHADER[] = {
#embed "shaders/barrel.frag"
    , 0};

std::string builtInShaderPath() {
    static std::string written;
    if (!written.empty() && std::filesystem::is_regular_file(written))
        return written;

    const std::string_view SOURCE{BUILT_IN_SHADER};
    uint64_t               hash = 1469598103934665603ULL; // FNV-1a
    for (const unsigned char C : SOURCE) {
        hash ^= C;
        hash *= 1099511628211ULL;
    }

    const char*           RUNTIME = std::getenv("XDG_RUNTIME_DIR");
    std::filesystem::path dir     = RUNTIME && *RUNTIME ? std::filesystem::path{RUNTIME} / "spatialoverview" :
                                                          std::filesystem::path{"/tmp"} / std::format("spatialoverview-{}", getuid());
    std::error_code       error;
    std::filesystem::create_directories(dir, error);
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);

    const auto FILE = dir / std::format("barrel-{:016x}.frag", hash);
    if (!std::filesystem::is_regular_file(FILE, error)) {
        const auto TEMP = FILE.string() + ".tmp";
        {
            std::ofstream out{TEMP, std::ios::trunc};
            out << SOURCE;
            if (!out.good())
                return {};
        }
        std::filesystem::rename(TEMP, FILE, error);
        if (error)
            return {};
    }
    written = FILE.string();
    return written;
}

void requestReload() {
    if (!g_pHyprRenderer)
        return;

    g_pHyprRenderer->m_reloadScreenShader = true;
    if (const auto MONITOR = g_pHyprRenderer->m_renderData.pMonitor.lock())
        g_pHyprRenderer->damageMonitor(MONITOR);
}

}

std::string shaderPath() {
    const auto CONFIGURED = ScrollOverview::Config::getBarrelShaderPath();
    return CONFIGURED.empty() ? builtInShaderPath() : CONFIGURED;
}

void enable() {
    ++users;
}

void disable() {
    users = std::max(0, users - 1);
    if (users != 0)
        return;

    // The compositor reloads the user's configured screen shader at the start
    // of the next frame, when the EGL context is guaranteed to be current.
    installed        = false;
    installedProgram = 0;
    installedPath.clear();
    requestReload();
}

void updateUniforms(float progress) {
    if (!Render::GL::g_pHyprOpenGL)
        return;

    const auto PATH = shaderPath();
    const bool WANTED = users > 0 && ScrollOverview::Config::getBarrelEnabled() && !PATH.empty() && std::filesystem::is_regular_file(PATH);

    if (!WANTED) {
        if (installed) {
            // We are currently inside a render pass, so restoring here is safe
            // and prevents even a single frame of overview-only distortion from
            // leaking into the normal desktop.
            Render::GL::g_pHyprOpenGL->applyScreenShader(ScrollOverview::Config::getValue<std::string>("decoration:screen_shader"));
            installed        = false;
            installedProgram = 0;
            installedPath.clear();
        }
        return;
    }

    const auto SHADER = Render::GL::g_pHyprOpenGL->m_finalScreenShader;
    if (!SHADER)
        return;

    // Hyprland may replace the final shader after a config reload. Comparing
    // program IDs lets us reinstall ours exactly once, rather than recompiling
    // it every frame.
    if (!installed || installedPath != PATH || SHADER->program() != installedProgram) {
        Render::GL::g_pHyprOpenGL->applyScreenShader(PATH);
        installedProgram = SHADER->program();
        installedPath    = PATH;
        installed        = installedProgram != 0;
    }

    if (!installed)
        return;

    const float PROGRESS = std::clamp(progress, 0.F, 1.F);
    const auto PREVIOUS = Render::GL::g_pHyprOpenGL->useShader(SHADER);
    SHADER->setUniformFloat(SHADER_DISTORT, ScrollOverview::Config::getBarrelStrength() * PROGRESS);
    SHADER->setUniformFloat(SHADER_CONTRAST, 1.F + (ScrollOverview::Config::getBarrelEdgeScale() - 1.F) * PROGRESS);
    SHADER->setUniformFloat(SHADER_BRIGHTNESS, ScrollOverview::Config::getBarrelFeather() * PROGRESS);
    const auto set1f = [&SHADER](const char* name, double value) {
        if (const auto LOCATION = glGetUniformLocation(SHADER->program(), name); LOCATION >= 0)
            glUniform1f(LOCATION, sc<float>(value));
    };
    set1f("edgeBlur", Tuning::number("distortion:edge_blur") * PROGRESS);
    set1f("edgeBlurStart", Tuning::number("distortion:edge_blur_start"));
    set1f("vignette", Tuning::number("distortion:vignette") * PROGRESS);
    set1f("chromatic", Tuning::number("distortion:chromatic") * PROGRESS);
    if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "minimapPanel"); LOCATION >= 0)
        glUniform4f(LOCATION, 0.F, 0.F, 0.F, 0.F);
    if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "minimapArrangeButton"); LOCATION >= 0)
        glUniform4f(LOCATION, 0.F, 0.F, 0.F, 0.F);
    if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "hudRect"); LOCATION >= 0)
        glUniform4f(LOCATION, 0.F, 0.F, 0.F, 0.F);
    Render::GL::g_pHyprOpenGL->useShader(PREVIOUS);
}

bool updateHud(const SP<Render::ITexture>& texture, const std::array<float, 4>& rect, float alpha, const SP<Render::ITexture>& atlas,
               const std::vector<SHudRegion>& regions) {
    if (!installed || !Render::GL::g_pHyprOpenGL || !texture || !texture->m_texID)
        return false;

    const auto SHADER = Render::GL::g_pHyprOpenGL->m_finalScreenShader;
    if (!SHADER || !SHADER->program() || glGetUniformLocation(SHADER->program(), "hudRect") < 0 ||
        glGetUniformLocation(SHADER->program(), "hudTex") < 0)
        return false;

    pendingHud = {.texture = texture, .rect = rect, .alpha = std::clamp(alpha, 0.F, 1.F), .atlas = atlas && atlas->m_texID ? atlas : nullptr, .regions = regions,
                  .monitor = g_pHyprRenderer->m_renderData.pMonitor};
    return true;
}

void applyPendingHud() {
    auto pending = std::move(pendingHud);
    pendingHud   = {};
    if (!installed || !Render::GL::g_pHyprOpenGL)
        return;

    const auto SHADER = Render::GL::g_pHyprOpenGL->m_finalScreenShader;
    if (!SHADER || !SHADER->program())
        return;
    const auto RECT    = glGetUniformLocation(SHADER->program(), "hudRect");
    const auto SAMPLER = glGetUniformLocation(SHADER->program(), "hudTex");
    if (RECT < 0 || SAMPLER < 0)
        return;

    // Every frame of every monitor either gets its own palette or an empty
    // rectangle, so a palette can never linger on the wrong output.
    const bool SHOW = pending.texture && pending.texture->m_texID && pending.monitor && pending.monitor == g_pHyprRenderer->m_renderData.pMonitor;

    // The final pass samples the frame from unit 0. Park the HUD textures on
    // units nothing else uses between here and that pass.
    const bool ATLAS = SHOW && pending.atlas && !pending.regions.empty() && glGetUniformLocation(SHADER->program(), "hudAtlas") >= 0;
    if (SHOW) {
        GLint previousUnit = GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previousUnit);
        bindOnUnit(pending.texture, HUD_TEXTURE_UNIT);
        if (ATLAS)
            bindOnUnit(pending.atlas, ATLAS_TEXTURE_UNIT);
        glActiveTexture(previousUnit);
    }

    const auto PREVIOUS = Render::GL::g_pHyprOpenGL->useShader(SHADER);
    glUniform1i(SAMPLER, HUD_TEXTURE_UNIT);
    if (SHOW)
        glUniform4f(RECT, pending.rect[0], pending.rect[1], pending.rect[2], pending.rect[3]);
    else
        glUniform4f(RECT, 0.F, 0.F, 0.F, 0.F);
    if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "hudAlpha"); LOCATION >= 0)
        glUniform1f(LOCATION, pending.alpha);

    const int REGIONS = ATLAS ? std::min<int>(pending.regions.size(), MAX_HUD_REGIONS) : 0;
    if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "hudAtlasCount"); LOCATION >= 0)
        glUniform1i(LOCATION, REGIONS);
    if (REGIONS > 0) {
        std::vector<float> rects, uvs;
        for (int i = 0; i < REGIONS; ++i) {
            rects.insert(rects.end(), pending.regions[i].screen.begin(), pending.regions[i].screen.end());
            uvs.insert(uvs.end(), pending.regions[i].uv.begin(), pending.regions[i].uv.end());
        }
        if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "hudAtlas"); LOCATION >= 0)
            glUniform1i(LOCATION, ATLAS_TEXTURE_UNIT);
        if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "hudAtlasRects[0]"); LOCATION >= 0)
            glUniform4fv(LOCATION, REGIONS, rects.data());
        if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "hudAtlasUVs[0]"); LOCATION >= 0)
            glUniform4fv(LOCATION, REGIONS, uvs.data());
    }
    Render::GL::g_pHyprOpenGL->useShader(PREVIOUS);
}

void discardPendingHud() {
    pendingHud = {};
}

bool updateMinimap(const SMinimapData& data) {
    if (!installed || !Render::GL::g_pHyprOpenGL)
        return false;

    const auto SHADER = Render::GL::g_pHyprOpenGL->m_finalScreenShader;
    if (!SHADER || !SHADER->program())
        return false;

    const auto PREVIOUS = Render::GL::g_pHyprOpenGL->useShader(SHADER);
    const auto set1f = [&](const char* name, float value) {
        if (const auto location = glGetUniformLocation(SHADER->program(), name); location >= 0)
            glUniform1f(location, value);
    };
    const auto set1i = [&](const char* name, int value) {
        if (const auto location = glGetUniformLocation(SHADER->program(), name); location >= 0)
            glUniform1i(location, value);
    };
    const auto set3f = [&](const char* name, const std::array<float, 3>& value) {
        if (const auto location = glGetUniformLocation(SHADER->program(), name); location >= 0)
            glUniform3f(location, value[0], value[1], value[2]);
    };
    const auto set4f = [&](const char* name, const std::array<float, 4>& value) {
        if (const auto location = glGetUniformLocation(SHADER->program(), name); location >= 0)
            glUniform4f(location, value[0], value[1], value[2], value[3]);
    };

    constexpr size_t MAX_WINDOWS = 64;
    const auto       WINDOWCOUNT = std::min(data.windows.size(), MAX_WINDOWS);
    std::vector<float> windowRects;
    windowRects.reserve(WINDOWCOUNT * 4);
    for (size_t i = 0; i < WINDOWCOUNT; ++i)
        windowRects.insert(windowRects.end(), data.windows[i].begin(), data.windows[i].end());

    set4f("minimapPanel", data.panel);
    set4f("minimapArrangeButton", data.arrangeButton);
    set4f("minimapViewport", data.viewport);
    set3f("minimapActiveColor", data.activeColor);
    set3f("minimapInactiveColor", data.inactiveColor);
    set1f("minimapOpacity", data.opacity);
    set1f("minimapTransition", data.transition);
    set1i("minimapWindowCount", sc<int>(WINDOWCOUNT));
    set1i("minimapFocusedIndex", data.focusedIndex >= 0 && sc<size_t>(data.focusedIndex) < WINDOWCOUNT ? data.focusedIndex : -1);
    if (!windowRects.empty()) {
        if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "minimapWindows[0]"); LOCATION >= 0)
            glUniform4fv(LOCATION, sc<GLsizei>(WINDOWCOUNT), windowRects.data());

        std::vector<float> dims(WINDOWCOUNT, 0.F);
        for (size_t i = 0; i < WINDOWCOUNT && i < data.windowAlpha.size(); ++i)
            dims[i] = 1.F - std::clamp(data.windowAlpha[i], 0.F, 1.F);
        if (const auto LOCATION = glGetUniformLocation(SHADER->program(), "minimapWindowDim[0]"); LOCATION >= 0)
            glUniform1fv(LOCATION, sc<GLsizei>(WINDOWCOUNT), dims.data());
    }

    Render::GL::g_pHyprOpenGL->useShader(PREVIOUS);
    return true;
}

}
