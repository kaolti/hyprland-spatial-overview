#define WLR_USE_UNSTABLE

#include "Icons.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <hyprgraphics/image/Image.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

namespace SpatialOverview::Icons {
    namespace {
        namespace fs = std::filesystem;

        struct SIndex {
            std::unordered_map<std::string, std::string> byWMClass;
            std::unordered_map<std::string, std::string> byFile;
            std::unordered_map<std::string, std::string> byHost;
            std::unordered_map<std::string, std::string> byName;
        };

        std::optional<SIndex>                                          g_index;
        std::vector<std::string>                                       g_themes;
        std::unordered_map<std::string, std::unique_ptr<Hyprgraphics::CImage>> g_images;   // path@size
        std::unordered_map<std::string, std::string>                   g_paths;            // class -> icon path ("" = none)

        std::string lower(std::string value) {
            std::ranges::transform(value, value.begin(), [](unsigned char c) { return sc<char>(std::tolower(c)); });
            return value;
        }

        std::string env(const char* name, const std::string& fallback) {
            const char* value = std::getenv(name);
            return value && *value ? std::string{value} : fallback;
        }

        std::string home() {
            return env("HOME", "/tmp");
        }

        std::vector<fs::path> dataDirs() {
            std::vector<fs::path> dirs{env("XDG_DATA_HOME", home() + "/.local/share")};
            std::string           rest = env("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
            size_t                start = 0;
            while (start <= rest.size()) {
                const auto END = rest.find(':', start);
                const auto DIR = rest.substr(start, END == std::string::npos ? std::string::npos : END - start);
                if (!DIR.empty() && std::ranges::find(dirs, fs::path{DIR}) == dirs.end())
                    dirs.emplace_back(DIR);
                if (END == std::string::npos)
                    break;
                start = END + 1;
            }
            for (const auto& flatpak : {home() + "/.local/share/flatpak/exports/share", std::string{"/var/lib/flatpak/exports/share"}}) {
                if (std::ranges::find(dirs, fs::path{flatpak}) == dirs.end())
                    dirs.emplace_back(flatpak);
            }
            return dirs;
        }

        // Earlier data dirs win, so a user's own .desktop overrides the system's.
        void indexDesktopFile(const fs::path& path, SIndex& index) {
            std::ifstream in{path};
            std::string   line, icon, wmClass, exec, name;
            bool          inEntry = false;
            while (std::getline(in, line)) {
                if (line.starts_with('[')) {
                    inEntry = line.starts_with("[Desktop Entry]");
                    continue;
                }
                if (!inEntry)
                    continue;
                if (line.starts_with("Icon="))
                    icon = line.substr(5);
                else if (line.starts_with("StartupWMClass="))
                    wmClass = line.substr(15);
                else if (line.starts_with("Exec=") && exec.empty())
                    exec = line.substr(5);
                else if (line.starts_with("Name="))
                    name = line.substr(5);
            }
            if (icon.empty())
                return;

            if (!wmClass.empty())
                index.byWMClass.try_emplace(lower(wmClass), icon);
            index.byFile.try_emplace(lower(path.stem().string()), icon);
            if (!name.empty())
                index.byName.try_emplace(lower(name), icon);

            // Omarchy web apps run as chromium --app=URL; their windows are
            // classed chrome-<host>__-Default, so remember the URL host.
            for (const auto* SCHEME : {"https://", "http://"}) {
                const auto AT = exec.find(SCHEME);
                if (AT == std::string::npos)
                    continue;
                const auto BEGIN = AT + std::string_view{SCHEME}.size();
                const auto END   = exec.find_first_of("/ \"'?", BEGIN);
                index.byHost.try_emplace(lower(exec.substr(BEGIN, END == std::string::npos ? std::string::npos : END - BEGIN)), icon);
                break;
            }
        }

        const SIndex& index() {
            if (g_index)
                return *g_index;
            g_index.emplace();
            std::error_code error;
            for (const auto& dir : dataDirs()) {
                const auto APPS = dir / "applications";
                if (!fs::is_directory(APPS, error))
                    continue;
                for (fs::recursive_directory_iterator it{APPS, fs::directory_options::skip_permission_denied, error}, end; it != end; it.increment(error)) {
                    if (error)
                        break;
                    if (it->path().extension() == ".desktop")
                        indexDesktopFile(it->path(), *g_index);
                }
            }
            return *g_index;
        }

        std::string firstLine(const fs::path& path) {
            std::ifstream in{path};
            std::string   line;
            std::getline(in, line);
            while (!line.empty() && std::isspace(sc<unsigned char>(line.back())))
                line.pop_back();
            return line;
        }

        std::vector<fs::path> iconRoots() {
            std::vector<fs::path> roots{home() + "/.icons"};
            for (const auto& dir : dataDirs())
                roots.emplace_back(dir / "icons");
            return roots;
        }

        void addThemeChain(const std::string& theme, int depth = 0) {
            if (theme.empty() || depth > 6 || std::ranges::find(g_themes, theme) != g_themes.end())
                return;
            g_themes.emplace_back(theme);
            std::error_code error;
            for (const auto& root : iconRoots()) {
                const auto INDEX = root / theme / "index.theme";
                if (!fs::is_regular_file(INDEX, error))
                    continue;
                std::ifstream in{INDEX};
                std::string   line;
                while (std::getline(in, line)) {
                    if (!line.starts_with("Inherits="))
                        continue;
                    std::string rest = line.substr(9);
                    size_t      start = 0;
                    while (start < rest.size()) {
                        const auto END = rest.find(',', start);
                        addThemeChain(rest.substr(start, END == std::string::npos ? std::string::npos : END - start), depth + 1);
                        if (END == std::string::npos)
                            break;
                        start = END + 1;
                    }
                    break;
                }
                break;
            }
        }

        const std::vector<std::string>& themes() {
            if (!g_themes.empty())
                return g_themes;
            // Omarchy records the icon theme it hands to GNOME; GTK's own
            // settings file is the next best source without asking dconf.
            std::string configured = firstLine(home() + "/.local/state/omarchy/current/theme/icons.theme");
            if (configured.empty()) {
                for (const auto* FILE : {"/.config/gtk-3.0/settings.ini", "/.config/gtk-4.0/settings.ini"}) {
                    std::ifstream in{home() + FILE};
                    std::string   line;
                    while (std::getline(in, line)) {
                        if (line.starts_with("gtk-icon-theme-name=")) {
                            configured = line.substr(20);
                            break;
                        }
                    }
                    if (!configured.empty())
                        break;
                }
            }
            addThemeChain(configured);
            addThemeChain("hicolor");
            return g_themes;
        }

        std::string resolveIconName(const std::string& name) {
            std::error_code error;
            if (name.empty())
                return {};
            if (name.starts_with('/'))
                return fs::is_regular_file(name, error) ? name : std::string{};

            static constexpr const char* SUBDIRS[] = {"scalable/apps", "512x512/apps", "256x256@2x/apps", "256x256/apps", "128x128/apps", "96x96/apps",
                                                      "64x64/apps",    "48x48@2x/apps", "48x48/apps",     "32x32@2x/apps", "32x32/apps"};
            for (const auto& theme : themes()) {
                for (const auto& root : iconRoots()) {
                    const auto BASE = root / theme;
                    if (!fs::is_directory(BASE, error))
                        continue;
                    for (const auto* SUBDIR : SUBDIRS) {
                        for (const auto* EXTENSION : {".svg", ".png"}) {
                            const auto PATH = BASE / SUBDIR / (name + EXTENSION);
                            if (fs::is_regular_file(PATH, error))
                                return PATH.string();
                        }
                    }
                }
            }
            for (const auto* EXTENSION : {".svg", ".png"}) {
                const auto PATH = fs::path{"/usr/share/pixmaps"} / (name + EXTENSION);
                if (fs::is_regular_file(PATH, error))
                    return PATH.string();
            }
            return {};
        }

        std::string iconNameFor(const std::string& appId) {
            const auto& INDEX = index();
            const auto  KEY   = lower(appId);
            for (const auto* MAP : {&INDEX.byWMClass, &INDEX.byFile}) {
                if (const auto IT = MAP->find(KEY); IT != MAP->end())
                    return IT->second;
            }
            // Reverse-DNS app ids ("org.gnome.Nautilus") often ship a
            // desktop file under just their last component, and vice versa.
            if (const auto DOT = KEY.rfind('.'); DOT != std::string::npos) {
                if (const auto IT = INDEX.byFile.find(KEY.substr(DOT + 1)); IT != INDEX.byFile.end())
                    return IT->second;
            }
            if (KEY.starts_with("chrome-")) {
                auto host = KEY.substr(7);
                if (const auto END = host.find("__"); END != std::string::npos)
                    host = host.substr(0, END);
                for (std::string candidate = host; !candidate.empty();) {
                    if (const auto IT = INDEX.byHost.find(candidate); IT != INDEX.byHost.end())
                        return IT->second;
                    const auto NEXT = candidate.find('_'); // chrome-host_path__: drop the path
                    if (NEXT == std::string::npos)
                        break;
                    candidate = candidate.substr(0, NEXT);
                }
            }
            if (const auto IT = INDEX.byName.find(KEY); IT != INDEX.byName.end())
                return IT->second;
            return appId; // many apps name their icon after themselves
        }
    }

