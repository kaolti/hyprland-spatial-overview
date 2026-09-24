#define WLR_USE_UNSTABLE

#include "Navigator.hpp"
#include "Tuning.hpp"

#include <algorithm>
#include <cctype>
#include <glib.h>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Group.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

static std::string canvasLowercaseAscii(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return sc<char>(std::tolower(c)); });
    return value;
}

static bool canvasContainsAny(std::string_view value, std::initializer_list<std::string_view> needles) {
    return std::ranges::any_of(needles, [value](const auto needle) { return value.contains(needle); });
}

ECanvasWindowCategory canvasWindowCategory(const PHLWINDOW& window) {
    if (!window)
        return ECanvasWindowCategory::OTHER;

    const auto CLASS = canvasLowercaseAscii(window->m_class + " " + window->m_initialClass);
    const auto TITLE = canvasLowercaseAscii(window->m_title + " " + window->m_initialTitle);
    const auto ALL   = CLASS + " " + TITLE;

    if (canvasContainsAny(CLASS, {"foot", "alacritty", "kitty", "ghostty", "wezterm", "konsole", "terminal"}))
        return ECanvasWindowCategory::TERMINAL;
    if (canvasContainsAny(ALL, {"slack", "discord", "msteams", "microsoft teams", "signal", "telegram", "whatsapp", "mattermost", "element", "gmail",
                                "mail.google", "outlook", "thunderbird", "hey.com", " imbox", " inbox"}))
        return ECanvasWindowCategory::COMMUNICATION;
    if (canvasContainsAny(CLASS, {"chromium", "chrome", "firefox", "brave", "vivaldi", "zen", "browser"}))
        return ECanvasWindowCategory::BROWSER;
    if (canvasContainsAny(CLASS, {"code", "codium", "jetbrains", "idea", "clion", "pycharm", "webstorm", "zed", "sublime", "emacs", "nvim", "dev"}))
        return ECanvasWindowCategory::DEVELOPMENT;
    if (canvasContainsAny(CLASS, {"nautilus", "thunar", "dolphin", "nemo", "files", "obsidian", "notion", "logseq", "notes", "writer"}))
        return ECanvasWindowCategory::FILES_NOTES;
    if (canvasContainsAny(CLASS, {"spotify", "vlc", "mpv", "music", "video", "player"}))
        return ECanvasWindowCategory::MEDIA;
    return ECanvasWindowCategory::OTHER;
}

namespace SpatialOverview::Navigator {
    namespace {
        SState                                  g_state;
        std::function<void()>                   g_damage;
        wl_event_source*                        g_tickTimer = nullptr;
        std::unordered_map<uint64_t, uint64_t>  g_focusStamps;
        uint64_t                                g_focusCounter = 0;
        std::vector<PHLWINDOWREF>               g_sessionOrder;
        std::unordered_set<uint32_t>            g_consumedKeys;

        wl_event_source*                        g_repeatTimer = nullptr;
        SRepeatKey                              g_repeatKey;
        int                                     g_repeatIntervalMs = 40;
        std::function<void(const SRepeatKey&)>  g_repeatHandler;

        constexpr int CARET_BLINK_MS = 530;

        // One folded character per source codepoint, so match positions map
        // straight back onto the text that is displayed.
        struct SFoldedText {
            std::vector<gunichar> chars;
            std::vector<bool>     wordStart;
        };

        PHLWINDOW overviewWindow(const PHLWINDOW& window) {
            if (!window)
                return nullptr;
            if (window->m_group)
                return window->m_group->current();
            return window;
        }

        bool isCandidate(const PHLWINDOW& window) {
            if (!Desktop::View::validMapped(window))
                return false;
            if (window->m_workspace && window->m_workspace->m_isSpecialWorkspace)
                return false;
            if (window->m_pinned)
                return false;
            return true;
        }

