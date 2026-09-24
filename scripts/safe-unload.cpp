// One-shot helper plugin for scripts/install-live.sh.
//
// Unloading spatialoverview runs that build's teardown, which re-tiles every
// canvas window. Builds before the layout-target fix could leave a window's
// layout target in the space of a workspace that no longer exists, and
// re-tiling such a window crashes Hyprland. This helper, loaded next to the
// old build, puts every layout target back in its window's workspace, then
// unloads spatialoverview and repairs again before Hyprland can delete any
// workspace the teardown emptied. All of it runs in one event-loop callback.
//
// It writes "checked repaired unresolved unloaded" for each pass to
// $XDG_RUNTIME_DIR/spatialoverview-safe-unload.<instance>, and skips the
// unload when a target cannot be repaired without risk.

#define WLR_USE_UNSTABLE

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>

#include <cstdlib>
#include <format>
#include <fstream>

namespace {
    HANDLE                    g_handle = nullptr;
    UP<SEventLoopDoLaterLock>             g_pending;

    struct SPass {
        int checked = 0, repaired = 0, unresolved = 0;
    };

    // Dwindle recalculates the space a tiled target leaves, which dereferences
    // that space's workspace, unless the target is its last tiled node.
    bool safeToDetach(const SP<Layout::ITarget>& target, const SP<Layout::CSpace>& from) {
        if (from->workspace() || target->floating())
            return true;
        int tiled = 0;
        for (const auto& ref : from->targets())
            if (const auto T = ref.lock(); T && !T->floating())
                ++tiled;
        return tiled <= 1;
    }

    SPass repair() {
        SPass pass;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w || !w->m_isMapped)
                continue;
            const auto TARGET = w->layoutTarget();
            const auto FROM   = TARGET ? TARGET->space() : nullptr;
            if (!FROM)
                continue;
            ++pass.checked;
            const auto WS = w->m_workspace;
            if (valid(WS) && FROM == WS->m_space && FROM->workspace() == WS)
                continue;
            // Pinned windows follow the monitor on their own; leave them unless stranded.
            if (w->m_pinned && FROM->workspace())
                continue;
            const bool PLACEABLE = valid(WS) && WS->m_space && (TARGET->floating() || (WS->m_monitor && Desktop::focusState()->monitor()));
            if (!PLACEABLE || !safeToDetach(TARGET, FROM)) {
                ++pass.unresolved;
                continue;
            }
            g_layoutManager->newTarget(TARGET, WS->m_space);
            ++pass.repaired;
        }
        return pass;
    }

    void run() {
        const auto BEFORE = repair();
        bool       unloaded = false;
        SPass      after;
        if (BEFORE.unresolved == 0) {
            for (auto* plugin : g_pPluginSystem->getAllPlugins()) {
                if (plugin->m_handle != g_handle && plugin->m_name == "spatialoverview") {
                    g_pPluginSystem->unloadPlugin(plugin);
                    unloaded = true;
                    break;
                }
            }
            after = repair();
        }

        const char* RUNTIME  = std::getenv("XDG_RUNTIME_DIR");
        const char* INSTANCE = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
        std::ofstream out{std::format("{}/spatialoverview-safe-unload.{}", RUNTIME ? RUNTIME : "/tmp", INSTANCE ? INSTANCE : "unknown")};
        out << std::format("before {} {} {}\nafter {} {} {}\nunloaded {}\n", BEFORE.checked, BEFORE.repaired, BEFORE.unresolved, after.checked, after.repaired, after.unresolved,
                           unloaded ? 1 : 0);
    }
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    g_handle = handle;
    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH)
        throw std::runtime_error("[spatialoverview-safe-unload] version mismatch");

    // Not from inside PLUGIN_INIT: the plugin list is mid-update while loading.
    g_pending = g_pEventLoopManager->doLaterLock([] { run(); });
    return {"spatialoverview-safe-unload", "Repairs layout targets, then unloads spatialoverview", "Kaolti", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_pending.reset(); // never leave a callback into unloaded code
}
