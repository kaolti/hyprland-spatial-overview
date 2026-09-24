#define WLR_USE_UNSTABLE

#include "Tuning.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <unordered_map>

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

#include "Config.hpp"

namespace SpatialOverview::Tuning {
    namespace {
        using enum EKind;

        // Only what can be seen while the tuner is open: settings that act
        // during the zoom animation (lens timing, animation speed) stay in
        // the config, where they are documented.
        const std::vector<SParam> PARAMS = {
            // ---- lens ------------------------------------------------------------------
            {"distortion:strength", "Lens", "Curvature", "How strongly the lens bends the canvas; below zero it bends inward", FLOAT, 0.14, -0.5, 0.5, 0.01, "", false},
            {"distortion:edge_scale", "Lens", "Edge overscan", "Enlarges the view so the bent corners stay filled", FLOAT, 1.08, 1.0, 1.5, 0.01, "×", false},
            {"distortion:feather", "Lens", "Edge fade", "Width of the fade to black at the screen edges", FLOAT, 0.025, 0.0, 0.25, 0.005, "", false},
            {"distortion:edge_blur", "Lens", "Edge blur", "Lens blur toward the screen edges", FLOAT, 0.0, 0.0, 1.0, 0.02, "%"},
            {"distortion:edge_blur_start", "Lens", "Blur begins at", "Where the edge blur starts: 0% at the center, 100% in the corners", FLOAT, 0.45, 0.0, 0.95, 0.01,
             "%"},
            {"distortion:vignette", "Lens", "Vignette", "Darkens the screen toward its edges", FLOAT, 0.0, 0.0, 1.0, 0.02, "%"},
            {"distortion:chromatic", "Lens", "Color fringe", "Splits red and blue apart toward the edges, like a real lens", FLOAT, 0.0, 0.0, 1.0, 0.02, "%"},

            // ---- background --------------------------------------------------------------
            {"canvas:background_dim", "Background", "Darkening", "Dims the wallpaper behind the zoomed-out canvas", FLOAT, 0.35, 0.0, 0.9, 0.01, "%", false},
            {"blur", "Background", "Blur", "Blurs the wallpaper when zoomed out", BOOL, 0, 0, 1, 1, "", false},
            {"blur_strength", "Background", "Blur strength", "How much the wallpaper blurs", FLOAT, 0.7, 0.0, 1.0, 0.01, "%", false},
            {"parallax:enabled", "Background", "Parallax", "The wallpaper drifts behind the windows as you pan", BOOL, 1, 0, 1, 1},
            {"parallax:strength", "Background", "Parallax amount", "How far the wallpaper moves, relative to the windows", FLOAT, 0.06, 0.0, 0.5, 0.005, "%"},
            {"parallax:depth", "Background", "Parallax depth", "How much the wallpaper shrinks as you zoom out", FLOAT, 0.12, 0.0, 1.0, 0.02, "%"},
            {"parallax:desktop", "Background", "Parallax at rest", "Also drift the wallpaper when panning the full-size desktop", BOOL, 0, 0, 1, 1},

            // ---- grid --------------------------------------------------------------------
            {"canvas:grid_enabled", "Grid", "Grid", "Shows the canvas grid", BOOL, 1, 0, 1, 1, "", false},
            {"canvas:grid_style", "Grid", "Style", "Dots or lines", INT, 1, 0, 1, 1, "", false, {"Lines", "Dots"}},
            {"canvas:grid_size", "Grid", "Spacing", "Distance between grid marks; windows snap to it", INT, 160, 24, 400, 2, "px", false},
            {"canvas:grid_dot_size", "Grid", "Dot size", "Diameter of the grid dots", FLOAT, 3.2, 2.0, 16.0, 0.1, "px", false},
            {"canvas:grid_width", "Grid", "Line width", "Width of the grid lines", INT, 1, 1, 8, 1, "px", false},
            {"canvas:grid_opacity", "Grid", "Opacity", "How visible the grid is", FLOAT, 0.16, 0.0, 0.5, 0.01, "%", false},

            // ---- camera ------------------------------------------------------------------
            {"canvas:initial_zoom", "Camera", "Zoomed-out level", "How far Super+Ctrl+G zooms out", FLOAT, 0.72, 0.15, 0.95, 0.01, "%", false},
            {"input:pan_sensitivity", "Camera", "Pan speed", "Camera speed when dragging the canvas", FLOAT, 1.0, 0.05, 5.0, 0.05, "×", false},
            {"canvas:minimap_enabled", "Camera", "Minimap", "Shows the minimap in the corner", BOOL, 1, 0, 1, 1, "", false},
            {"canvas:minimap_opacity", "Camera", "Minimap opacity", "How solid the minimap is", FLOAT, 0.72, 0.0, 1.0, 0.02, "%", false},

            // ---- hud ---------------------------------------------------------------------
            {"navigator:accent", "HUD", "Accent color", "Selection, caret, key names and outlines; ←→ presets, ⇧←→ fine hue", COLOR, 0, 0, 0, 1, "", false},
            {"navigator:hud_scale", "HUD", "Scale", "Size of everything in the HUD", FLOAT, 1.0, 0.5, 2.0, 0.05, "×"},
            {"navigator:width", "HUD", "Width", "Width of the search palette", FLOAT, 720, 360, 1400, 10, "px"},
            {"navigator:top", "HUD", "Distance from top", "Where the search palette sits", FLOAT, 0.075, 0.0, 0.5, 0.005, "%"},
            {"navigator:rows", "HUD", "Result rows", "How many results show at once", INT, 6, 3, 12, 1},
            {"navigator:row_height", "HUD", "Row height", "Height of each result", FLOAT, 58, 36, 96, 1, "px"},
            {"navigator:rounding", "HUD", "Corner radius", "Roundness of the results and the search field", FLOAT, 11, 0, 30, 1, "px"},
            {"navigator:panel_opacity", "HUD", "Panel opacity", "How solid the palette background is; lower is glassier", FLOAT, 0.94, 0.0, 1.0, 0.01, "%"},
            {"navigator:uppercase", "HUD", "Uppercase", "Capitals, or text as written", BOOL, 1, 0, 1, 1},
            {"navigator:letter_spacing", "HUD", "Letter spacing", "Space between letters", FLOAT, 1.0, 0.0, 3.0, 0.05, "×"},
            {"navigator:query_size", "HUD", "Search text", "Size of the search text", FLOAT, 17, 10, 32, 0.5, "px"},
            {"navigator:title_size", "HUD", "Title text", "Size of result titles", FLOAT, 14, 9, 26, 0.5, "px"},
            {"navigator:detail_size", "HUD", "Detail text", "Size of app names and distances", FLOAT, 10.5, 7, 20, 0.5, "px"},
            {"navigator:corner_size", "HUD", "Corner text", "Size of the readouts in the screen corners", FLOAT, 12, 8, 22, 0.5, "px"},
            {"navigator:corner_labels", "HUD", "Corner readouts", "Shows the readouts in the screen corners", BOOL, 1, 0, 1, 1},
            {"navigator:labels", "HUD", "Window labels", "Shows window titles on the map", BOOL, 1, 0, 1, 1, "", false},
            {"navigator:label_size", "HUD", "Window label text", "Size of the window titles on the map", FLOAT, 10.5, 7, 20, 0.5, "px"},
            {"navigator:dim_unmatched", "HUD", "Dim non-matches", "How far windows that don't match the search fade", FLOAT, 0.7, 0.0, 1.0, 0.02, "%", false},

            // ---- search field -------------------------------------------------------------
            {"navigator:search_height", "Search field", "Height", "Height of the search field", FLOAT, 60, 36, 110, 1, "px"},
            {"navigator:search_border", "Search field", "Border", "Border width around the search field; 0 for none", FLOAT, 1.5, 0.0, 6.0, 0.25, "px"},
            {"navigator:search_border_opacity", "Search field", "Border opacity", "How solid the search field border is", FLOAT, 0.85, 0.0, 1.0, 0.02, "%"},
            {"navigator:search_glow", "Search field", "Glow", "Soft glow around the search field", FLOAT, 0.3, 0.0, 1.0, 0.02, "%"},
        };

