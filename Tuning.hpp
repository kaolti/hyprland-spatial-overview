#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Everything the live tuner can change, in display order. The table also
// registers the config keys that are new with it, and is where their ranges
// live, so the tuner, the config and the renderers agree on limits.
//
// The tuner writes what it changes to a small Lua file that the user's
// spatialoverview.lua applies after its own settings (see filePath()), so a
// value set live survives `hyprctl reload` and restarts.
namespace SpatialOverview::Tuning {

    enum class EKind : unsigned char {
        FLOAT,
        INT,
        BOOL,
        COLOR, // a string: #rrggbb, a preset name, or auto
    };

    struct SParam {
        const char*              key;         // below plugin:spatialoverview:, e.g. "distortion:strength"
        const char*              section;     // group heading in the tuner
        const char*              label;
        const char*              description; // for keys registered here
        EKind                    kind;
        double                   def, min, max, step;
        const char*              unit    = ""; // "px", "×", or "%" (shown as value × 100)
        bool                     own     = true; // false: an older key registered in Config.cpp
        std::vector<const char*> choices = {};   // INT keys shown by name
    };

    const std::vector<SParam>& params();
    const SParam*              find(std::string_view key);

    // Registers the keys this table owns. Called with the rest of the config.
    void                       registerConfig();

    // The live value, clamped to the table's range.
    double                     get(const SParam& param);
    double                     number(std::string_view key);
    bool                       flag(std::string_view key);

    // Sets a value live (clamped and snapped to its step) and writes it to the
    // tuning file. Returns false when the value did not change.
    bool                       set(const SParam& param, double value);
    bool                       step(const SParam& param, double steps);
    // Back to the value the parameter had when the tuner was opened.
    bool                       revert(const SParam& param);
    void                       beginSession();

    // "0.24", "46 px", "35%", "On", "Dots"; the HUD applies its own case.
    std::string                format(const SParam& param, double value);
    // Position of the value in its range, 0..1, for the slider.
    double                     fraction(const SParam& param, double value);

    // Color settings: the configured text, and what a preset name stands
    // for ("cyan" -> "#22d3ee", "theme" -> "auto").
    std::string                text(const SParam& param);
    std::optional<std::string> presetValue(std::string_view name);

    std::string                filePath();
}
