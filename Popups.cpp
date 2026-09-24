#define WLR_USE_UNSTABLE

// Popup placement for canvas windows. Hyprland keeps a popup inside the
// monitor that holds its window's real position; on the canvas the window is
// drawn somewhere else (wherever the camera shows it), so menus flipped or
// slid away from their window. Here the popup is kept inside the screen as
// the canvas shows it (see canvasPopupConstraint in scrollOverview.cpp).
//
// This file only reaches into CPopup, whose positioner is private; standard
// headers come first so the access trick below never touches them.
#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/math/Math.hpp>

#define private public
#define protected public
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#undef protected
#undef private

std::optional<CBox> canvasPopupConstraint(const PHLWINDOW& window);

bool canvasRepositionPopup(Desktop::View::CPopup* popup) {
    if (!popup || !popup->m_resource || popup->m_windowOwner.expired())
        return false;
    const auto CONSTRAINT = canvasPopupConstraint(popup->m_windowOwner.lock());
    if (!CONSTRAINT)
        return false;
    popup->m_resource->applyPositioning(*CONSTRAINT, popup->t1ParentCoords());
    return true;
}

// Whether any popup of the window is fading in or out.
bool popupTreeFading(const PHLWINDOW& window) {
    if (!window || window->m_isX11 || !window->m_popupHead)
        return false;
    bool fading = false;
    window->m_popupHead->breadthfirst(
        [&fading](SP<Desktop::View::CPopup> popup, void*) {
            if (popup && popup->m_alpha.get(Desktop::View::POPUP_ALPHA_FADE)->isBeingAnimated())
                fading = true;
        },
        nullptr);
    return fading;
}