        std::vector<PHLWINDOW> candidates() {
            std::vector<PHLWINDOW>          result;
            std::unordered_set<const void*> visited;
            for (const auto& windowRef : Desktop::windowState()->windows()) {
                const auto WINDOW = overviewWindow(windowRef);
                if (!isCandidate(WINDOW) || !visited.emplace(WINDOW.get()).second)
                    continue;
                result.emplace_back(WINDOW);
            }
            return result;
        }

        gunichar foldChar(gunichar c) {
            char        buffer[8];
            const int   LENGTH = g_unichar_to_utf8(c, buffer);
            gchar*      folded = g_utf8_casefold(buffer, LENGTH);
            gchar*      nfd    = folded ? g_utf8_normalize(folded, -1, G_NORMALIZE_NFD) : nullptr;
            gunichar    result = g_unichar_tolower(c);
            for (const gchar* p = nfd; p && *p; p = g_utf8_next_char(p)) {
                const gunichar D = g_utf8_get_char(p);
                if (g_unichar_ismark(D))
                    continue;
                result = D;
                break;
            }
            g_free(nfd);
            g_free(folded);
            return result;
        }

        SFoldedText fold(const std::string& text) {
            SFoldedText result;
            if (!g_utf8_validate(text.c_str(), sc<gssize>(text.size()), nullptr)) {
                bool previousAlnum = false;
                for (const unsigned char c : text) {
                    result.chars.emplace_back(std::tolower(c));
                    result.wordStart.emplace_back(!previousAlnum && std::isalnum(c));
                    previousAlnum = std::isalnum(c);
                }
                return result;
            }

            bool     previousAlnum = false;
            gunichar previous      = 0;
            for (const char* p = text.c_str(); *p; p = g_utf8_next_char(p)) {
                const gunichar C     = g_utf8_get_char(p);
                const bool     ALNUM = g_unichar_isalnum(C);
                const bool     CAMEL = previous && g_unichar_islower(previous) && g_unichar_isupper(C);
                result.chars.emplace_back(foldChar(C));
                result.wordStart.emplace_back(ALNUM && (!previousAlnum || CAMEL));
                previousAlnum = ALNUM;
                previous      = C;
            }
            return result;
        }

        std::vector<std::vector<gunichar>> queryTokens(const std::string& query) {
            std::vector<std::vector<gunichar>> tokens;
            std::vector<gunichar>              current;
            const auto                         FOLDED = fold(query);
            for (const auto C : FOLDED.chars) {
                if (g_unichar_isspace(C)) {
                    if (!current.empty())
                        tokens.emplace_back(std::move(current));
                    current.clear();
                    continue;
                }
                current.emplace_back(C);
            }
            if (!current.empty())
                tokens.emplace_back(std::move(current));
            return tokens;
        }

        // A word matches only where its letters appear together, in order
        // ("fas" finds FAStfetch, never Free imAgeS). Among those, the start
        // of the text beats the start of a word beats the middle of one, and
        // earlier beats later. Returns -1 when the token does not match.
        int scoreToken(const SFoldedText& field, const std::vector<gunichar>& token, std::vector<size_t>& positions) {
            const size_t N = field.chars.size();
            const size_t M = token.size();
            if (M == 0)
                return 0;
            if (M > N)
                return -1;

            int    best      = -1;
            size_t bestStart = 0;
            for (size_t start = 0; start + M <= N; ++start) {
                if (!std::equal(token.begin(), token.end(), field.chars.begin() + start))
                    continue;
                int score = 100 + sc<int>(M) * 14 - sc<int>(std::min<size_t>(start, 40)) / 2;
                if (start == 0)
                    score += 70;
                else if (field.wordStart[start])
                    score += 45;
                if (score > best) {
                    best      = score;
                    bestStart = start;
                }
            }
            if (best < 0)
                return -1;

            positions.clear();
            for (size_t i = 0; i < M; ++i)
                positions.emplace_back(bestStart + i);
            return best;
        }

        struct SWindowFields {
            std::string title;
            std::string klass;
            SFoldedText foldedTitle;
            SFoldedText foldedClass;
            SFoldedText foldedCategory;
        };

        std::unordered_map<uint64_t, SWindowFields> g_fieldCache;

