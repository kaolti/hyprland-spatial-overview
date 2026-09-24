#define WLR_USE_UNSTABLE

#include "Hud.hpp"

#include <algorithm>
#include <cairo/cairo.h>
#include <cmath>
#include <format>
#include <glib.h>
#include <limits>
#include <optional>
#include <pango/pangocairo.h>
#include <unordered_map>

#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include "Config.hpp"
#include "Icons.hpp"
#include "Tuning.hpp"

namespace SpatialOverview::Hud {
    namespace {
        STheme   g_theme;
        uint64_t g_themeFrame = std::numeric_limits<uint64_t>::max();
        uint64_t g_frame      = 0;

        struct SCachedLabel {
            SLabel   label;
            uint64_t lastUsed = 0;
        };
        std::unordered_map<std::string, SCachedLabel> g_labels;
        std::unordered_map<std::string, double>       g_labelWidths; // measuring is the per-frame cost of a label

        struct SCachedPalette {
            std::string          key;
            SP<Render::ITexture> texture;
            CBox                 box;
            struct SRow {
                CBox         box; // texture-local
                int          index = 0;
                PHLWINDOWREF window;
            };
            std::vector<CBox> panels; // texture-local
            std::vector<SRow> rows;
        } g_palette;

        struct SCachedChrome {
            std::string key;
            SChrome     chrome;
        } g_chrome;

        SP<Render::ITexture> g_gridTile;
        std::string          g_gridTileKey;

        cairo_surface_t*     g_measureSurface = nullptr;
        cairo_t*             g_measure        = nullptr;

        // ---- colors ---------------------------------------------------------

        CHyprColor mix(const CHyprColor& a, const CHyprColor& b, float t) {
            return CHyprColor{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
        }

        CHyprColor withAlpha(CHyprColor color, float alpha) {
            color.a = alpha;
            return color;
        }

        std::optional<CHyprColor> parseHex(std::string value) {
            value.erase(std::remove_if(value.begin(), value.end(), [](char c) { return c == '#' || c == ' '; }), value.end());
            if (value.starts_with("0x"))
                value = value.substr(2);
            if (value.size() != 6 && value.size() != 8)
                return std::nullopt;
            try {
                const auto PART = [&](size_t i) { return std::stoi(value.substr(i, 2), nullptr, 16) / 255.F; };
                return CHyprColor{PART(0), PART(2), PART(4), value.size() == 8 ? PART(6) : 1.F};
            } catch (...) { return std::nullopt; }
        }

        std::string hex(const CHyprColor& color) {
            const auto BYTE = [](float v) { return std::clamp(sc<int>(std::round(v * 255.F)), 0, 255); };
            return std::format("#{:02x}{:02x}{:02x}", BYTE(color.r), BYTE(color.g), BYTE(color.b));
        }

        // Theme borders are often muted; "auto" needs a luminous version of
        // the same hue to hold a row of dark text.
        CHyprColor luminous(const CHyprColor& color) {
            const float MAX = std::max({color.r, color.g, color.b});
            const float MIN = std::min({color.r, color.g, color.b});
            float       h = 0.F, s = 0.F, l = (MAX + MIN) / 2.F;
            if (MAX - MIN > 0.0001F) {
                const float D = MAX - MIN;
                s             = l > 0.5F ? D / (2.F - MAX - MIN) : D / (MAX + MIN);
                if (MAX == color.r)
                    h = (color.g - color.b) / D + (color.g < color.b ? 6.F : 0.F);
                else if (MAX == color.g)
                    h = (color.b - color.r) / D + 2.F;
                else
                    h = (color.r - color.g) / D + 4.F;
                h /= 6.F;
            }
            if (s < 0.12F)
                return CHyprColor{1.F, 0.42F, 0.1F, 1.F};
            s = std::max(s, 0.62F);
            l = std::clamp(l, 0.55F, 0.64F);

            const auto HUE = [](float p, float q, float t) {
                t = t < 0.F ? t + 1.F : t > 1.F ? t - 1.F : t;
                if (t < 1.F / 6.F)
                    return p + (q - p) * 6.F * t;
                if (t < 0.5F)
                    return q;
                if (t < 2.F / 3.F)
                    return p + (q - p) * (2.F / 3.F - t) * 6.F;
                return p;
            };
            const float Q = l < 0.5F ? l * (1.F + s) : l + s - l * s;
            const float P = 2.F * l - Q;
            return CHyprColor{HUE(P, Q, h + 1.F / 3.F), HUE(P, Q, h), HUE(P, Q, h - 1.F / 3.F), 1.F};
        }

        void setColor(cairo_t* cr, const CHyprColor& color, float alphaScale = 1.F) {
            cairo_set_source_rgba(cr, color.r, color.g, color.b, std::clamp(color.a * alphaScale, 0.0, 1.0));
        }

        // ---- shapes -----------------------------------------------------------

        void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
            r = std::max(0.0, std::min({r, w / 2.0, h / 2.0}));
            cairo_new_sub_path(cr);
            cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
            cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
            cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
            cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
            cairo_close_path(cr);
        }

        // A soft drop shadow from layered strokes, to lift the container off
        // the canvas without a blur pass.
        void shadow(cairo_t* cr, double x, double y, double w, double h, double r, double spread, double strength = 1.0, double drop = 0.25) {
            constexpr int LAYERS = 10;
            for (int i = LAYERS; i >= 1; --i) {
                const double T = sc<double>(i) / LAYERS;
                const double E = spread * T;
                roundedRect(cr, x - E, y - E + spread * drop, w + E * 2.0, h + E * 2.0, r + E);
                cairo_set_source_rgba(cr, 0, 0, 0, std::min(1.0, 0.16 * strength * std::pow(1.0 - T, 1.6)));
                cairo_set_line_width(cr, spread / LAYERS * 2.2);
                cairo_stroke(cr);
            }
        }