        // Accent presets, chosen to hold dark text or white text cleanly.
        struct SPreset {
            const char* name;
            const char* value; // #rrggbb, or auto for the theme's border color
        };
        const std::vector<SPreset> PRESETS = {
            {"Orange", "#ff6b1a"}, {"Amber", "#ffb020"},  {"Yellow", "#ffe14d"}, {"Lime", "#b8f03c"},   {"Green", "#3ddc84"},   {"Mint", "#4dffc3"},
            {"Teal", "#2ec4b6"},   {"Cyan", "#22d3ee"},   {"Sky", "#5ab8ff"},    {"Blue", "#3b82f6"},   {"Indigo", "#6366f1"},  {"Violet", "#8b5cf6"},
            {"Purple", "#c084fc"}, {"Magenta", "#ff4fd8"}, {"Pink", "#ff6fa9"},  {"Red", "#ff4d4d"},    {"White", "#f2f2f2"},   {"Theme", "auto"},
        };

        std::unordered_map<std::string, double>    g_sessionStart;
        std::unordered_map<std::string, std::string> g_sessionStartText;

        // Hyprland cannot write a string setting in place, so a tuned color
        // lives here until the config itself changes the value (a reload
        // applying the tuning file, or an edit), which then takes over.
        struct STextOverride {
            std::string value;
            std::string base; // the config's value when it was tuned
        };
        std::unordered_map<std::string, STextOverride> g_textOverrides;
        std::unordered_map<std::string_view, const SParam*> g_index;

