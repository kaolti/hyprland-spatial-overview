#pragma once

#include "globals.hpp"

#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <cairo/cairo.h>

// App icons for canvas windows, resolved the way launchers do: the window's
// app id is matched to a .desktop entry (by StartupWMClass, file name, or the
// URL host of an Omarchy web app), and its Icon= is looked up in the current
// icon theme, the themes it inherits, hicolor, and pixmaps.
namespace SpatialOverview::Icons {

    // A cairo image of roughly `size` device pixels (SVGs are rendered at that
    // size), or nullptr when the app has no findable icon. Owned by the cache.
    cairo_surface_t* forWindow(const PHLWINDOW& window, int size);

    // Resolves (and caches) the icon file for a window without loading it,
    // so the file-system scan happens before an animation rather than in it.
    void             resolve(const PHLWINDOW& window);

    void             shutdown();
}