        // Rows read as smoky glass: a little lighter toward the top left.
        void card(cairo_t* cr, double x, double y, double w, double h, double r, const STheme& theme) {
            roundedRect(cr, x, y, w, h, r);
            cairo_pattern_t* fill = cairo_pattern_create_linear(x, y, x + w, y + h);
            const auto       LEFT = mix(theme.card, CHyprColor{1, 1, 1, theme.card.a}, 0.045F);
            cairo_pattern_add_color_stop_rgba(fill, 0, LEFT.r, LEFT.g, LEFT.b, LEFT.a);
            cairo_pattern_add_color_stop_rgba(fill, 1, theme.card.r, theme.card.g, theme.card.b, theme.card.a);
            cairo_set_source(cr, fill);
            cairo_fill_preserve(cr);
            cairo_pattern_destroy(fill);
            setColor(cr, theme.edge);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        // ---- text -------------------------------------------------------------

        struct STextStyle {
            double      px      = 14.0;
            PangoWeight weight  = PANGO_WEIGHT_NORMAL;
            double      spacing = 0.0; // letter spacing, px
        };

        PangoLayout* layoutFor(cairo_t* cr, const STextStyle& style, const std::string& markup, double maxWidth = -1,
                               PangoEllipsizeMode ellipsize = PANGO_ELLIPSIZE_END) {
            PangoLayout*          layout = pango_cairo_create_layout(cr);
            PangoFontDescription* desc   = pango_font_description_from_string(g_theme.font.c_str());
            pango_font_description_set_absolute_size(desc, style.px * PANGO_SCALE);
            pango_font_description_set_weight(desc, style.weight);
            pango_layout_set_font_description(layout, desc);
            pango_font_description_free(desc);
            pango_layout_set_single_paragraph_mode(layout, true);
            // pango measures letter spacing in 1/1024 pt; cairo runs at 96 dpi.
            const double SPACING = style.spacing * g_theme.tracking;
            const auto   SPACED  = SPACING > 0 ? std::format("<span letter_spacing=\"{}\">{}</span>", sc<int>(SPACING * 768.0), markup) : markup;
            pango_layout_set_markup(layout, SPACED.c_str(), -1);
            if (maxWidth > 0) {
                pango_layout_set_width(layout, sc<int>(maxWidth * PANGO_SCALE));
                pango_layout_set_ellipsize(layout, ellipsize);
            }
            return layout;
        }

        Vector2D layoutSize(PangoLayout* layout) {
            PangoRectangle logical;
            pango_layout_get_pixel_extents(layout, nullptr, &logical);
            return {sc<double>(logical.width), sc<double>(logical.height)};
        }

        // Draws markup vertically centered on cy. Returns its advance width.
        double drawText(cairo_t* cr, const STextStyle& style, const std::string& markup, double x, double cy, const CHyprColor& color, double maxWidth = -1,
                        PangoEllipsizeMode ellipsize = PANGO_ELLIPSIZE_END) {
            if (markup.empty())
                return 0.0;
            PangoLayout* layout = layoutFor(cr, style, markup, maxWidth, ellipsize);
            const auto   SIZE   = layoutSize(layout);
            setColor(cr, color);
            cairo_move_to(cr, x, cy - SIZE.y / 2.0);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
            return SIZE.x;
        }

        double measureText(const STextStyle& style, const std::string& markup) {
            if (!g_measure) {
                g_measureSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
                g_measure        = cairo_create(g_measureSurface);
            }
            PangoLayout* layout = layoutFor(g_measure, style, markup);
            const double WIDTH  = layoutSize(layout).x;
            g_object_unref(layout);
            return WIDTH;
        }

        std::string escape(const std::string& text) {
            gchar*      escaped = g_markup_escape_text(text.c_str(), sc<gssize>(text.size()));
            std::string result  = escaped ? escaped : "";
            g_free(escaped);
            return result;
        }

        std::string validUtf8(const std::string& text) {
            if (g_utf8_validate(text.c_str(), sc<gssize>(text.size()), nullptr))
                return text;
            gchar*      valid  = g_utf8_make_valid(text.c_str(), sc<gssize>(text.size()));
            std::string result = valid ? valid : "";
            g_free(valid);
            return result;
        }

        // Uppercase one codepoint at a time, so match positions (codepoint
        // indices into the original text) still line up.
        std::string upper(const std::string& rawText) {
            const auto  TEXT = validUtf8(rawText);
            std::string out;
            out.reserve(TEXT.size());
            for (const char* p = TEXT.c_str(); *p; p = g_utf8_next_char(p)) {
                char      buffer[8];
                const int LENGTH = g_unichar_to_utf8(g_unichar_toupper(g_utf8_get_char(p)), buffer);
                out.append(buffer, LENGTH);
            }
            return out;
        }

        // The HUD's case: capitals, or the text as written.
        std::string caseText(const std::string& text) {
            return g_theme.uppercase ? upper(text) : validUtf8(text);
        }

        std::string caseMarkup(const std::string& text) {
            return escape(caseText(text));
        }

        // Fixed interface text, written in sentence case and set in the
        // HUD's case.
        std::string ui(const std::string& text) {
            return caseMarkup(text);
        }

        // Uppercase markup with matched characters picked out, either in a
        // color or (on the accent row) underlined.
        std::string matchedMarkup(const std::string& rawText, const std::vector<size_t>& matches, const std::optional<CHyprColor>& color) {
            const auto  TEXT = caseText(rawText);
            const auto  OPEN = color ? "<span foreground=\"" + hex(*color) + "\" weight=\"bold\">" : std::string{"<span underline=\"single\" weight=\"bold\">"};
            std::string out;
            size_t      index = 0;
            size_t      next  = 0;
            bool        inHit = false;
            for (const char* p = TEXT.c_str(); *p; p = g_utf8_next_char(p), ++index) {
                const char* END = g_utf8_next_char(p);
                const bool  HIT = next < matches.size() && matches[next] == index;
                if (HIT)
                    ++next;
                if (HIT && !inHit)
                    out += OPEN;
                if (!HIT && inHit)
                    out += "</span>";
                inHit = HIT;
                out += escape(std::string{p, sc<size_t>(END - p)});
            }
            if (inHit)
                out += "</span>";
            return out;
        }

        SP<Render::ITexture> upload(cairo_surface_t* surface) {
            cairo_surface_flush(surface);
            if (!g_pHyprRenderer)
                return nullptr;
            auto texture = g_pHyprRenderer->createTexture(surface);
            return texture && texture->ok() ? texture : nullptr;
        }

        // ---- pieces -------------------------------------------------------------

        void drawIcon(cairo_t* cr, cairo_surface_t* icon, double x, double y, double size) {
            const int W = cairo_image_surface_get_width(icon);
            const int H = cairo_image_surface_get_height(icon);
            if (W <= 0 || H <= 0)
                return;
            const double FIT = size / std::max(W, H);
            cairo_save(cr);
            cairo_translate(cr, x + (size - W * FIT) / 2.0, y + (size - H * FIT) / 2.0);
            cairo_scale(cr, FIT, FIT);
            cairo_set_source_surface(cr, icon, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            cairo_restore(cr);
        }

        // An app icon in a rounded well, or its category code when the app
        // ships no icon.
        void iconWell(cairo_t* cr, const PHLWINDOW& window, double x, double y, double size, double s, bool selected, const STheme& theme) {
            roundedRect(cr, x, y, size, size, 8.0 * s);
            if (selected)
                cairo_set_source_rgba(cr, 0.03, 0.03, 0.03, 0.9);
            else
                cairo_set_source_rgba(cr, 1, 1, 1, 0.045);
            cairo_fill(cr);

            const double INNER = std::round(size * 0.68);
            if (auto* icon = Icons::forWindow(window, sc<int>(INNER))) {
                drawIcon(cr, icon, x + (size - INNER) / 2.0, y + (size - INNER) / 2.0, INNER);
                return;
            }
            const STextStyle CODE{.px = 11.0 * s, .weight = PANGO_WEIGHT_BOLD, .spacing = 0.5 * s};
            const auto       TEXT = escape(std::string{Navigator::categoryCode(canvasWindowCategory(window))});
            drawText(cr, CODE, TEXT, x + (size - measureText(CODE, TEXT)) / 2.0, y + size / 2.0, selected ? theme.accent : theme.textDim);
        }

        std::string directionHint(const PHLWINDOW& window, const SPaletteView& view) {
            if (!window || view.monitorSize.x <= 0 || view.monitorSize.y <= 0)
                return {};
            const auto   CENTER = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT).middle();
            const double DX     = (CENTER.x - view.origin.x) / view.monitorSize.x;
            const double DY     = (CENTER.y - view.origin.y) / view.monitorSize.y;
            const double DIST   = std::hypot(DX, DY);
            if (DIST < 0.55)
                return "In view";
            static constexpr const char* ARROWS[] = {"→", "↘", "↓", "↙", "←", "↖", "↑", "↗"};
            const double ANGLE = std::atan2(DY, DX);
            const int    INDEX = (sc<int>(std::round(ANGLE / (M_PI / 4.0))) + 8) % 8;
            return std::format("{} {:.1f} screens", ARROWS[INDEX], DIST);
        }

        struct SHelpRow {
            const char* keys;
            const char* action;
        };

        struct SHelpSection {
            const char*           title;
            std::vector<SHelpRow> rows;
        };

        const std::vector<SHelpSection>& helpLeft() {
            static const SHelpSection FIND{"Find", {{"Type", "Search"}, {"↑ ↓", "Move in results"}, {"⇥ ⇧⇥", "Next / prev"}, {"⌃1-9", "Jump to result"},
                                                   {"↵", "Go to window"}, {"⇧↵", "Bring it here"}, {"Esc", "Clear, then back"}}};
            static const SHelpSection PLACES{"Places", {{"Super 1-0", "Go to a place"}, {"Super ⇥ ⇧⇥", "Next / prev place"},
                                                       {"Super ⇧ 1-0", "Take window along"}, {"Super ⇧ Alt 1-0", "Send window there"}}};
            static const SHelpSection MOUSE{"Mouse", {{"Click", "Go to window"}, {"Drag", "Move or pan"}, {"Wheel", "Zoom"}, {"Minimap", "Jump"}}};
            static const std::vector<SHelpSection> WITHPLACES{FIND, PLACES, MOUSE};
            static const std::vector<SHelpSection> WITHOUT{FIND, MOUSE};
            return ScrollOverview::Config::getCanvasPlaces() ? WITHPLACES : WITHOUT;
        }

        const std::vector<SHelpSection>& helpRight() {
            static const std::vector<SHelpSection> SECTIONS{
                {"View", {{"←→↑↓", "Nearest that way"}, {"⌃ ←→↑↓", "Pan"}, {"⌃+ ⌃−", "Zoom"}, {"⌃0", "Fit everything"}, {"⌃F", "Frame selection"}}},
                {"Arrange", {{"⇧ ←→↑↓", "Nudge window"}, {"⌃A", "Tidy all"}, {"⌃Z ⌃⇧Z", "Undo / redo"}, {"Super W", "Close window"}}},
                {"Anywhere", {{"Alt ⇥", "Recent windows"}, {"Super ⇧ ←→", "Move window"}, {"⌃,", "Tune the look"}}},
            };
            return SECTIONS;
        }

        // Help is set relative to the title size.
        double helpUnit() {
            return g_theme.titleSize / 14.0;
        }

        double helpHeight(double s) {
            const double U      = helpUnit();
            const auto   COLUMN = [U](const std::vector<SHelpSection>& sections) {
                double h = 0;
                for (const auto& section : sections)
                    h += 30.0 * U + section.rows.size() * 24.0 * U + 10.0;
                return h;
            };
            return (std::max(COLUMN(helpLeft()), COLUMN(helpRight())) + 28.0) * s;
        }

        void drawHelp(cairo_t* cr, double x, double y, double w, double h, double s, const STheme& theme) {
            card(cr, x, y, w, h, theme.rounding * s, theme);

            const double     U = helpUnit();
            const STextStyle HEADER{.px = 11.0 * U * s, .weight = PANGO_WEIGHT_BOLD, .spacing = 2.4 * s};
            const STextStyle KEY{.px = 12.0 * U * s, .weight = PANGO_WEIGHT_MEDIUM, .spacing = 0.8 * s};
            const STextStyle ACTION{.px = 11.0 * U * s, .weight = PANGO_WEIGHT_NORMAL, .spacing = 1.2 * s};

            const auto COLUMN = [&](const std::vector<SHelpSection>& sections, double cx, double width) {
                double keyWidth = 64.0 * U * s;
                for (const auto& section : sections) {
                    for (const auto& row : section.rows)
                        keyWidth = std::max(keyWidth, measureText(KEY, ui(row.keys)));
                }
                double cy = y + 26.0 * U * s;
                for (const auto& section : sections) {
                    drawText(cr, HEADER, ui(section.title), cx, cy, theme.accent);
                    cy += 30.0 * U * s;
                    for (const auto& row : section.rows) {
                        drawText(cr, KEY, ui(row.keys), cx, cy, theme.text);
                        drawText(cr, ACTION, ui(row.action), cx + keyWidth + 18.0 * s, cy, theme.textDim, width - keyWidth - 24.0 * s);
                        cy += 24.0 * U * s;
                    }
                    cy += 10.0 * s;
                }
            };
            COLUMN(helpLeft(), x + 22.0 * s, w / 2.0 - 30.0 * s);
            COLUMN(helpRight(), x + w / 2.0 + 8.0 * s, w / 2.0 - 30.0 * s);
        }

        constexpr double GAP    = 6.0;  // between cards

        // A footer line under the rows ("+3 more", the tuner's description):
        // one row gap above its text, nothing below it but the padding.
        double footerHeight(const STheme& theme) {
            return GAP + std::ceil(theme.detailSize * 1.3);
        }
        constexpr double TUNER_ROW_H = 40.0;

        std::string paletteKey(const SPaletteView& view, const Navigator::SState& state) {
            std::string key = std::format("{}x{}@{:.3f}|{}|{}|{}|{}|{}|{}|{}|{}", view.monitorSize.x, view.monitorSize.y, view.scale, state.query, state.selected, state.listOffset,
                                          Navigator::listVisible(), state.helpOpen, state.caretVisible, state.switcher, g_theme.signature);
            if (state.tuner) {
                key += std::format("|tune|{}|{}|{}|n{}", state.tunerQuery, state.tunerSelected, state.tunerOffset, state.tunerResults.size());
                const size_t END = std::min(state.tunerResults.size(), sc<size_t>(state.tunerOffset + Navigator::TUNER_ROWS));
                for (size_t i = state.tunerOffset; i < END; ++i) {
                    const auto& PARAM = Tuning::params()[state.tunerResults[i]];
                    key += "|" + Tuning::format(PARAM, Tuning::get(PARAM));
                }
                return key;
            }
            if (Navigator::listVisible() && !state.helpOpen) {
                const size_t END = std::min(state.results.size(), sc<size_t>(state.listOffset + g_theme.rows));
                for (size_t i = state.listOffset; i < END; ++i) {
                    const auto WINDOW = state.results[i].window.lock();
                    if (!WINDOW)
                        continue;
                    key += std::format("|{}:{}:{}", WINDOW->m_stableID, Navigator::displayTitle(WINDOW), directionHint(WINDOW, view));
                }
                key += std::format("|n{}", state.results.size());
            }
            return key;
        }

        // The search field: a card with an optional glow and border, the
        // prompt, the query (or a placeholder), a block cursor and [ Esc ].
        void searchField(cairo_t* cr, double x, double y, double w, double h, double s, const STheme& theme, const std::string& queryMarkup,
                         const std::string& placeholder, bool caret) {
            const double R = theme.rounding * s;
            if (theme.searchGlow > 0.001) {
                constexpr int LAYERS = 12;
                const double  SPREAD = 14.0 * s;
                for (int i = LAYERS; i >= 1; --i) {
                    const double T = sc<double>(i) / LAYERS;
                    const double E = SPREAD * T;
                    roundedRect(cr, x - E, y - E, w + E * 2.0, h + E * 2.0, R + E);
                    setColor(cr, withAlpha(theme.searchEdge, 1.F), theme.searchGlow * 0.2 * std::pow(1.0 - T, 2.0));
                    cairo_set_line_width(cr, SPREAD / LAYERS * 2.2);
                    cairo_stroke(cr);
                }
            }
            card(cr, x, y, w, h, R, theme);
            if (theme.searchBorder > 0.001 && theme.searchEdge.a > 0.001) {
                const double B = theme.searchBorder * s;
                roundedRect(cr, x + B / 2.0, y + B / 2.0, w - B, h - B, std::max(0.0, R - B / 2.0));
                setColor(cr, theme.searchEdge);
                cairo_set_line_width(cr, B);
                cairo_stroke(cr);
            }

            const double     CY = y + h / 2.0;
            const STextStyle PROMPT{.px = theme.querySize * s, .weight = PANGO_WEIGHT_BOLD, .spacing = 0.5 * s};
            const STextStyle QUERY{.px = theme.querySize * s, .weight = PANGO_WEIGHT_MEDIUM, .spacing = 1.8 * s};
            const STextStyle SMALL{.px = (theme.detailSize + 1.0) * s, .weight = PANGO_WEIGHT_NORMAL, .spacing = 1.6 * s};

            const double PROMPTW = drawText(cr, PROMPT, "&gt;_", x + 20.0 * s, CY, theme.accent);
            const double TEXTX   = x + 20.0 * s + PROMPTW + 14.0 * s;
            const auto   ESC     = ui("[ Esc ]");
            const double ESCW    = measureText(SMALL, ESC);
            const double TEXTMAX = x + w - TEXTX - ESCW - 40.0 * s;
            const double CARETW  = theme.querySize * 0.53 * s;
            const double CARETH  = theme.querySize * 1.3 * s;

            double caretX = TEXTX;
            if (queryMarkup.empty())
                drawText(cr, QUERY, placeholder, TEXTX + CARETW + 7.0 * s, CY, theme.textFaint, TEXTMAX);
            else
                caretX = TEXTX + std::min(drawText(cr, QUERY, queryMarkup, TEXTX, CY, theme.text, TEXTMAX, PANGO_ELLIPSIZE_START), TEXTMAX) + 3.0 * s;
            if (caret) {
                cairo_rectangle(cr, caretX, CY - CARETH / 2.0, CARETW, CARETH);
                setColor(cr, theme.accent);
                cairo_fill(cr);
            }
            drawText(cr, SMALL, ESC, x + w - ESCW - 20.0 * s, CY, theme.textFaint);
        }

        // One setting: its section, its name, the value and a control that
        // shows where the value sits in its range.
        void tunerRow(cairo_t* cr, const Tuning::SParam& param, double x, double y, double w, double h, double s, bool selected, const STheme& theme) {
            const double R = theme.rounding * s;
            if (selected) {
                roundedRect(cr, x, y, w, h, R);
                setColor(cr, theme.accent);
                cairo_fill(cr);
            } else
                card(cr, x, y, w, h, R, theme);

            const double     MID  = y + h / 2.0;
            const auto       ON   = selected ? theme.onAccent : theme.text;
            const auto       DIM  = selected ? withAlpha(theme.onAccent, 0.62F) : theme.textFaint;
            const auto       HOT  = selected ? theme.onAccent : theme.accent;
            const STextStyle TAG{.px = theme.detailSize * s, .weight = PANGO_WEIGHT_NORMAL, .spacing = 1.3 * s};
            const STextStyle NAME{.px = theme.titleSize * s, .weight = selected ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_MEDIUM, .spacing = 1.4 * s};
            const STextStyle VALUE{.px = (theme.detailSize + 1.0) * s, .weight = PANGO_WEIGHT_BOLD, .spacing = 1.2 * s};

            const double VAL   = Tuning::get(param);
            double       right = x + w - 16.0 * s;
            if (param.kind == Tuning::EKind::BOOL) {
                const double PW = 34.0 * s, PH = 18.0 * s, PX = right - PW;
                const bool   YES = VAL >= 0.5;
                roundedRect(cr, PX, MID - PH / 2.0, PW, PH, PH / 2.0);
                if (YES) {
                    setColor(cr, HOT);
                    cairo_fill(cr);
                } else {
                    setColor(cr, DIM);
                    cairo_set_line_width(cr, 1.5 * s);
                    cairo_stroke(cr);
                }
                cairo_new_sub_path(cr);
                cairo_arc(cr, YES ? PX + PW - PH / 2.0 : PX + PH / 2.0, MID, PH / 2.0 - 3.5 * s, 0, 2 * M_PI);
                setColor(cr, YES ? (selected ? theme.accent : theme.onAccent) : DIM);
                cairo_fill(cr);
                right = PX - 12.0 * s;
            } else if (param.kind == Tuning::EKind::COLOR) {
                // The accent is the theme's, already resolved (presets, auto).
                const double SIZE = 18.0 * s, SX = right - SIZE;
                roundedRect(cr, SX, MID - SIZE / 2.0, SIZE, SIZE, 4.0 * s);
                setColor(cr, theme.accent);
                cairo_fill_preserve(cr);
                setColor(cr, selected ? theme.onAccent : CHyprColor{1.F, 1.F, 1.F, 0.35F});
                cairo_set_line_width(cr, 1.5 * s);
                cairo_stroke(cr);
                right = SX - 12.0 * s;
            } else if (param.choices.empty()) {
                const double SW = 150.0 * s, SH = 4.0 * s, SX = right - SW;
                const double F  = Tuning::fraction(param, VAL);
                roundedRect(cr, SX, MID - SH / 2.0, SW, SH, SH / 2.0);
                setColor(cr, selected ? withAlpha(theme.onAccent, 0.22F) : CHyprColor{1.F, 1.F, 1.F, 0.10F});
                cairo_fill(cr);
                if (F > 0.0) {
                    roundedRect(cr, SX, MID - SH / 2.0, std::max(SH, SW * F), SH, SH / 2.0);
                    setColor(cr, HOT);
                    cairo_fill(cr);
                }
                cairo_rectangle(cr, SX + SW * F - 1.5 * s, MID - 7.0 * s, 3.0 * s, 14.0 * s);
                setColor(cr, HOT);
                cairo_fill(cr);
                right = SX - 14.0 * s;
            }

            auto value = param.kind == Tuning::EKind::COLOR && Tuning::format(param, VAL).starts_with('#') ? escape(Tuning::format(param, VAL)) :
                                                                                                             ui(Tuning::format(param, VAL));
            if (!param.choices.empty() || param.kind == Tuning::EKind::COLOR)
                value = "‹ " + value + " ›";
            const double VALW = measureText(VALUE, value);
            drawText(cr, VALUE, value, right - VALW, MID, HOT);
            right -= VALW + 14.0 * s;

            const double TAGW  = 112.0 * s * theme.detailSize / 10.5;
            const double NAMEX = x + 16.0 * s + TAGW;
            drawText(cr, TAG, ui(param.section), x + 16.0 * s, MID, DIM, TAGW - 10.0 * s);
            drawText(cr, NAME, ui(param.label), NAMEX, MID, ON, right - NAMEX);
        }

        std::string homeRelative(const std::string& path) {
            const char* HOME = std::getenv("HOME");
            if (HOME && *HOME && path.starts_with(std::string{HOME} + "/"))
                return "~" + path.substr(std::string_view{HOME}.size());
            return path;
        }
    }