        std::string fullKey(const SParam& param) {
            return std::string{"plugin:spatialoverview:"} + param.key;
        }

        std::string lowerAscii(std::string value) {
            std::ranges::transform(value, value.begin(), [](unsigned char c) { return sc<char>(std::tolower(c)); });
            return value;
        }

        struct SRGB {
            double r = 0, g = 0, b = 0;
        };

        std::optional<SRGB> parseHex(std::string value) {
            std::erase(value, '#');
            if (value.starts_with("0x"))
                value = value.substr(2);
            if (value.size() != 6 && value.size() != 8)
                return std::nullopt;
            try {
                const auto PART = [&](size_t i) { return std::stoi(value.substr(i, 2), nullptr, 16) / 255.0; };
                return SRGB{PART(0), PART(2), PART(4)};
            } catch (...) { return std::nullopt; }
        }

        std::string hexOf(const SRGB& color) {
            const auto BYTE = [](double v) { return std::clamp(sc<int>(std::round(v * 255.0)), 0, 255); };
            return std::format("#{:02x}{:02x}{:02x}", BYTE(color.r), BYTE(color.g), BYTE(color.b));
        }

        // Rotates the hue, keeping lightness and saturation; a grey gets
        // enough color to show the rotation.
        SRGB rotateHue(const SRGB& color, double degrees) {
            const double MAX = std::max({color.r, color.g, color.b}), MIN = std::min({color.r, color.g, color.b});
            double       h = 0, sat = 0, l = (MAX + MIN) / 2.0;
            if (MAX - MIN > 1e-6) {
                const double D = MAX - MIN;
                sat            = l > 0.5 ? D / (2.0 - MAX - MIN) : D / (MAX + MIN);
                h              = MAX == color.r ? (color.g - color.b) / D + (color.g < color.b ? 6.0 : 0.0) : MAX == color.g ? (color.b - color.r) / D + 2.0 : (color.r - color.g) / D + 4.0;
                h /= 6.0;
            }
            if (sat < 0.1) {
                sat = 0.85;
                l   = std::clamp(l, 0.45, 0.65);
            }
            h = std::fmod(h + degrees / 360.0 + 1.0, 1.0);
            const auto CHANNEL = [](double p, double q, double t) {
                t = t < 0 ? t + 1 : t > 1 ? t - 1 : t;
                return t < 1.0 / 6.0 ? p + (q - p) * 6.0 * t : t < 0.5 ? q : t < 2.0 / 3.0 ? p + (q - p) * (2.0 / 3.0 - t) * 6.0 : p;
            };
            const double Q = l < 0.5 ? l * (1.0 + sat) : l + sat - l * sat, P = 2.0 * l - Q;
            return {CHANNEL(P, Q, h + 1.0 / 3.0), CHANNEL(P, Q, h), CHANNEL(P, Q, h - 1.0 / 3.0)};
        }

