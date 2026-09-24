#pragma once

#include "globals.hpp"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/time/Time.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Coarse app families used by smart arrangement, search, and window badges.
enum class ECanvasWindowCategory : uint8_t {
    TERMINAL,
    BROWSER,
    COMMUNICATION,
    DEVELOPMENT,
    FILES_NOTES,
    MEDIA,
    OTHER,
};

ECanvasWindowCategory canvasWindowCategory(const PHLWINDOW& window);

// The navigator is the keyboard-first layer of the zoomed-out canvas: a
// search palette that ranks every window as you type, while the camera and
// keyboard focus follow the best match. Its state is shared by all monitors;
// only the output that owns keyboard focus draws the palette.
namespace SpatialOverview::Navigator {

    struct SResult {
        PHLWINDOWREF        window;
        int                 score = 0;
        std::vector<size_t> titleMatches; // codepoint indices into displayTitle()
        std::vector<size_t> classMatches; // codepoint indices into displayClass()
    };

    struct SState {
        bool                 open         = false;
        std::string          query;
        bool                 listRevealed = false;
        int                  selected     = 0;
        int                  listOffset   = 0;
        std::vector<SResult> results;
        bool                 helpOpen     = false;
        bool                 switcher     = false; // opened by Alt+Tab; releasing Alt lands
        bool                 caretVisible = true;
        PHLWINDOWREF         returnWindow;
        std::string          notice;
        Time::steady_tp      noticeUntil = {};
        Time::steady_tp      openedAt    = {};
        uint64_t             revision    = 1;

        // The live tuner: settings in place of the result list.
        bool                 tuner         = false;
        std::string          tunerQuery;    // filters settings by name
        std::vector<int>     tunerResults;  // indices into Tuning::params()
        int                  tunerSelected = 0;
        int                  tunerOffset   = 0;
    };

    constexpr int    TUNER_ROWS = 10;

    SState&          state();
    void             touch();
    void             begin(const PHLWINDOW& focused);
    void             end();
    bool             isOpen();
    void             setDamageCallback(std::function<void()> callback);
    void             showNotice(const std::string& text);

    // Window text, as shown in the palette and on canvas labels.
    std::string      displayTitle(const PHLWINDOW& window);
    std::string      displayClass(const PHLWINDOW& window);
    std::string_view categoryCode(ECanvasWindowCategory category);
    std::string_view categoryName(ECanvasWindowCategory category);

    // Query editing. All operations are UTF-8 aware and return whether the
    // query changed.
    bool             appendText(const std::string& utf8);
    bool             popChar();
    bool             popWord();
    bool             clearQuery();
    bool             queryActive();
    bool             listVisible();

    // Ranking. Rebuilding keeps the selected window when it still matches,
    // unless resetSelection is set (a new query always starts at the best hit).
    void             rebuildResults(bool resetSelection);
    PHLWINDOW        selectedWindow();
    bool             selectWindow(const PHLWINDOW& window);
    bool             moveSelection(int delta);
    bool             isMatch(const PHLWINDOW& window);
    int              candidateCount();

    // Most-recently-used order. Focus changes made while the navigator is
    // browsing are provisional and are not recorded until the user lands.
    void             noteFocus(const PHLWINDOW& window, bool force = false);
    void             forget(const PHLWINDOW& window);

    // Key repeat for keys the navigator consumes (Wayland leaves repeat to
    // clients, and the navigator is acting as the client here).
    struct SRepeatKey {
        uint32_t    keycode = 0;
        uint32_t    keysym  = 0;
        uint32_t    mods    = 0;
        std::string text;
    };
    void             startRepeat(const SRepeatKey& key, int delayMs, int rateHz, std::function<void(const SRepeatKey&)> handler);
    void             stopRepeat(uint32_t keycode = 0);

    // Keys whose press was consumed also have their release consumed, even if
    // the navigator closed in between, so applications never see half a key.
    void             markConsumed(uint32_t keycode);
    bool             takeConsumed(uint32_t keycode);

    // The live tuner (Ctrl+, in the navigator). Adjusting applies at once and
    // saves to the tuning file; each returns whether anything changed.
    void             openTuner();
    void             closeTuner();
    bool             tunerAppend(const std::string& utf8);
    bool             tunerPop();
    bool             tunerClear();
    bool             tunerMove(int delta);
    bool             tunerAdjust(double steps);
    bool             tunerRevert();

    void             shutdown();
}