    // ---- theme -----------------------------------------------------------------

    void refreshTheme() {
        STheme theme;
        const auto* ACCENTPARAM = Tuning::find("navigator:accent");
        const auto  CONFIGURED  = ACCENTPARAM ? Tuning::text(*ACCENTPARAM) : ScrollOverview::Config::getNavigatorAccent();
        CHyprColor accent{1.F, 0.42F, 0.1F, 1.F};
        const auto PRESET = Tuning::presetValue(CONFIGURED);
        const auto VALUE  = PRESET ? *PRESET : CONFIGURED;
        if (const auto PARSED = parseHex(VALUE))
            accent = *PARSED;
        else if (VALUE == "auto") {
            auto& ref = ScrollOverview::Config::valueRef<::Config::IComplexConfigValue>("general:col.active_border");
            if (ref.good()) {
                if (const auto GRADIENT = dc<::Config::CGradientValueData*>(ref.ptr()); GRADIENT && !GRADIENT->m_colors.empty())
                    accent = luminous(GRADIENT->m_colors.front());
            }
        }
        accent.a        = 1.F;
        theme.accent    = accent;
        // Text on the accent is black or white, whichever contrasts more
        // (WCAG relative luminance).
        const auto  LINEAR    = [](float c) { return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F); };
        const float LUMINANCE = 0.2126F * LINEAR(accent.r) + 0.7152F * LINEAR(accent.g) + 0.0722F * LINEAR(accent.b);
        theme.onAccent        = (LUMINANCE + 0.05F) / 0.05F >= 1.05F / (LUMINANCE + 0.05F) ? CHyprColor{0.035F, 0.035F, 0.035F, 1.F} : CHyprColor{0.97F, 0.97F, 0.97F, 1.F};
        theme.panel     = CHyprColor{0.055F, 0.055F, 0.058F, 0.94F};
        theme.card      = CHyprColor{0.105F, 0.105F, 0.11F, 0.96F};
        theme.edge      = CHyprColor{1.F, 1.F, 1.F, 0.065F};
        theme.text      = CHyprColor{0.93F, 0.93F, 0.93F, 1.F};
        theme.textDim   = CHyprColor{0.56F, 0.56F, 0.57F, 1.F};
        theme.textFaint = CHyprColor{0.36F, 0.36F, 0.37F, 1.F};
        theme.font      = ScrollOverview::Config::getNavigatorMonoFont();
        if (theme.font.empty())
            theme.font = "Monospace";