        int presetIndex(const std::string& value) {
            const auto LOWER = lowerAscii(value);
            for (size_t i = 0; i < PRESETS.size(); ++i) {
                if (LOWER == PRESETS[i].value || LOWER == lowerAscii(PRESETS[i].name))
                    return sc<int>(i);
            }
            return -1;
        }

        double clampToParam(const SParam& param, double value, double grid) {
            if (!std::isfinite(value))
                value = param.def;
            value = std::clamp(value, param.min, param.max);
            if (param.kind != FLOAT)
                return std::round(value);
            // Snap to a grid anchored at min, so ←/→ land on round values.
            const double STEPS = std::round((value - param.min) / grid);
            return std::clamp(param.min + STEPS * grid, param.min, param.max);
        }

        std::string luaValue(const SParam& param, double value) {
            switch (param.kind) {
                case BOOL: return value >= 0.5 ? "true" : "false";
                case INT: return std::format("{}", sc<long long>(std::llround(value)));
                default: {
                    auto text = std::format("{:.4f}", value);
                    while (text.ends_with('0'))
                        text.pop_back();
                    if (text.ends_with('.'))
                        text.pop_back();
                    return text == "-0" ? "0" : text;
                }
            }
        }

        // The tuning file is ours: one `["key"] = value,` line per entry.
        std::map<std::string, std::string> readFile(const std::string& path) {
            std::map<std::string, std::string> entries;
            std::ifstream                      in{path};
            static const std::regex            LINE{R"re(^\s*\["([a-z0-9_:]+)"\]\s*=\s*([^,\s]+)\s*,?\s*$)re"};
            std::string                        line;
            while (std::getline(in, line)) {
                std::smatch match;
                if (std::regex_match(line, match, LINE))
                    entries[match[1]] = match[2];
            }
            return entries;
        }

        void writeFile(const std::string& key, const std::string& value) {
            const auto PATH = filePath();
            if (PATH.empty())
                return;
            auto entries = readFile(PATH);
            entries[key] = value;

            std::ostringstream out;
            out << "-- Written by the Spatial Overview tuner (Ctrl+, in the zoomed-out canvas).\n"
                   "-- Applied after spatialoverview.lua, so these win. Delete a line to hand\n"
                   "-- that setting back to spatialoverview.lua.\n"
                   "return {\n";
            for (const auto& [name, text] : entries)
                out << std::format("  [\"{}\"] = {},\n", name, text);
            out << "}\n";

            std::error_code error;
            std::filesystem::create_directories(std::filesystem::path{PATH}.parent_path(), error);
            const auto TEMP = PATH + ".tmp";
            {
                std::ofstream file{TEMP, std::ios::trunc};
                if (!file)
                    return;
                file << out.str();
                if (!file.good())
                    return;
            }
            std::filesystem::rename(TEMP, PATH, error);
        }

        void apply(const SParam& param, double value) {
            const auto KEY = fullKey(param);
            switch (param.kind) {
                case BOOL: ScrollOverview::Config::setValue(KEY, value >= 0.5); break;
                case INT: ScrollOverview::Config::setValue(KEY, sc<int>(std::llround(value))); break;
                default: ScrollOverview::Config::setValue(KEY, sc<float>(value)); break;
            }
        }
    }

    const std::vector<SParam>& params() {
        return PARAMS;
    }

    const SParam* find(std::string_view key) {
        if (g_index.empty()) {
            for (const auto& param : PARAMS)
                g_index.emplace(param.key, &param);
        }
        const auto IT = g_index.find(key);
        return IT == g_index.end() ? nullptr : IT->second;
    }

