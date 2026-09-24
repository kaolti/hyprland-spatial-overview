#include "IOverview.hpp"

#include <algorithm>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/output/Monitor.hpp>

static std::vector<SP<IOverview>> g_scrollOverviews;

const std::vector<SP<IOverview>>& scrollOverviews() {
    return g_scrollOverviews;
}

SP<IOverview> scrollOverviewForMonitor(PHLMONITOR monitor) {
    if (!monitor)
        return {};

    const auto IT = std::ranges::find_if(g_scrollOverviews, [&monitor](const auto& overview) {
        return overview && overview->pMonitor.lock() == monitor;
    });
    return IT == g_scrollOverviews.end() ? SP<IOverview>{} : *IT;
}

SP<IOverview> scrollOverviewAt(const Vector2D& point) {
    const auto IT = std::ranges::find_if(g_scrollOverviews, [&point](const auto& overview) {
        const auto monitor = overview ? overview->pMonitor.lock() : PHLMONITOR{};
        return monitor && monitor->logicalBox().containsPoint(point);
    });
    return IT == g_scrollOverviews.end() ? SP<IOverview>{} : *IT;
}

bool canvasSteppedAside(const PHLMONITOR& monitor); // scrollOverview.cpp

SP<IOverview> activeScrollOverview() {
    if (const auto monitor = Desktop::focusState()->monitor()) {
        if (const auto overview = scrollOverviewForMonitor(monitor))
            return overview;
        // A fullscreen window has the focused screen to itself (its canvas
        // stepped aside); the other screens' canvases must not take focus
        // or keys from it.
        if (canvasSteppedAside(monitor))
            return {};
    }

    if (g_pScrollOverview && std::ranges::find(g_scrollOverviews, g_pScrollOverview) != g_scrollOverviews.end())
        return g_pScrollOverview;

    return g_scrollOverviews.empty() ? SP<IOverview>{} : g_scrollOverviews.front();
}

void closeAll() {
    const auto overviews = g_scrollOverviews;
    for (const auto& overview : overviews) {
        if (overview)
            overview->close();
    }
}

void registerScrollOverview(const SP<IOverview>& overview) {
    if (!overview || std::ranges::find(g_scrollOverviews, overview) != g_scrollOverviews.end())
        return;

    g_scrollOverviews.emplace_back(overview);
    g_pScrollOverview = overview;
}

void unregisterScrollOverview(IOverview* overview) {
    if (!overview)
        return;

    std::erase_if(g_scrollOverviews, [overview](const auto& candidate) { return candidate.get() == overview; });
    g_pScrollOverview = activeScrollOverview();
}

void unregisterScrollOverviewForMonitor(PHLMONITOR monitor) {
    const auto overview = scrollOverviewForMonitor(monitor);
    if (overview)
        unregisterScrollOverview(overview.get());
}

void clearScrollOverviews() {
    auto overviews = std::move(g_scrollOverviews);
    g_scrollOverviews.clear();
    g_pScrollOverview.reset();
    overviews.clear();
}