        using Tuning::number, Tuning::flag;
        theme.hudScale   = number("navigator:hud_scale");
        theme.width      = number("navigator:width");
        theme.top        = number("navigator:top");
        theme.rows       = std::max(1, sc<int>(number("navigator:rows")));
        theme.rowH       = number("navigator:row_height");
        theme.searchH    = number("navigator:search_height");
        theme.rounding   = number("navigator:rounding");
        theme.tracking   = number("navigator:letter_spacing");
        theme.uppercase  = flag("navigator:uppercase");
        theme.corners    = flag("navigator:corner_labels");
        theme.querySize  = number("navigator:query_size");
        theme.titleSize  = number("navigator:title_size");
        theme.detailSize = number("navigator:detail_size");
        theme.cornerSize = number("navigator:corner_size");
        theme.labelSize  = number("navigator:label_size");
        theme.searchBorder = number("navigator:search_border");
        theme.searchGlow   = number("navigator:search_glow");
        theme.panelPad        = number("navigator:panel_padding");
        theme.panelShadow     = number("navigator:panel_shadow");
        theme.panelShadowSize = number("navigator:panel_shadow_size");
        // Lower panel opacity turns the whole palette to glass, rows included.
        const float PANELALPHA = sc<float>(number("navigator:panel_opacity"));
        theme.panel.a          = PANELALPHA;
        theme.card.a           = std::min(0.96F, PANELALPHA + 0.02F);
        theme.searchEdge       = accent;
        if (const auto EDGE = parseHex(ScrollOverview::Config::getValue<std::string>("plugin:spatialoverview:navigator:search_border_color")))
            theme.searchEdge = *EDGE;
        theme.searchEdge.a = sc<float>(number("navigator:search_border_opacity"));