    void registerConfig() {
        using namespace ::Config::Values;
        for (const auto& param : PARAMS) {
            if (!param.own)
                continue;
            const auto KEY = fullKey(param);
            switch (param.kind) {
                case BOOL: HyprlandAPI::addConfigValueV2(SCROLLOVERVIEW_HANDLE, makeShared<CBoolValue>(KEY.c_str(), param.description, param.def >= 0.5)); break;
                case INT:
                    HyprlandAPI::addConfigValueV2(SCROLLOVERVIEW_HANDLE,
                                                  makeShared<CIntValue>(KEY.c_str(), param.description, sc<Hyprlang::INT>(param.def),
                                                                        SIntValueOptions{.min = sc<Hyprlang::INT>(param.min), .max = sc<Hyprlang::INT>(param.max)}));
                    break;
                default:
                    HyprlandAPI::addConfigValueV2(SCROLLOVERVIEW_HANDLE,
                                                  makeShared<CFloatValue>(KEY.c_str(), param.description, sc<float>(param.def),
                                                                          SFloatValueOptions{.min = sc<float>(param.min), .max = sc<float>(param.max)}));
                    break;
            }
        }
        HyprlandAPI::addConfigValueV2(SCROLLOVERVIEW_HANDLE,
                                      makeShared<CStringValue>("plugin:spatialoverview:navigator:search_border_color",
                                                               "search field border and glow color: accent, or #rrggbb", Hyprlang::STRING{"accent"}));
        HyprlandAPI::addConfigValueV2(SCROLLOVERVIEW_HANDLE,
                                      makeShared<CStringValue>("plugin:spatialoverview:navigator:tuning_file",
                                                               "where the live tuner saves; empty for $XDG_CONFIG_HOME/hypr/spatialoverview-tuning.lua",
                                                               Hyprlang::STRING{""}));
    }

    double get(const SParam& param) {
        const auto KEY = fullKey(param);
        double     value;
        switch (param.kind) {
            case COLOR: return 0.0;
            case BOOL: value = ScrollOverview::Config::getValue<bool>(KEY) ? 1.0 : 0.0; break;
            case INT: value = ScrollOverview::Config::getValue<int>(KEY); break;
            default: value = ScrollOverview::Config::getValue<float>(KEY); break;
        }
        return std::isfinite(value) ? std::clamp(value, param.min, param.max) : param.def;
    }

    double number(std::string_view key) {
        const auto* PARAM = find(key);
        return PARAM ? get(*PARAM) : 0.0;
    }

    bool flag(std::string_view key) {
        return number(key) >= 0.5;
    }

    namespace {
        bool assignText(const SParam& param, const std::string& next) {
            const auto PREVIOUS = text(param);
            if (next == PREVIOUS)
                return false;
            g_sessionStartText.try_emplace(param.key, PREVIOUS);
            g_textOverrides[param.key] = {.value = next, .base = ScrollOverview::Config::getValue<std::string>(fullKey(param))};
            writeFile(param.key, "\"" + next + "\"");
            return true;
        }

        bool assign(const SParam& param, double next) {
            const double PREVIOUS = get(param);
            if (std::abs(next - PREVIOUS) < 1e-9)
                return false;
            g_sessionStart.try_emplace(param.key, PREVIOUS);
            apply(param, next);
            writeFile(param.key, luaValue(param, next));
            return true;
        }
    }

    bool set(const SParam& param, double value) {
        return assign(param, clampToParam(param, value, param.step));
    }