    namespace {
        const std::string* pathFor(const PHLWINDOW& window) {
            if (!window)
                return nullptr;
            const auto APPID = window->m_class.empty() ? window->m_initialClass : window->m_class;
            if (APPID.empty())
                return nullptr;
            auto pathIt = g_paths.find(APPID);
            if (pathIt == g_paths.end()) {
                auto path = resolveIconName(iconNameFor(APPID));
                if (path.empty())
                    path = resolveIconName(lower(APPID));
                pathIt = g_paths.emplace(APPID, path).first;
            }
            return pathIt->second.empty() ? nullptr : &pathIt->second;
        }
    }

    void resolve(const PHLWINDOW& window) {
        pathFor(window);
    }

    cairo_surface_t* forWindow(const PHLWINDOW& window, int size) {
        if (size < 4)
            return nullptr;
        const auto* PATH = pathFor(window);
        if (!PATH)
            return nullptr;

        // SVGs render at the requested size; bitmaps load once and are scaled
        // by the caller.
        const bool SVG   = PATH->ends_with(".svg");
        const auto KEY   = SVG ? *PATH + "@" + std::to_string(size) : *PATH;
        auto       image = g_images.find(KEY);
        if (image == g_images.end()) {
            if (g_images.size() > 256)
                g_images.clear();
            auto loaded = std::make_unique<Hyprgraphics::CImage>(*PATH, Hyprutils::Math::Vector2D{sc<double>(size), sc<double>(size)});
            if (!loaded->success() || !loaded->cairoSurface() || !loaded->cairoSurface()->cairo())
                loaded.reset();
            image = g_images.emplace(KEY, std::move(loaded)).first;
        }
        return image->second ? image->second->cairoSurface()->cairo() : nullptr;
    }

    void shutdown() {
        g_images.clear();
        g_paths.clear();
        g_index.reset();
        g_themes.clear();
    }
}