        theme.signature = std::format("{}{}{}|{}|{:.3f}|{:.1f}|{:.4f}|{}|{:.1f}|{:.1f}|{:.1f}|{:.2f}|{}{}|{:.2f}|{:.2f}|{:.2f}|{:.2f}|{:.2f}|{:.2f}|{:.2f}|{}{:.2f}",
                                      hex(theme.accent), hex(theme.card), hex(theme.searchEdge), theme.font, theme.hudScale, theme.width, theme.top, theme.rows,
                                      theme.rowH, theme.searchH, theme.rounding, theme.tracking, theme.uppercase, theme.corners, theme.querySize, theme.titleSize,
                                      theme.detailSize, theme.cornerSize, theme.labelSize, theme.searchBorder, theme.searchGlow, PANELALPHA, theme.searchEdge.a) +
            std::format("|{:.1f}|{:.2f}|{:.1f}", theme.panelPad, theme.panelShadow, theme.panelShadowSize);
        g_theme = theme;
    }

    const STheme& theme() {
        if (g_themeFrame != g_frame) {
            refreshTheme();
            g_themeFrame = g_frame;
        }
        return g_theme;
    }

    // ---- palette -----------------------------------------------------------------

    bool palette(const SPaletteView& view, SP<Render::ITexture>& texture, CBox& box) {
        const auto& STATE = Navigator::state();
        if (!STATE.open || view.monitorSize.x <= 0 || view.monitorSize.y <= 0)
            return false;

        const auto& THEME = theme();
        const auto  KEY   = paletteKey(view, STATE);
        if (g_palette.texture && g_palette.key == KEY) {
            texture = g_palette.texture;
            box     = g_palette.box;
            return true;
        }

        const double s       = view.scale * THEME.hudScale;
        const double PAD     = THEME.panelPad;
        const double M       = std::max(36.0, THEME.panelShadowSize + 6.0) * s; // room for the shadow and glow
        const double W       = std::min(THEME.width * s, view.monitorSize.x * view.scale * 0.94);
        const double INNERW  = W - PAD * 2.0 * s;
        const double FOOTH   = footerHeight(THEME) * s;
        const double R       = THEME.rounding * s;
        const bool   TUNER   = STATE.tuner;
        const bool   LIST    = !TUNER && Navigator::listVisible() && !STATE.helpOpen;
        const int    COUNT   = TUNER ? sc<int>(STATE.tunerResults.size()) : sc<int>(STATE.results.size());
        const int    OFFSET  = TUNER ? STATE.tunerOffset : STATE.listOffset;
        const int    MAXROWS = TUNER ? Navigator::TUNER_ROWS : THEME.rows;
        const double ROWH    = (TUNER ? TUNER_ROW_H : THEME.rowH) * s;
        const int    ROWS    = LIST || TUNER ? std::max(1, std::min(MAXROWS, COUNT - OFFSET)) : 0;
        const bool   MORE    = LIST && COUNT > OFFSET + ROWS;
        const bool   FOOTER  = TUNER || MORE;
        const bool   HELP    = STATE.helpOpen && !TUNER;
        const double HELPH   = HELP ? helpHeight(s) : 0.0;
        const double SEARCHH = THEME.searchH * s;

        double innerH = SEARCHH;
        if (LIST || TUNER)
            innerH += GAP * s + ROWS * ROWH + (ROWS - 1) * GAP * s + (FOOTER ? FOOTH : 0.0);
        if (HELP)
            innerH += GAP * s + HELPH;
        const double CONTAINERH = innerH + PAD * 2.0 * s;

        const int TEXW    = sc<int>(std::ceil(W + M * 2.0));
        const int TEXH    = sc<int>(std::ceil(CONTAINERH + M * 2.0));
        auto*     surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, TEXW, TEXH);
        auto*     cr      = cairo_create(surface);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

        std::vector<CBox>                 panels{CBox{M, M, W, CONTAINERH}};
        std::vector<SCachedPalette::SRow> rows;

        // The container: dark glass lifted by a soft shadow.
        if (THEME.panel.a > 0.001F) {
            // Even on every side, so the padding reads the same all round.
            shadow(cr, M, M, W, CONTAINERH, R + 7.0 * s, THEME.panelShadowSize * s, THEME.panelShadow, 0.0);
            roundedRect(cr, M, M, W, CONTAINERH, R + 7.0 * s);
            cairo_pattern_t* fill = cairo_pattern_create_linear(0, M, 0, M + CONTAINERH);
            const auto       TOP  = mix(THEME.panel, CHyprColor{1, 1, 1, THEME.panel.a}, 0.035F);
            cairo_pattern_add_color_stop_rgba(fill, 0, TOP.r, TOP.g, TOP.b, TOP.a);
            cairo_pattern_add_color_stop_rgba(fill, 1, THEME.panel.r, THEME.panel.g, THEME.panel.b, THEME.panel.a);
            cairo_set_source(cr, fill);
            cairo_fill_preserve(cr);
            cairo_pattern_destroy(fill);
            setColor(cr, THEME.edge);
            cairo_set_line_width(cr, 1.0 * s);
            cairo_stroke(cr);
        }

