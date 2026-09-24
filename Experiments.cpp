#include "Experiments.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace SpatialOverview::Experiments {
    namespace {
        struct SExperiment {
            std::string_view id;
            std::string_view title;
            std::string_view help;
        };

        constexpr std::array<SExperiment, 10> EXPERIMENTS{{
            {"baseline", "Baseline", "The canvas with its navigator. Super+Ctrl+G zooms out and you can type to find a window. Super+Ctrl+Alt+1 through 9 turn on one idea. Super+Ctrl+Alt+0 comes back here."},
            {"landing", "Landing", "Zoom out with Super+Ctrl+G. Drag the frame to choose the 100% view. Return lands there while the search is empty. Escape goes back. Alt+M maximizes."},
            {"labels", "Flat", "Zoom out without the lens, so labels and the search palette sit on a flat map."},
            {"alttab", "Alt-Tab", "Alt+Tab opens the map and moves to the next window. Alt+Shift+Tab goes backward. Return lands. Escape goes back."},
            {"areas", "Areas", "New windows stay tiled. Zoom out, then Alt+1 through 9 frames that workspace. Alt+W restores its tiling and lands."},
            {"quiet", "Quiet", "Zoom out. The grid and dim recede, and the lens is off."},
            {"persist", "Persist", "Positions and cameras are written to disk and put back while this experiment is selected."},
            {"depth", "Depth", "Zoom out. Windows pick up a shadow and a side edge. The clickable window does not move."},
            {"lens", "Lens", "The barrel lens stays on, including a little of it at normal size."},
            {"all", "All", "Landing, labels, alt-tab, areas, quiet, persist, and depth together. The lens stays off so the frame stays true."},
        }};

        ECanvasExperiment g_current = ECanvasExperiment::Baseline;

        std::filesystem::path stateFile() {
            if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state)
                return std::filesystem::path{state} / "spatial-overview" / "experiment";
            const char* home = std::getenv("HOME");
            return std::filesystem::path{home ? home : "/tmp"} / ".local" / "state" / "spatial-overview" / "experiment";
        }
    }

    void load() {
        std::ifstream in{stateFile()};
        std::string   id;
        if (in && std::getline(in, id))
            setByName(id);
    }

    bool setByName(std::string_view name) {
        if (name == "next" || name == "prev") {
            const int COUNT = static_cast<int>(EXPERIMENTS.size());
            const int STEP  = name == "next" ? 1 : -1;
            const int INDEX = (static_cast<int>(g_current) + STEP + COUNT) % COUNT;
            name            = EXPERIMENTS[INDEX].id;
        } else if (name == "status")
            return true;

        const auto FOUND = std::ranges::find_if(EXPERIMENTS, [&](const SExperiment& experiment) { return experiment.id == name; });
        if (FOUND == EXPERIMENTS.end())
            return false;

        g_current = static_cast<ECanvasExperiment>(FOUND - EXPERIMENTS.begin());

        std::error_code error;
        std::filesystem::create_directories(stateFile().parent_path(), error);
        std::ofstream out{stateFile(), std::ios::trunc};
        if (out)
            out << FOUND->id << '\n';
        return true;
    }

    ECanvasExperiment current() {
        return g_current;
    }

    bool on(ECanvasExperiment experiment) {
        if (experiment == ECanvasExperiment::Baseline)
            return g_current == ECanvasExperiment::Baseline;
        if (experiment == ECanvasExperiment::Lens)
            return g_current == ECanvasExperiment::Lens;
        return g_current == experiment || g_current == ECanvasExperiment::All;
    }

    std::string id() {
        return std::string{EXPERIMENTS[static_cast<size_t>(g_current)].id};
    }

    std::string title() {
        return std::string{EXPERIMENTS[static_cast<size_t>(g_current)].title};
    }

    std::string help() {
        return std::string{EXPERIMENTS[static_cast<size_t>(g_current)].help};
    }

    std::string statePath() {
        return stateFile().string();
    }
}
