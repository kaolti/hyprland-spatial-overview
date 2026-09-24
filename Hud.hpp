#pragma once

#include "globals.hpp"
#include "Navigator.hpp"

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/math/Math.hpp>

#include <array>
#include <string>
#include <vector>

namespace Render {
    class ITexture;
}

// Canvas HUD drawn with cairo/pango: monospace, uppercase, letter-spaced, a
// dark glass container of card rows with one solid accent row for the
// selection. The palette and the screen-corner readouts are composed after
// the lens in the final screen pass, so they stay flat and sharp; window
// labels live in the world and are drawn under the lens with their windows.
namespace SpatialOverview::Hud {

    struct STheme {
        CHyprColor  accent;    // the selected row, the caret, key names
        CHyprColor  onAccent;  // text on the accent
        CHyprColor  panel;     // the container
        CHyprColor  card;      // rows
        CHyprColor  edge;      // hairlines
        CHyprColor  text;
        CHyprColor  textDim;
        CHyprColor  textFaint;
        std::string font;

        // Layout and type, from the navigator:* settings (see Tuning.cpp).
        // Lengths are logical pixels before hudScale.
        double      hudScale = 1.0, width = 720.0, top = 0.075, rowH = 58.0, searchH = 60.0, rounding = 11.0, tracking = 1.0;
        int         rows      = 6;
        bool        uppercase = true, corners = true;
        double      querySize = 17.0, titleSize = 14.0, detailSize = 10.5, cornerSize = 12.0, labelSize = 10.5;
        double      searchBorder = 1.5, searchGlow = 0.3;
        CHyprColor  searchEdge; // border and glow color; alpha is the border opacity
        std::string signature;  // all of the above, for texture cache keys
    };

    const STheme& theme();
    void          refreshTheme();

    struct SPaletteView {
        Vector2D    monitorSize; // logical
        float       scale = 1.F; // monitor scale
        float       zoom  = 1.F;
        Vector2D    origin;      // world point the session started from, for distance hints
        std::string monitorName;
        std::string experiment;  // non-empty while a lab experiment is selected
    };

    // Renders (or reuses) the palette texture. The box is in monitor device
    // pixels. Returns false when there is nothing to show.
    bool palette(const SPaletteView& view, SP<Render::ITexture>& texture, CBox& box);

    // Readouts pinned to the screen corners, packed into one atlas texture.
    struct SChromeRegion {
        CBox                 screen; // monitor device pixels
        std::array<float, 4> uv{};   // x, y, w, h in the atlas, 0..1
    };
    struct SChrome {
        SP<Render::ITexture>       texture;
        std::vector<SChromeRegion> regions;
    };
    bool chrome(const SPaletteView& view, SChrome& out);

    // Where the selected window should sit (logical y from the monitor top)
    // so the palette, even with a full result list, never covers it.
    double selectionFocusY(const Vector2D& monitorSize);

    // Hit test against the last palette drawn, in monitor device pixels:
    // a result index, PALETTE_PANEL for the rest of the palette, or
    // PALETTE_MISS outside it.
    constexpr int PALETTE_PANEL = -1;
    constexpr int PALETTE_MISS  = -2;
    int           paletteHit(const Vector2D& point, PHLWINDOW* window = nullptr);

    struct SLabel {
        SP<Render::ITexture> texture;
        Vector2D             size;
    };
    SLabel label(const PHLWINDOW& window, bool selected, float scale, float maxWidthPx);

    // One repeating tile of the canvas grid: a 2x2 block of cells whose
    // top-left mark belongs to the coarser level and whose other three marks
    // fade with `fine` (0..1), so zooming across levels never pops. `style`
    // 1 draws dots of `mark` diameter, 0 draws lines of `mark` width. Sizes
    // are in device pixels; the texture is cached until they change.
    SP<Render::ITexture> gridTile(double cell, double mark, double fine, int style);

    void                 endFrame();
    void                 shutdown();
}