        const double X = M + PAD * s;
        double       y = M + PAD * s;

        if (TUNER)
            searchField(cr, X, y, INNERW, SEARCHH, s, THEME, STATE.tunerQuery.empty() ? "" : caseMarkup(STATE.tunerQuery), ui("Tune the look  //  type to filter"),
                        STATE.caretVisible);
        else
            searchField(cr, X, y, INNERW, SEARCHH, s, THEME, STATE.query.empty() ? "" : caseMarkup(STATE.query),
                        ui(STATE.switcher ? "Recent windows" : "Search windows"), STATE.caretVisible);
        y += SEARCHH;

        if (TUNER) {
            y += GAP * s;
            if (STATE.tunerResults.empty()) {
                card(cr, X, y, INNERW, ROWH, R, THEME);
                const STextStyle NOTE{.px = THEME.titleSize * s, .weight = PANGO_WEIGHT_MEDIUM, .spacing = 1.5 * s};
                drawText(cr, NOTE, ui("No setting matches “") + caseMarkup(STATE.tunerQuery) + "”", X + 22.0 * s, y + ROWH / 2.0, THEME.textDim, INNERW - 44.0 * s);
            }
            for (int row = 0; row < ROWS && OFFSET + row < COUNT; ++row) {
                const int INDEX = OFFSET + row;
                tunerRow(cr, Tuning::params()[STATE.tunerResults[INDEX]], X, y + row * (ROWH + GAP * s), INNERW, ROWH, s, INDEX == STATE.tunerSelected, THEME);
            }
            y += ROWS * ROWH + (ROWS - 1) * GAP * s;

            // What the selected setting does, so no row is a guess.
            const STextStyle FOOT{.px = THEME.detailSize * s, .weight = PANGO_WEIGHT_NORMAL, .spacing = 1.4 * s};
            std::string      about = ui(std::format("{} of {}", COUNT == 0 ? 0 : STATE.tunerSelected + 1, COUNT));
            if (COUNT > 0)
                about += "  //  " + ui(Tuning::params()[STATE.tunerResults[STATE.tunerSelected]].description);
            drawText(cr, FOOT, about, X + 14.0 * s, y + GAP * s + (FOOTH - GAP * s) / 2.0, THEME.textDim, INNERW - 28.0 * s);
            y += FOOTH;
        }

        if (LIST) {
            const STextStyle TITLE{.px = THEME.titleSize * s, .weight = PANGO_WEIGHT_MEDIUM, .spacing = 1.5 * s};
            const STextStyle TITLESEL{.px = THEME.titleSize * s, .weight = PANGO_WEIGHT_BOLD, .spacing = 1.5 * s};
            const STextStyle META{.px = THEME.detailSize * s, .weight = PANGO_WEIGHT_NORMAL, .spacing = 1.3 * s};
            const STextStyle HINT{.px = (THEME.detailSize + 1.0) * s, .weight = PANGO_WEIGHT_NORMAL, .spacing = 1.2 * s};
            const STextStyle BADGE{.px = (THEME.detailSize + 1.0) * s, .weight = PANGO_WEIGHT_BOLD, .spacing = 1.8 * s};
            const double     WELL = std::clamp(THEME.rowH * 0.62, 16.0, 72.0) * s;
            // Title over detail, centered as a pair in the row.
            const double TITLEH = THEME.titleSize * 1.25 * s, DETAILH = THEME.detailSize * 1.25 * s, LINEGAP = 3.5 * s;
            const double TITLEDY = -(TITLEH + LINEGAP + DETAILH) / 2.0 + TITLEH / 2.0;
            const double DETAILDY = (TITLEH + LINEGAP + DETAILH) / 2.0 - DETAILH / 2.0;

            y += GAP * s;
            if (STATE.results.empty()) {
                card(cr, X, y, INNERW, ROWH, R, THEME);
                const double MID = y + ROWH / 2.0;
                drawText(cr, TITLE, ui("No match for “") + caseMarkup(STATE.query) + "”", X + 22.0 * s, MID + TITLEDY, THEME.text, INNERW - 44.0 * s);
                drawText(cr, META, ui("Backspace edits  //  Esc clears"), X + 22.0 * s, MID + DETAILDY, THEME.textDim);
            }

            for (int row = 0; row < ROWS && OFFSET + row < COUNT; ++row) {
                const int   INDEX  = OFFSET + row;
                const auto& RESULT = STATE.results[INDEX];
                const auto  WINDOW = RESULT.window.lock();
                if (!WINDOW)
                    continue;
                const bool   SELECTED = INDEX == STATE.selected;
                const double RY       = y + row * (ROWH + GAP * s);
                const double MID      = RY + ROWH / 2.0;
                rows.push_back({.box = CBox{X, RY, INNERW, ROWH}, .index = INDEX, .window = WINDOW});

                if (SELECTED) {
                    roundedRect(cr, X, RY, INNERW, ROWH, R);
                    setColor(cr, THEME.accent);
                    cairo_fill(cr);
                } else
                    card(cr, X, RY, INNERW, ROWH, R, THEME);

                iconWell(cr, WINDOW, X + 11.0 * s, MID - WELL / 2.0, WELL, s, SELECTED, THEME);

                // Right edge: the accent row's black badge, or a quick-jump key.
                double rightEdge = X + INNERW - 16.0 * s;
                if (SELECTED) {
                    const auto   TEXT   = ui("Enter");
                    const double BADGEW = measureText(BADGE, TEXT) + 26.0 * s;
                    const double BADGEH = std::min(34.0, THEME.rowH * 0.59) * s;
                    roundedRect(cr, rightEdge - BADGEW + 4.0 * s, MID - BADGEH / 2.0, BADGEW, BADGEH, std::min(7.0 * s, BADGEH / 2.0));
                    cairo_set_source_rgba(cr, 0.03, 0.03, 0.03, 1.0);
                    cairo_fill(cr);
                    drawText(cr, BADGE, TEXT, rightEdge - BADGEW + 4.0 * s + 13.0 * s, MID, THEME.accent);
                    rightEdge -= BADGEW + 12.0 * s;
                } else if (row < 9) {
                    const auto   TEXT  = std::format("[ ⌃{} ]", row + 1);
                    const double HINTW = measureText(HINT, TEXT);
                    drawText(cr, HINT, TEXT, rightEdge - HINTW, MID, THEME.textFaint);
                    rightEdge -= HINTW + 16.0 * s;
                }

                const double TX   = X + 11.0 * s + WELL + 16.0 * s;
                const double TMAX = rightEdge - TX;
                const auto   ON   = SELECTED ? THEME.onAccent : THEME.text;
                const auto   TITLEMARKUP =
                    matchedMarkup(Navigator::displayTitle(WINDOW), RESULT.titleMatches, SELECTED ? std::nullopt : std::optional<CHyprColor>{THEME.accent});
                drawText(cr, SELECTED ? TITLESEL : TITLE, TITLEMARKUP, TX, MID + TITLEDY, ON, TMAX);

                auto meta = matchedMarkup(Navigator::displayClass(WINDOW), RESULT.classMatches, SELECTED ? std::nullopt : std::optional<CHyprColor>{THEME.accent});
                if (const auto HINTTEXT = directionHint(WINDOW, view); !HINTTEXT.empty())
                    meta += "  //  " + ui(HINTTEXT);
                drawText(cr, META, meta, TX, MID + DETAILDY, SELECTED ? withAlpha(THEME.onAccent, 0.72F) : THEME.textDim, TMAX);
            }
            y += ROWS * ROWH + (ROWS - 1) * GAP * s;

            if (MORE) {
                const STextStyle MORESTYLE{.px = THEME.detailSize * s, .weight = PANGO_WEIGHT_NORMAL, .spacing = 1.6 * s};
                drawText(cr, MORESTYLE, ui(std::format("+{} more  //  keep typing or ↓", COUNT - OFFSET - ROWS)), X + 14.0 * s, y + GAP * s + (FOOTH - GAP * s) / 2.0,
                         THEME.textFaint);
                y += FOOTH;
            }
        }