    bool step(const SParam& param, double steps) {
        if (param.kind == COLOR) {
            const auto CURRENT = text(param);
            if (std::abs(steps) >= 1.0) {
                // Whole steps walk the presets; a custom color starts from Orange.
                const int COUNT = sc<int>(PRESETS.size());
                int       index = presetIndex(CURRENT);
                index           = index < 0 ? (steps > 0 ? 0 : COUNT - 1) : ((index + (steps > 0 ? 1 : -1)) % COUNT + COUNT) % COUNT;
                return assignText(param, PRESETS[index].value);
            }
            // Fine steps turn the hue, 5° at a time.
            auto base = parseHex(CURRENT);
            if (!base) {
                const auto PRESET = presetValue(CURRENT);
                base              = PRESET ? parseHex(*PRESET) : std::nullopt;
            }
            return assignText(param, hexOf(rotateHue(base.value_or(SRGB{1.0, 0.42, 0.1}), steps * 50.0)));
        }
        if (param.kind == BOOL)
            return set(param, steps > 0 ? 1.0 : 0.0);
        if (!param.choices.empty()) {
            // Choices wrap around, like a toggle.
            const double COUNT = param.max - param.min + 1.0;
            double       next  = get(param) - param.min + (steps > 0 ? 1.0 : -1.0);
            next               = std::fmod(std::fmod(next, COUNT) + COUNT, COUNT);
            return set(param, param.min + next);
        }
        // Fine steps (|steps| < 1) snap to a finer grid instead of back.
        const double GRID = param.step * std::min(1.0, std::abs(steps));
        return assign(param, clampToParam(param, get(param) + steps * param.step, GRID));
    }

    bool revert(const SParam& param) {
        if (param.kind == COLOR) {
            const auto IT = g_sessionStartText.find(param.key);
            return IT != g_sessionStartText.end() && assignText(param, IT->second);
        }
        const auto IT = g_sessionStart.find(param.key);
        return IT != g_sessionStart.end() && assign(param, std::clamp(IT->second, param.min, param.max));
    }

    void beginSession() {
        g_sessionStart.clear();
        g_sessionStartText.clear();
    }

    std::string format(const SParam& param, double value) {
        if (param.kind == COLOR) {
            const auto CURRENT = text(param);
            const int  INDEX   = presetIndex(CURRENT);
            return INDEX >= 0 ? PRESETS[INDEX].name : CURRENT;
        }
        if (param.kind == BOOL)
            return value >= 0.5 ? "On" : "Off";
        if (!param.choices.empty()) {
            const auto INDEX = sc<size_t>(std::clamp(std::llround(value - param.min), 0LL, sc<long long>(param.choices.size()) - 1));
            return param.choices[INDEX];
        }
        const std::string_view UNIT = param.unit;
        if (UNIT == "%")
            return std::format("{}%", sc<long long>(std::llround(value * 100.0)));
        // Enough decimals to show one step.
        const int DECIMALS = param.kind == INT ? 0 : std::clamp(sc<int>(std::ceil(-std::log10(param.step) - 1e-9)), 0, 3);
        auto      text     = std::format("{:.{}f}", value, DECIMALS);
        if (UNIT == "×")
            return text + "×";
        return UNIT.empty() ? text : text + " " + std::string{UNIT};
    }

    double fraction(const SParam& param, double value) {
        return param.max > param.min ? std::clamp((value - param.min) / (param.max - param.min), 0.0, 1.0) : 0.0;
    }

    std::string text(const SParam& param) {
        const auto CONFIGURED = ScrollOverview::Config::getValue<std::string>(fullKey(param));
        if (const auto IT = g_textOverrides.find(param.key); IT != g_textOverrides.end()) {
            if (IT->second.base == CONFIGURED)
                return IT->second.value;
            g_textOverrides.erase(IT);
        }
        return CONFIGURED;
    }

    std::optional<std::string> presetValue(std::string_view name) {
        const int INDEX = presetIndex(std::string{name});
        return INDEX >= 0 ? std::optional<std::string>{PRESETS[INDEX].value} : std::nullopt;
    }

    std::string filePath() {
        auto configured = ScrollOverview::Config::getValue<std::string>("plugin:spatialoverview:navigator:tuning_file");
        if (!configured.empty())
            return configured;
        std::filesystem::path base;
        if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && *config)
            base = config;
        else if (const char* home = std::getenv("HOME"); home && *home)
            base = std::filesystem::path{home} / ".config";
        else
            return {};
        return (base / "hypr" / "spatialoverview-tuning.lua").string();
    }
}