        const SWindowFields& fieldsFor(const PHLWINDOW& window) {
            const auto TITLE = displayTitle(window);
            const auto CLASS = displayClass(window);
            auto&      entry = g_fieldCache[window->m_stableID];
            if (entry.title != TITLE || entry.klass != CLASS || entry.foldedTitle.chars.empty()) {
                entry.title          = TITLE;
                entry.klass          = CLASS;
                entry.foldedTitle    = fold(TITLE);
                entry.foldedClass    = fold(CLASS);
                entry.foldedCategory = fold(std::string{categoryName(canvasWindowCategory(window))});
            }
            return entry;
        }

        void ensureSelectionVisible() {
            const int     ROWS  = std::max(1, sc<int>(Tuning::number("navigator:rows")));
            const int     COUNT = sc<int>(g_state.results.size());
            g_state.selected    = COUNT == 0 ? 0 : std::clamp(g_state.selected, 0, COUNT - 1);
            if (g_state.selected < g_state.listOffset)
                g_state.listOffset = g_state.selected;
            if (g_state.selected >= g_state.listOffset + ROWS)
                g_state.listOffset = g_state.selected - ROWS + 1;
            g_state.listOffset = std::clamp(g_state.listOffset, 0, std::max(0, COUNT - ROWS));
        }

        int tick(void*) {
            if (!g_state.open) {
                return 0;
            }
            g_state.caretVisible = !g_state.caretVisible;
            if (!g_state.notice.empty() && Time::steadyNow() >= g_state.noticeUntil)
                g_state.notice.clear();
            touch();
            if (g_damage)
                g_damage();
            if (g_tickTimer)
                wl_event_source_timer_update(g_tickTimer, CARET_BLINK_MS);
            return 0;
        }

        int repeatTick(void*) {
            if (!g_repeatKey.keycode || !g_repeatHandler)
                return 0;
            const auto KEY     = g_repeatKey;
            const auto HANDLER = g_repeatHandler;
            if (g_repeatTimer)
                wl_event_source_timer_update(g_repeatTimer, g_repeatIntervalMs);
            HANDLER(KEY);
            return 0;
        }
    }

    SState& state() {
        return g_state;
    }

    void touch() {
        ++g_state.revision;
    }

    bool isOpen() {
        return g_state.open;
    }

    void setDamageCallback(std::function<void()> callback) {
        g_damage = std::move(callback);
    }

    void showNotice(const std::string& text) {
        g_state.notice      = text;
        g_state.noticeUntil = Time::steadyNow() + std::chrono::milliseconds(1800);
        touch();
    }