        if (HELP) {
            y += GAP * s;
            drawHelp(cr, X, y, INNERW, HELPH, s, THEME);
            y += HELPH;
        }

        cairo_destroy(cr);
        auto uploaded = upload(surface);
        cairo_surface_destroy(surface);
        if (!uploaded)
            return false;

        const Vector2D FULL = view.monitorSize * view.scale;
        g_palette.key       = KEY;
        g_palette.texture   = uploaded;
        g_palette.panels    = std::move(panels);
        g_palette.rows      = std::move(rows);
        g_palette.box       = CBox{std::round((FULL.x - TEXW) / 2.0), std::round(FULL.y * THEME.top - M), sc<double>(TEXW), sc<double>(TEXH)};
        texture             = g_palette.texture;
        box                 = g_palette.box;
        return true;
    }

    double selectionFocusY(const Vector2D& monitorSize) {
        const auto&  THEME  = theme();
        const double TOP    = monitorSize.y * THEME.top;
        const double BOTTOM = TOP + (THEME.panelPad * 2.0 + THEME.searchH + GAP + THEME.rows * THEME.rowH + (THEME.rows - 1) * GAP + footerHeight(THEME)) * THEME.hudScale;
        return std::clamp((BOTTOM + monitorSize.y) / 2.0, monitorSize.y / 2.0, monitorSize.y * 0.66);
    }

    int paletteHit(const Vector2D& point, PHLWINDOW* window) {
        if (!g_palette.texture || !Navigator::isOpen())
            return PALETTE_MISS;
        const auto LOCAL = point - g_palette.box.pos();
        for (const auto& row : g_palette.rows) {
            if (!row.box.containsPoint(LOCAL))
                continue;
            // The row names the window it showed, even if the results have
            // been re-ranked since that frame.
            if (window)
                *window = row.window.lock();
            return row.index;
        }
        for (const auto& panel : g_palette.panels) {
            if (panel.containsPoint(LOCAL))
                return PALETTE_PANEL;
        }
        return PALETTE_MISS;
    }

    // ---- screen corners ------------------------------------------------------------

    bool chrome(const SPaletteView& view, SChrome& out) {
        const auto& STATE = Navigator::state();
        if (!STATE.open || view.monitorSize.x <= 0 || view.monitorSize.y <= 0)
            return false;

        const auto& THEME  = theme();
        if (!THEME.corners)
            return false;
        const int   TOTAL  = Navigator::candidateCount();
        const int   COUNT  = sc<int>(STATE.results.size());
        const bool  LIST   = Navigator::listVisible() && !STATE.tuner;
        const auto  ACCENT = hex(THEME.accent);
        const auto  K      = [&ACCENT](const char* key, const char* what) { return std::format("<span foreground=\"{}\">{}</span> {}", ACCENT, ui(key), ui(what)); };

        struct SBlock {
            std::vector<std::pair<std::string, CHyprColor>> lines; // markup, color
            bool                                            right = false;
        };
        std::vector<SBlock> blocks(3);

        blocks[0].lines.push_back({ui("Phantomat  //  Canvas"), THEME.textDim});
        if (!view.experiment.empty())
            blocks[0].lines.push_back({ui("[ Exp  //  ") + caseMarkup(view.experiment) + " ]", THEME.accent});

        blocks[1].right = true;
        blocks[1].lines.push_back({ui(std::format("Zoom {:03d}%  //  {:02d} windows  //  ", sc<int>(std::round(view.zoom * 100.F)), TOTAL)) + caseMarkup(view.monitorName),
                                   THEME.textDim});
        if (LIST && COUNT > 0)
            blocks[1].lines.push_back({std::format("[ {:02d} / {:02d} ]", STATE.selected + 1, COUNT), THEME.accent});
        else if (Navigator::queryActive())
            blocks[1].lines.push_back({"[ 00 / 00 ]", THEME.accent});
        blocks[1].lines.push_back(
            {"&gt;_" + ui(STATE.tuner ? "Tuning" : STATE.switcher ? "Release Alt" : LIST ? "Enter" : "Type to search"), THEME.accent});

        if (!STATE.notice.empty())
            blocks[2].lines.push_back({caseMarkup(STATE.notice), THEME.accent});
        else if (STATE.tuner) {
            blocks[2].lines.push_back({ui("Saved as you go to ") + escape(homeRelative(Tuning::filePath())), THEME.textFaint});
            blocks[2].lines.push_back({K("↑↓", "Pick") + "   " + K("←→", "Adjust") + "   " + K("⇧", "Fine") + "   " + K("⌃", "×10") + "   " + K("Del", "Revert") +
                                           "   " + K("Esc", "Done"),
                                       THEME.textDim});
        }
        else if (STATE.switcher)
            blocks[2].lines.push_back({K("⇥", "Next") + "   " + K("⇧⇥", "Back") + "   " + K("Esc", "Cancel"), THEME.textDim});
        else
            blocks[2].lines.push_back({K("↵", "Go") + "   " + K("⇥", "Next") + "   " + K("⇧↵", "Bring here") + "   " + K("⌃0", "Fit all") + "   " + K("⌃,", "Tune") + "   " +
                                           K("F1", "Keys") + "   " + K("Esc", "Back"),
                                       THEME.textDim});

        const double     s = view.scale * THEME.hudScale;
        const STextStyle STYLE{.px = THEME.cornerSize * s, .weight = PANGO_WEIGHT_MEDIUM, .spacing = 2.2 * s};
        const double     LINE = THEME.cornerSize * 1.83 * s;

        std::string key = std::format("{}x{}@{:.3f}|{}", view.monitorSize.x, view.monitorSize.y, view.scale, THEME.signature);
        for (const auto& block : blocks) {
            for (const auto& [markup, color] : block.lines)
                key += "|" + markup + hex(color);
        }
        if (g_chrome.chrome.texture && g_chrome.key == key) {
            out = g_chrome.chrome;
            return true;
        }

        std::vector<Vector2D> sizes;
        double                atlasW = 1.0, atlasH = 0.0;
        for (const auto& block : blocks) {
            double w = 0.0;
            for (const auto& line : block.lines)
                w = std::max(w, measureText(STYLE, line.first));
            const Vector2D SIZE{std::ceil(w + 4.0 * s), std::ceil(block.lines.size() * LINE)};
            sizes.push_back(SIZE);
            atlasW = std::max(atlasW, SIZE.x);
            atlasH += SIZE.y + 2.0;
        }
        atlasH = std::max(1.0, std::ceil(atlasH));

        auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sc<int>(atlasW), sc<int>(atlasH));
        auto* cr      = cairo_create(surface);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

        const Vector2D FULL   = view.monitorSize * view.scale;
        const double   MARGIN = 38.0 * s;
        SChrome        result;
        double         ay = 0.0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const auto& BLOCK = blocks[i];
            const auto  SIZE  = sizes[i];
            for (size_t line = 0; line < BLOCK.lines.size(); ++line) {
                const auto& [markup, color] = BLOCK.lines[line];
                const double LX              = BLOCK.right ? SIZE.x - 2.0 * s - measureText(STYLE, markup) : 2.0 * s;
                drawText(cr, STYLE, markup, LX, ay + line * LINE + LINE / 2.0, color);
            }
            CBox screen;
            if (i == 0)
                screen = CBox{MARGIN, MARGIN * 0.8, SIZE.x, SIZE.y};
            else if (i == 1)
                screen = CBox{FULL.x - MARGIN - SIZE.x, MARGIN * 0.8, SIZE.x, SIZE.y};
            else
                screen = CBox{MARGIN, FULL.y - MARGIN * 0.8 - SIZE.y, SIZE.x, SIZE.y};
            result.regions.push_back(
                {.screen = screen.round(), .uv = {0.F, sc<float>(ay / atlasH), sc<float>(SIZE.x / atlasW), sc<float>(SIZE.y / atlasH)}});
            ay += SIZE.y + 2.0;
        }

        cairo_destroy(cr);
        result.texture = upload(surface);
        cairo_surface_destroy(surface);
        if (!result.texture)
            return false;

        g_chrome = SCachedChrome{.key = key, .chrome = result};
        out      = result;
        return true;
    }

    // ---- grid ----------------------------------------------------------------------

    SP<Render::ITexture> gridTile(double cell, double mark, double fine, int style) {
        // Quantize so a zoom animation re-renders the (tiny) tile at most once
        // per visible change.
        cell = std::round(cell * 8.0) / 8.0;
        mark = std::round(mark * 4.0) / 4.0;
        fine = std::round(std::clamp(fine, 0.0, 1.0) * 32.0) / 32.0;
        if (cell < 2.0)
            return nullptr;

        const auto KEY = std::format("{}|{}|{}|{}", cell, mark, fine, style);
        if (g_gridTile && g_gridTileKey == KEY)
            return g_gridTile;

        const double SPAN    = cell * 2.0;
        const int    PIXELS  = std::max(2, sc<int>(std::round(SPAN)));
        auto*        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, PIXELS, PIXELS);
        auto*        cr      = cairo_create(surface);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
        cairo_scale(cr, PIXELS / SPAN, PIXELS / SPAN);

        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 2; ++column) {
                const double ALPHA = row == 0 && column == 0 ? 1.0 : fine;
                if (ALPHA <= 0.001)
                    continue;
                const double X = cell * 0.5 + column * cell;
                const double Y = cell * 0.5 + row * cell;
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, ALPHA);
                if (style == 1) {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, X, Y, mark * 0.5, 0, 2 * M_PI);
                    cairo_fill(cr);
                    continue;
                }
                // Lines: each drawn once, through its own mark.
                if (row == 0) {
                    cairo_rectangle(cr, X - mark * 0.5, 0, mark, SPAN);
                    cairo_fill(cr);
                }
                if (column == 0) {
                    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, row == 0 ? 1.0 : fine);
                    cairo_rectangle(cr, 0, Y - mark * 0.5, SPAN, mark);
                    cairo_fill(cr);
                }
            }
        }

        cairo_destroy(cr);
        g_gridTile    = upload(surface);
        g_gridTileKey = g_gridTile ? KEY : std::string{};
        cairo_surface_destroy(surface);
        return g_gridTile;
    }

    // ---- world labels ------------------------------------------------------------

    SLabel label(const PHLWINDOW& window, bool selected, float scale, float maxWidthPx) {
        if (!window || maxWidthPx < 84.F * scale * theme().labelSize / 10.5)
            return {};

        const auto&      THEME = theme();
        const double     s     = scale;
        const double     U     = THEME.labelSize / 10.5;
        const double     H     = 22.0 * U * s;
        const double     ICON  = 14.0 * U * s;
        const double     PADL  = 7.0 * s + ICON + 8.0 * s;
        const double     PADR  = 9.0 * s;
        const STextStyle STYLE{.px = THEME.labelSize * s, .weight = selected ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_MEDIUM, .spacing = 1.3 * s};
        const auto       TITLE = caseMarkup(Navigator::displayTitle(window));

        const auto WIDTHKEY = std::format("{}\x1f{}\x1f{:.2f}\x1f{}", TITLE, selected, s, THEME.signature);
        auto       measured = g_labelWidths.find(WIDTHKEY);
        if (measured == g_labelWidths.end()) {
            if (g_labelWidths.size() > 2048)
                g_labelWidths.clear();
            measured = g_labelWidths.emplace(WIDTHKEY, measureText(STYLE, TITLE)).first;
        }
        const double NATURAL = measured->second + PADL + PADR;
        const double LIMIT   = std::min<double>(maxWidthPx, 460.0 * s);
        // Width buckets keep zoom animations from re-rendering every frame.
        const double WIDTH = NATURAL <= LIMIT ? NATURAL : std::max(48.0 * s, std::floor(LIMIT / (24.0 * s)) * 24.0 * s);

        const auto KEY = std::format("{}\x1f{}\x1f{:.2f}\x1f{}\x1f{}\x1f{}", TITLE, window->m_class, s, sc<int>(WIDTH), selected, THEME.signature);
        if (const auto IT = g_labels.find(KEY); IT != g_labels.end()) {
            IT->second.lastUsed = g_frame;
            return IT->second.label;
        }

        const int TEXW    = sc<int>(std::ceil(WIDTH));
        const int TEXH    = sc<int>(std::ceil(H));
        auto*     surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, TEXW, TEXH);
        auto*     cr      = cairo_create(surface);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

        roundedRect(cr, 0.5, 0.5, TEXW - 1.0, TEXH - 1.0, 4.0 * s);
        setColor(cr, selected ? THEME.accent : withAlpha(THEME.panel, 0.9F));
        cairo_fill_preserve(cr);
        setColor(cr, selected ? THEME.accent : THEME.edge);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        const double ICONY = (H - ICON) / 2.0;
        if (auto* icon = Icons::forWindow(window, sc<int>(std::round(ICON))))
            drawIcon(cr, icon, 7.0 * s, ICONY, ICON);
        else {
            cairo_rectangle(cr, 7.0 * s + ICON * 0.3, ICONY + ICON * 0.3, ICON * 0.4, ICON * 0.4);
            setColor(cr, selected ? THEME.onAccent : THEME.textDim);
            cairo_fill(cr);
        }

        drawText(cr, STYLE, TITLE, PADL, H / 2.0, selected ? THEME.onAccent : THEME.text, WIDTH - PADL - PADR);

        cairo_destroy(cr);
        SLabel result{.texture = upload(surface), .size = {sc<double>(TEXW), sc<double>(TEXH)}};
        cairo_surface_destroy(surface);
        if (!result.texture)
            return {};

        g_labels[KEY] = SCachedLabel{.label = result, .lastUsed = g_frame};
        return result;
    }

    void endFrame() {
        ++g_frame;
        constexpr size_t LABEL_CAP = 256;
        if (g_frame % 120 != 0 && g_labels.size() <= LABEL_CAP)
            return;
        std::erase_if(g_labels, [](const auto& entry) { return g_frame - entry.second.lastUsed > 600; });
        if (g_labels.size() <= LABEL_CAP)
            return;
        // Still over the cap (zooming across many width buckets, or titles that
        // tick): keep only the most recently used labels.
        std::vector<uint64_t> stamps;
        stamps.reserve(g_labels.size());
        for (const auto& [key, entry] : g_labels)
            stamps.push_back(entry.lastUsed);
        std::ranges::nth_element(stamps, stamps.begin() + (stamps.size() - LABEL_CAP / 2));
        const uint64_t CUTOFF = stamps[stamps.size() - LABEL_CAP / 2];
        std::erase_if(g_labels, [CUTOFF](const auto& entry) { return entry.second.lastUsed < CUTOFF; });
    }

    void shutdown() {
        g_labels.clear();
        g_labelWidths.clear();
        g_gridTile.reset();
        g_gridTileKey.clear();
        g_palette = SCachedPalette{};
        g_chrome  = SCachedChrome{};
        Icons::shutdown();
        if (g_measure)
            cairo_destroy(g_measure);
        if (g_measureSurface)
            cairo_surface_destroy(g_measureSurface);
        g_measure        = nullptr;
        g_measureSurface = nullptr;
    }
}
