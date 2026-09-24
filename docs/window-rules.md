# How windows behave on the canvas

The canvas replaces tiling and workspaces with one plane of floating windows,
so every Hyprland and Omarchy key that moves, resizes or restacks windows
needs a meaning there. These are the rules, and the nested tests that check
them (`tests/*-nested.py`; most take `NESTED_SCALE=1.25` for scaled screens).

## Screens

- **The screens are one desk.** They show the parts of the canvas next to each
  other, as the monitors are arranged, and move together: panning, zooming or
  landing on a window on any screen moves all of them. A window can sit across
  the seam, half on each screen; it is never shown twice. Dragging it from one
  screen to the other is one continuous move, and it stays where it is dropped.
  `canvas.linked_screens = false` gives each screen its own camera instead.
  *linked, multimonitor (independent cameras)*
- **A window belongs to the screen that shows most of it.** Its Hyprland
  workspace follows (it never moves on the canvas for it), so focus-by-monitor,
  one fullscreen window per screen and where an X11 app thinks it is all hold.
  *fullscreen-cases*
- **Both screens are in the same view**: 100% or zoomed out, never one of each.
  Anything that lands at 100% (a search, SUPER + T, a place, fullscreen) brings
  every screen back, following the screen you acted on. *keys, fill, places,
  fullscreen-cases*

## Focus

- **The canvas never moves focus on its own.** Rebuilding (a window changed
  workspace, a screen came back) keeps focus where it is.
- **A window already on screen stays where you see it** when something outside
  the canvas focuses it (a dock, a launcher, CTRL + ALT + TAB); one that is not
  on screen brings the camera to it. *linked, keys*
- **Landing on a window brings it in front.** *keys*
- **Focus follows the pointer between screens**, also onto a screen given to a
  fullscreen window. *fullscreen-cases*
- **Arrows go where they point.** SUPER + arrows at 100% and the arrows in the
  zoomed-out view pick the nearest window that way, from where the windows are
  now (after a drag or a tidy-up too), across both screens. It has to reach
  further that way than the current one with both edges, and lie that way
  rather than off to the side (some of it within 45°). A window in line with
  the current one (the same row or column) comes first; going up or down, a
  window off the line wins if it is wholly nearer. With nothing that way the
  selection stays. *arrows*

## Fill (SUPER + T, SUPER + ALT + F)

- The window fills the screen it is on: the screen minus what bars and docks
  reserve, with the gap tiled windows keep (`general:gaps_out`) all around and
  its border inside it. It comes in front. A dock that reserves no space leaves
  none free.
- Again: back exactly where and how big it was. Moved meanwhile: its old size,
  around where it is now. Resized meanwhile: it fills again.
- From the zoomed-out view it lands at 100% first. *fill*

## Fullscreen (SUPER + F, or the app)

- The window fills the screen it is on; only that screen's canvas steps aside.
  The other screens keep theirs, and nothing else moves. The bar is hidden
  under it and keys go to it. It grows out of where it was drawn and shrinks
  back into its place. *fullscreen, fullscreen-cases*
- **Each screen can have its own fullscreen window.** SUPER + F toggles the
  focused one only.
- **Games (Wine / Proton):** "windowed fullscreen" (a borderless window covering
  its monitor) becomes fullscreen on that monitor; going back to windowed gets
  the size the game asked for; flashing for attention (combat) does not end
  fullscreen; while the game hides its cursor (mouse-look) the pointer stays on
  its screen. X11 apps keep their last on-screen position while zoomed out or
  off screen, so their monitor and resolution list stay put. *fullscreen*
- **From the zoomed-out view**, SUPER + F brings every screen back to 100% and
  the window goes fullscreen. Leaving fullscreen while another screen is zoomed
  out: this one joins it. *fullscreen-cases*
- **SUPER + CTRL + G over a fullscreen window** takes the screen back: the
  window is on the canvas where it was, windowed; going back to it makes it
  fullscreen again, picking another window leaves it windowed. *fullscreen*

## Pin (SUPER + O)

- The window stays put on the screen, above the canvas, while the canvas moves
  (drawn every frame, clickable); again puts it back on the canvas where it
  then is. *keys*

## Places (the workspace keys)

Experimental and off by default (`canvas.places = true` turns them on); off,
the workspace keys do nothing on the canvas.

- SUPER + 1 … 0 go to a place on the canvas; SHIFT + SUPER + N takes the focused
  window there (same spot on the screen) and follows it; SHIFT + ALT + SUPER + N
  sends it without following. SUPER + TAB, SHIFT + SUPER + TAB and SUPER +
  scroll go to the place next door, empty or not; CTRL + SUPER + TAB goes back
  to the place you came from.
- A place is a view of all screens at 100%. At first they are a row, one
  screen-set apart, with the place numbered after the workspace you were on
  where you are; each remembers where you left its camera and the window that
  had focus. *places*
- **Where you are going shows on the minimap.** In the zoomed-out view the
  view glides to the place and stays zoomed out; at 100% the minimap shows
  while you travel, and fades away a moment after. *places*

## Other keys

| Keys | On the canvas |
| --- | --- |
| SUPER + minus / equal (and SHIFT, ALT, CTRL variants) | Resize the focused window, as Hyprland does. |
| CTRL + SUPER + F | "Tiled fullscreen": the app believes it is fullscreen and stays in its frame. |
| SUPER + G, SUPER + ALT + arrows (into a group) | Nothing: windows stack freely on the canvas, and a group made there crashes Hyprland when it quits. SUPER + ALT + G still takes a window out of a group. |
| ALT + TAB | Recent windows (the canvas switcher). |
| CTRL + ALT + TAB | Focus the next screen. |
| SUPER + J, P, L, Home, ALT + Home, SHIFT + ALT + SUPER + arrows | Nothing: they tile or move workspaces between monitors. |

Each is checked in every situation — the window on one screen, across the
seam, off screen, filled, and zoomed out — against the same rules: no other
window moves, the window stays floating and drawn, both screens stay in the
same view, zooming out and back changes nothing. *keys*

For scripts, `hyprctl spatialoverview` prints the canvases' state as JSON:
each screen's zoom and view, whether it is zoomed out, the windows that are
fullscreen on a screen of their own, the filled ones, and the window selected
in the zoomed-out view.

## Planned: a canvas for each screen

Linked screens make the screens one desk, so what the two show is always side
by side on the canvas. A second mode will give each screen a canvas of its
own, with one search across both.

- **Two modes.** `canvas.screens = "desk"` (as now) or `"separate"`, and a key
  (SUPER + CTRL + L) to flip between them live; windows stay where they are on
  the screen when you flip.
- **Each screen has its own canvas.** Every window belongs to one screen and is
  drawn only there. Each screen has its own camera, zoom and places: SUPER +
  1 … 0 act on the screen you are on.
- **One search across both.** SUPER + CTRL + G searches every window. `Enter`
  flies the camera of the screen that has the window to it and focuses it; the
  other screen stays put. `SHIFT + Enter` brings the window over to the screen
  you are on.
- **Moving a window to the other screen's canvas:** drag it across the seam
  and it lands on the other canvas where you drop it; a key does the same
  without the mouse.
- **New windows** open on the screen with the pointer.
- **The minimap** on each screen shows that screen's canvas.
- Fullscreen, fill, pin and the other rules above work as they do now, per
  screen.

The tests will cover both modes: the situations in *keys* run in each, and
flipping between the modes must leave every window where it is on the screen.