    void begin(const PHLWINDOW& focused) {
        stopRepeat();
        const auto REVISION  = g_state.revision;
        g_state              = SState{};
        g_state.open         = true;
        g_state.returnWindow = overviewWindow(focused);
        g_state.openedAt     = Time::steadyNow();
        g_state.revision     = REVISION + 1;

        // Freeze the recency order for the session: browsing moves keyboard
        // focus around, and the list must not reshuffle under the user.
        auto windows = candidates();
        std::ranges::stable_sort(windows, [](const PHLWINDOW& a, const PHLWINDOW& b) {
            const auto A = g_focusStamps.contains(a->m_stableID) ? g_focusStamps.at(a->m_stableID) : 0;
            const auto B = g_focusStamps.contains(b->m_stableID) ? g_focusStamps.at(b->m_stableID) : 0;
            return A > B;
        });
        if (const auto RETURN = g_state.returnWindow.lock()) {
            if (const auto IT = std::ranges::find(windows, RETURN); IT != windows.end())
                std::rotate(windows.begin(), IT, IT + 1);
        }
        g_sessionOrder.clear();
        for (const auto& window : windows)
            g_sessionOrder.emplace_back(window);

        rebuildResults(true);

        if (!g_tickTimer && g_pCompositor)
            g_tickTimer = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, tick, nullptr);
        if (g_tickTimer)
            wl_event_source_timer_update(g_tickTimer, CARET_BLINK_MS);
    }

    void end() {
        if (!g_state.open)
            return;
        stopRepeat();
        g_state.open = false;
        g_state.query.clear();
        g_state.results.clear();
        g_state.helpOpen     = false;
        g_state.listRevealed = false;
        g_state.tuner        = false;
        g_sessionOrder.clear();
        touch();
        if (g_tickTimer)
            wl_event_source_timer_update(g_tickTimer, 0);
    }

    std::string displayTitle(const PHLWINDOW& window) {
        if (!window)
            return {};
        if (!window->m_title.empty())
            return window->m_title;
        if (!window->m_initialTitle.empty())
            return window->m_initialTitle;
        return displayClass(window);
    }

    std::string displayClass(const PHLWINDOW& window) {
        if (!window)
            return {};
        if (!window->m_class.empty())
            return window->m_class;
        if (!window->m_initialClass.empty())
            return window->m_initialClass;
        return window->m_isX11 ? "xwayland" : "window";
    }

    std::string_view categoryCode(ECanvasWindowCategory category) {
        switch (category) {
            case ECanvasWindowCategory::TERMINAL: return "TM";
            case ECanvasWindowCategory::BROWSER: return "WB";
            case ECanvasWindowCategory::COMMUNICATION: return "CH";
            case ECanvasWindowCategory::DEVELOPMENT: return "DV";
            case ECanvasWindowCategory::FILES_NOTES: return "FL";
            case ECanvasWindowCategory::MEDIA: return "MD";
            default: return "AP";
        }
    }

    std::string_view categoryName(ECanvasWindowCategory category) {
        switch (category) {
            case ECanvasWindowCategory::TERMINAL: return "terminal";
            case ECanvasWindowCategory::BROWSER: return "browser web";
            case ECanvasWindowCategory::COMMUNICATION: return "chat mail";
            case ECanvasWindowCategory::DEVELOPMENT: return "code editor";
            case ECanvasWindowCategory::FILES_NOTES: return "files notes";
            case ECanvasWindowCategory::MEDIA: return "media music video";
            default: return "app";
        }
    }

    bool appendText(const std::string& utf8) {
        if (utf8.empty())
            return false;
        // Leading spaces carry no meaning and would only hide the placeholder.
        if (g_state.query.empty() && std::ranges::all_of(utf8, [](char c) { return c == ' '; }))
            return false;
        g_state.query += utf8;
        touch();
        return true;
    }

    bool popChar() {
        if (g_state.query.empty())
            return false;
        const char* start = g_state.query.c_str();
        const char* last  = g_utf8_find_prev_char(start, start + g_state.query.size());
        g_state.query.resize(last ? sc<size_t>(last - start) : 0);
        touch();
        return true;
    }

    bool popWord() {
        if (g_state.query.empty())
            return false;
        while (!g_state.query.empty() && g_state.query.back() == ' ')
            g_state.query.pop_back();
        while (!g_state.query.empty() && g_state.query.back() != ' ')
            popChar();
        touch();
        return true;
    }

    bool clearQuery() {
        if (g_state.query.empty())
            return false;
        g_state.query.clear();
        touch();
        return true;
    }

    bool queryActive() {
        return g_state.open && !queryTokens(g_state.query).empty();
    }

    bool listVisible() {
        return g_state.open && (queryActive() || g_state.listRevealed);
    }

    void rebuildResults(bool resetSelection) {
        const auto PREVIOUS = resetSelection ? PHLWINDOW{} : selectedWindow();
        const auto TOKENS   = queryTokens(g_state.query);

        // Session order first (recency), then any window that appeared after
        // the navigator opened.
        std::vector<PHLWINDOW>          ordered;
        std::unordered_set<const void*> seen;
        for (const auto& ref : g_sessionOrder) {
            const auto WINDOW = ref.lock();
            if (isCandidate(WINDOW) && seen.emplace(WINDOW.get()).second)
                ordered.emplace_back(WINDOW);
        }
        for (const auto& window : candidates()) {
            if (seen.emplace(window.get()).second)
                ordered.emplace_back(window);
        }

        std::vector<SResult> results;
        results.reserve(ordered.size());
        const auto RETURN = g_state.returnWindow.lock();
        for (size_t rank = 0; rank < ordered.size(); ++rank) {
            const auto& WINDOW = ordered[rank];
            SResult     result{.window = WINDOW};

            if (TOKENS.empty()) {
                result.score = -sc<int>(rank);
                results.emplace_back(std::move(result));
                continue;
            }

            const auto& FIELDS = fieldsFor(WINDOW);
            bool        matched = true;
            int         total   = 0;
            std::vector<size_t> scratch;
            for (const auto& token : TOKENS) {
                std::vector<size_t> titlePositions;
                std::vector<size_t> classPositions;
                const int TITLE    = scoreToken(FIELDS.foldedTitle, token, titlePositions);
                const int CLASS    = scoreToken(FIELDS.foldedClass, token, classPositions);
                const int CATEGORY = scoreToken(FIELDS.foldedCategory, token, scratch);
                const int CLASSW   = CLASS < 0 ? -1 : CLASS * 85 / 100;
                const int CATW     = CATEGORY < 0 ? -1 : CATEGORY * 55 / 100;
                const int BEST     = std::max({TITLE, CLASSW, CATW});
                if (BEST < 0) {
                    matched = false;
                    break;
                }
                total += BEST;
                if (TITLE >= 0)
                    result.titleMatches.insert(result.titleMatches.end(), titlePositions.begin(), titlePositions.end());
                if (CLASS >= 0)
                    result.classMatches.insert(result.classMatches.end(), classPositions.begin(), classPositions.end());
            }
            if (!matched)
                continue;

            // Recency breaks near-ties; the window you started from yields
            // slightly so "term" in a terminal finds the *other* terminal.
            total += std::max(0, 36 - sc<int>(rank) * 4);
            if (WINDOW == RETURN)
                total -= 24;
            result.score = total;
            for (auto* matches : {&result.titleMatches, &result.classMatches}) {
                std::ranges::sort(*matches);
                matches->erase(std::unique(matches->begin(), matches->end()), matches->end());
            }
            results.emplace_back(std::move(result));
        }

        std::ranges::stable_sort(results, [](const SResult& a, const SResult& b) { return a.score > b.score; });
        g_state.results = std::move(results);

        g_state.selected = 0;
        if (PREVIOUS) {
            for (size_t i = 0; i < g_state.results.size(); ++i) {
                if (g_state.results[i].window.lock() == PREVIOUS) {
                    g_state.selected = sc<int>(i);
                    break;
                }
            }
        }
        if (resetSelection)
            g_state.listOffset = 0;
        ensureSelectionVisible();
        touch();
    }

    PHLWINDOW selectedWindow() {
        if (g_state.selected < 0 || g_state.selected >= sc<int>(g_state.results.size()))
            return nullptr;
        const auto WINDOW = g_state.results[g_state.selected].window.lock();
        return isCandidate(WINDOW) ? WINDOW : nullptr;
    }

    bool selectWindow(const PHLWINDOW& window) {
        const auto TARGET = overviewWindow(window);
        for (size_t i = 0; i < g_state.results.size(); ++i) {
            if (g_state.results[i].window.lock() != TARGET)
                continue;
            if (g_state.selected != sc<int>(i)) {
                g_state.selected = sc<int>(i);
                ensureSelectionVisible();
                touch();
            }
            return true;
        }
        return false;
    }

    bool moveSelection(int delta) {
        const int COUNT = sc<int>(g_state.results.size());
        if (COUNT == 0)
            return false;
        g_state.selected     = ((g_state.selected + delta) % COUNT + COUNT) % COUNT;
        g_state.listRevealed = true;
        ensureSelectionVisible();
        touch();
        return true;
    }

    bool isMatch(const PHLWINDOW& window) {
        if (!queryActive())
            return true;
        const auto TARGET = overviewWindow(window);
        return std::ranges::any_of(g_state.results, [&TARGET](const SResult& result) { return result.window.lock() == TARGET; });
    }

    int candidateCount() {
        return sc<int>(candidates().size());
    }

    void noteFocus(const PHLWINDOW& window, bool force) {
        const auto TARGET = overviewWindow(window);
        if (!TARGET || (g_state.open && !force))
            return;
        g_focusStamps[TARGET->m_stableID] = ++g_focusCounter;
    }

    void forget(const PHLWINDOW& window) {
        if (!window)
            return;
        g_focusStamps.erase(window->m_stableID);
        g_fieldCache.erase(window->m_stableID);
        if (!g_state.open)
            return;
        const auto BEFORE = g_state.results.size();
        std::erase_if(g_state.results, [&window](const SResult& result) {
            const auto CANDIDATE = result.window.lock();
            return !CANDIDATE || CANDIDATE == window;
        });
        if (BEFORE != g_state.results.size()) {
            ensureSelectionVisible();
            touch();
        }
    }

    void startRepeat(const SRepeatKey& key, int delayMs, int rateHz, std::function<void(const SRepeatKey&)> handler) {
        if (!g_pCompositor || delayMs <= 0 || rateHz <= 0)
            return;
        g_repeatKey        = key;
        g_repeatHandler    = std::move(handler);
        g_repeatIntervalMs = std::max(8, 1000 / rateHz);
        if (!g_repeatTimer)
            g_repeatTimer = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, repeatTick, nullptr);
        if (g_repeatTimer)
            wl_event_source_timer_update(g_repeatTimer, delayMs);
    }

    void stopRepeat(uint32_t keycode) {
        if (keycode && g_repeatKey.keycode != keycode)
            return;
        g_repeatKey = SRepeatKey{};
        g_repeatHandler = nullptr;
        if (g_repeatTimer)
            wl_event_source_timer_update(g_repeatTimer, 0);
    }

    void markConsumed(uint32_t keycode) {
        g_consumedKeys.emplace(keycode);
    }

    bool takeConsumed(uint32_t keycode) {
        return g_consumedKeys.erase(keycode) > 0;
    }

    // ---- live tuner -------------------------------------------------------------

    namespace {
        int g_lastTunerParam = 0; // the tuner reopens where it was left

        void ensureTunerVisible() {
            const int COUNT       = sc<int>(g_state.tunerResults.size());
            g_state.tunerSelected = COUNT == 0 ? 0 : std::clamp(g_state.tunerSelected, 0, COUNT - 1);
            if (g_state.tunerSelected < g_state.tunerOffset)
                g_state.tunerOffset = g_state.tunerSelected;
            if (g_state.tunerSelected >= g_state.tunerOffset + TUNER_ROWS)
                g_state.tunerOffset = g_state.tunerSelected - TUNER_ROWS + 1;
            g_state.tunerOffset = std::clamp(g_state.tunerOffset, 0, std::max(0, COUNT - TUNER_ROWS));
        }

        // Every word of the filter must appear in the setting's name, section
        // or key; names count most, so "scale" finds Scale before
        // distortion:edge_scale.
        void refreshTuner(int keepParam, bool bestHit) {
            const auto& PARAMS = Tuning::params();
            const auto  TOKENS = queryTokens(g_state.tunerQuery);
            const auto  contains = [](const std::vector<gunichar>& hay, const std::vector<gunichar>& needle) {
                return !std::ranges::search(hay, needle).empty();
            };
            std::vector<std::pair<int, int>> scored; // score, index
            for (int i = 0; i < sc<int>(PARAMS.size()); ++i) {
                const auto LABEL   = fold(PARAMS[i].label);
                const auto SECTION = fold(PARAMS[i].section).chars;
                const auto KEY     = fold(PARAMS[i].key).chars;
                int        score   = 0;
                bool       all     = true;
                for (const auto& token : TOKENS) {
                    const auto AT = std::ranges::search(LABEL.chars, token);
                    if (!AT.empty()) {
                        const auto POS = AT.begin() - LABEL.chars.begin();
                        score += LABEL.wordStart[POS] ? (POS == 0 ? 8 : 6) : 4;
                    } else if (contains(SECTION, token))
                        score += 2;
                    else if (contains(KEY, token))
                        score += 1;
                    else {
                        all = false;
                        break;
                    }
                }
                if (all)
                    scored.emplace_back(score, i);
            }
            std::ranges::stable_sort(scored, [](const auto& a, const auto& b) { return a.first > b.first; });
            g_state.tunerResults.clear();
            for (const auto& [score, index] : scored)
                g_state.tunerResults.push_back(index);

            const auto IT         = std::ranges::find(g_state.tunerResults, keepParam);
            g_state.tunerSelected = bestHit || IT == g_state.tunerResults.end() ? 0 : sc<int>(IT - g_state.tunerResults.begin());
            if (bestHit)
                g_state.tunerOffset = 0;
            ensureTunerVisible();
            touch();
        }

        const Tuning::SParam* selectedParam() {
            if (!g_state.tuner || g_state.tunerResults.empty())
                return nullptr;
            const int INDEX  = g_state.tunerResults[std::clamp(g_state.tunerSelected, 0, sc<int>(g_state.tunerResults.size()) - 1)];
            g_lastTunerParam = INDEX;
            return &Tuning::params()[INDEX];
        }
    }

    void openTuner() {
        if (!g_state.open)
            return;
        g_state.tuner    = true;
        g_state.helpOpen = false;
        g_state.tunerQuery.clear();
        g_state.tunerOffset = 0;
        Tuning::beginSession();
        refreshTuner(g_lastTunerParam, false);
    }

    void closeTuner() {
        if (!g_state.tuner)
            return;
        g_state.tuner = false;
        touch();
    }

    bool tunerAppend(const std::string& utf8) {
        if (utf8.empty() || (g_state.tunerQuery.empty() && std::ranges::all_of(utf8, [](char c) { return c == ' '; })))
            return false;
        g_state.tunerQuery += utf8;
        refreshTuner(0, true);
        return true;
    }

    bool tunerPop() {
        if (g_state.tunerQuery.empty())
            return false;
        const char* start = g_state.tunerQuery.c_str();
        const char* last  = g_utf8_find_prev_char(start, start + g_state.tunerQuery.size());
        g_state.tunerQuery.resize(last ? sc<size_t>(last - start) : 0);
        refreshTuner(0, !g_state.tunerQuery.empty());
        return true;
    }

    bool tunerClear() {
        if (g_state.tunerQuery.empty())
            return false;
        const auto* KEEP = selectedParam();
        g_state.tunerQuery.clear();
        refreshTuner(KEEP ? sc<int>(KEEP - Tuning::params().data()) : 0, false);
        return true;
    }

    bool tunerMove(int delta) {
        const int COUNT = sc<int>(g_state.tunerResults.size());
        if (!g_state.tuner || COUNT == 0)
            return false;
        g_state.tunerSelected = ((g_state.tunerSelected + delta) % COUNT + COUNT) % COUNT;
        ensureTunerVisible();
        selectedParam();
        touch();
        return true;
    }

    bool tunerAdjust(double steps) {
        const auto* PARAM = selectedParam();
        if (!PARAM || !Tuning::step(*PARAM, steps))
            return false;
        touch();
        return true;
    }

    bool tunerRevert() {
        const auto* PARAM = selectedParam();
        if (!PARAM || !Tuning::revert(*PARAM))
            return false;
        touch();
        return true;
    }

    void shutdown() {
        stopRepeat();
        if (g_repeatTimer)
            wl_event_source_remove(g_repeatTimer);
        g_repeatTimer = nullptr;
        if (g_tickTimer)
            wl_event_source_remove(g_tickTimer);
        g_tickTimer = nullptr;
        g_damage    = nullptr;
        g_state     = SState{};
        g_sessionOrder.clear();
        g_fieldCache.clear();
        g_consumedKeys.clear();
    }
}
