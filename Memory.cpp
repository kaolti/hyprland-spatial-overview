#define WLR_USE_UNSTABLE

#include "Memory.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

#include "Navigator.hpp"

namespace SpatialOverview::Memory {
    namespace {
        struct SEntry {
            std::string klass;
            std::string title;
            CBox        box;
            int64_t     seen   = 0;
            uint64_t    window = 0; // stable id of the open window this entry describes
        };

        constexpr size_t MAX_ENTRIES = 96;

        bool                                      g_loaded = false;
        std::vector<SEntry>                       g_entries;
        std::unordered_map<std::string, Vector2D> g_cameras;

        // Hand-edited or damaged files must never feed NaN, infinity or
        // absurd sizes into window geometry or the camera.
        bool sane(double value, double limit) {
            return std::isfinite(value) && std::abs(value) <= limit;
        }

        bool saneBox(const CBox& box) {
            return sane(box.x, 1e6) && sane(box.y, 1e6) && sane(box.width, 16384) && sane(box.height, 16384) && box.width >= 1 && box.height >= 1;
        }

        std::string clean(std::string value) {
            std::ranges::replace(value, '\t', ' ');
            std::ranges::replace(value, '\n', ' ');
            std::ranges::replace(value, '\r', ' ');
            return value;
        }

        void load() {
            if (g_loaded)
                return;
            g_loaded = true;

            std::ifstream in{path()};
            std::string   line;
            while (in && std::getline(in, line)) {
                std::vector<std::string> fields;
                std::string              field;
                std::istringstream       stream{line};
                while (std::getline(stream, field, '\t'))
                    fields.push_back(field);
                try {
                    if (fields.size() == 4 && fields[0] == "camera") {
                        const Vector2D CAMERA{std::stod(fields[2]), std::stod(fields[3])};
                        if (sane(CAMERA.x, 1e6) && sane(CAMERA.y, 1e6))
                            g_cameras[fields[1]] = CAMERA;
                    } else if (fields.size() == 8 && fields[0] == "window") {
                        const CBox BOX{std::stod(fields[3]), std::stod(fields[4]), std::stod(fields[5]), std::stod(fields[6])};
                        if (saneBox(BOX))
                            g_entries.push_back({.klass = fields[1], .title = fields[2], .box = BOX, .seen = std::stoll(fields[7])});
                    }
                } catch (...) {
                    // A damaged line only costs that one window its memory.
                }
            }
        }
    }

    std::string path() {
        std::filesystem::path base;
        if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state)
            base = state;
        else
            base = std::filesystem::path{std::getenv("HOME") ? std::getenv("HOME") : "/tmp"} / ".local" / "state";
        return (base / "spatial-overview" / "canvas-memory.tsv").string();
    }

    std::optional<CBox> claim(const PHLWINDOW& window, bool restoring) {
        if (!window)
            return std::nullopt;
        load();

        const auto KLASS = clean(window->m_class.empty() ? window->m_initialClass : window->m_class);
        const auto TITLE = clean(Navigator::displayTitle(window));
        if (KLASS.empty())
            return std::nullopt;

        SEntry* best = nullptr;
        for (auto& entry : g_entries) {
            if (entry.window || entry.klass != KLASS)
                continue;
            if (entry.title == TITLE && (!best || best->title != TITLE || entry.seen > best->seen))
                best = &entry;
            else if (restoring && (!best || (best->title != TITLE && entry.seen > best->seen)))
                best = &entry;
        }
        if (!best)
            return std::nullopt;

        if (!restoring) {
            // Outside a restore, only a lone app returning to its own spot:
            // a second terminal belongs next to you, not in the first one's
            // old place.
            if (best->title != TITLE)
                return std::nullopt;
            const auto& WINDOWS = Desktop::windowState()->windows();
            const bool  SIBLING = std::ranges::any_of(WINDOWS, [&](const PHLWINDOW& other) {
                return other && other != window && other->m_isMapped && clean(other->m_class) == KLASS;
            });
            if (SIBLING)
                return std::nullopt;
        }

        best->window = window->m_stableID;
        return best->box;
    }

    std::optional<Vector2D> camera(const std::string& monitor) {
        load();
        if (const auto IT = g_cameras.find(monitor); IT != g_cameras.end())
            return IT->second;
        return std::nullopt;
    }

    void save(const std::vector<SWindowPlacement>& windows, const std::unordered_map<std::string, Vector2D>& cameras) {
        load();
        const int64_t NOW = std::time(nullptr);

        std::vector<SEntry> next;
        next.reserve(windows.size() + g_entries.size());
        for (const auto& placement : windows) {
            if (!placement.window || !saneBox(placement.box))
                continue;
            const auto KLASS = clean(placement.window->m_class.empty() ? placement.window->m_initialClass : placement.window->m_class);
            if (KLASS.empty())
                continue;
            next.push_back({.klass = KLASS, .title = clean(Navigator::displayTitle(placement.window)), .box = placement.box, .seen = NOW,
                            .window = placement.window->m_stableID});
        }

        // Entries describing a window that is still open are superseded by
        // the lines above; a window that has closed keeps its last home.
        for (auto entry : g_entries) {
            if (entry.window && std::ranges::any_of(windows, [&entry](const SWindowPlacement& placement) {
                    return placement.window && placement.window->m_stableID == entry.window;
                }))
                continue;
            entry.window = 0;
            const bool DUPLICATE = std::ranges::any_of(next, [&entry](const SEntry& current) {
                return current.klass == entry.klass && current.title == entry.title && current.box == entry.box;
            });
            if (!DUPLICATE)
                next.push_back(entry);
        }

        std::ranges::stable_sort(next, [](const SEntry& a, const SEntry& b) { return a.seen > b.seen; });
        if (next.size() > MAX_ENTRIES)
            next.resize(MAX_ENTRIES);
        g_entries = std::move(next);
        for (const auto& [monitor, offset] : cameras) {
            if (sane(offset.x, 1e6) && sane(offset.y, 1e6))
                g_cameras[monitor] = offset;
        }

        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path{path()}.parent_path(), error);
        const auto    TEMPORARY = path() + ".next";
        std::ofstream out{TEMPORARY, std::ios::trunc};
        if (!out)
            return;
        out << std::fixed << std::setprecision(1) << "# spatial-overview canvas memory v1\n";
        for (const auto& [monitor, offset] : g_cameras)
            out << "camera\t" << clean(monitor) << '\t' << offset.x << '\t' << offset.y << '\n';
        for (const auto& entry : g_entries)
            out << "window\t" << entry.klass << '\t' << entry.title << '\t' << entry.box.x << '\t' << entry.box.y << '\t' << entry.box.width << '\t' << entry.box.height
                << '\t' << entry.seen << '\n';
        out.close();
        if (out)
            std::filesystem::rename(TEMPORARY, path(), error);
    }
}
