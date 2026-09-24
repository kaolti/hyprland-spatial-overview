-- Spatial Overview: an infinite canvas for Hyprland 0.56+ (made on Omarchy).
--
--   SUPER + CTRL + G   the zoomed-out canvas; type to search, Enter to go
--   CTRL + ,           (in the canvas) tune the look live; F1 lists every key
--
-- Edit and run `hyprctl reload`, or tune live: the tuner saves to
-- spatialoverview-tuning.lua next to this file, which is applied last.

local loaded = false
for _, plugin in ipairs(hl.get_loaded_plugins()) do
  if plugin.name == "spatialoverview" then
    loaded = true
    break
  end
end
-- On the first pass Hyprland has only been told to load the plugin; it
-- parses this file again once the plugin is in. Without the plugin (say,
-- right after a Hyprland update, before a rebuild) nothing here applies.
if not loaded then
  return
end

-- Omarchy's o.bind shows bindings in its key guide; elsewhere, plain binds.
local bind = (o and o.bind) or function(keys, _description, action, opts)
  hl.bind(keys, action, opts)
end

hl.config({
  plugin = {
    spatialoverview = {
      -- Camera / tile geometry
      layout = "grid",
      scale = 0.36,
      workspace_gap = 0,
      grid = {
        columns = 3,
        stagger = 0.0,
      },

      -- Input. Middle-drag pans the world; click selects; dragging into empty
      -- grid territory places a floating window without creating a workspace.
      input = {
        pan_sensitivity = 1.0,
        drag_threshold = 10,
        touchpad_scroll_factor = 1.0,
        scroll_event_delay = 160,
      },

      -- Motion. Edit the `spatialOverviewEase` control points below to shape
      -- acceleration/deceleration without recompiling the plugin.
      animation = {
        enabled = true,
        speed = 8.0,
        bezier = "spatialOverviewEase",
      },

      -- Final overview-only lens pass. `strength` is signed, so negative
      -- values reverse the curvature.
      distortion = {
        enabled = true,
        strength = 0.14,
        edge_scale = 1.08,
        feather = 0.025,
        transition_power = 1.0,
        edge_blur = 0.0,        -- lens blur toward the edges, 0..1
        edge_blur_start = 0.45, -- where it begins: 0 center .. 1 corners
        vignette = 0.0,
        chromatic = 0.0,        -- red/blue fringe toward the edges
      },

      workspace_outline = {
        enabled = false,
        width = 2,
        drop_width = 4,
        rounding = 14,
        opacity = 0.34,
        active_opacity = 0.82,
        drop_opacity = 1.0,
        drop_fill_opacity = 0.12,
      },

      canvas = {
        enabled = true,
        desktop_mode = true,
        persistent = true,
        initial_zoom = 0.72,
        min_zoom = 0.15,
        max_zoom = 2.5,
        zoom_step = 0.12,
        auto_float = true,
        auto_place = true,
        placement_gap = 40,
        space_pan = false,
        direct_input = true,
        hover_focus = true,
        minimap_enabled = true,
        minimap_width = 240,
        minimap_height = 150,
        minimap_margin = 24,
        minimap_opacity = 0.72,
        arrange_context_grouping = true,
        arrange_size_similarity = 0.12,
        arrange_resize_limit = 0.15,
        grid_enabled = true,
        grid_size = 80,
        grid_width = 1,
        grid_opacity = 0.16,
        grid_style = 1, -- 0 lines, 1 dots
        grid_dot_size = 3.2,
        background_dim = 0.35,
        viewport_enabled = false,
        viewport_width = 3,
        viewport_rounding = 18,
        viewport_border_opacity = 0.90,
        viewport_outside_opacity = 0.26,
        float_on_drag = true,
        allow_window_overflow = true,
        snap_viewport_on_pan = true,
        commit_viewport_on_close = true,
        snap_enabled = true,
        remember_layout = true, -- windows keep their spots across restarts
      },

      -- Type-to-search palette in the zoomed-out canvas.
      navigator = {
        enabled = true,
        labels = true,          -- window titles on the map
        dim_unmatched = 0.7,    -- how far non-matching windows recede (0..1)
        pointer = true,         -- click lands, drag moves or pans, wheel zooms
        accent = "#ff6b1a",     -- selected row, caret, key names; #rrggbb, a preset (cyan, violet, ...) or "auto" for the theme
        mono_font = "JetBrainsMono Nerd Font", -- the whole HUD is set in this

        -- Look (feature level 3). All of these are also in the live tuner.
        hud_scale = 1.0,
        width = 720,
        top = 0.075,
        rows = 6,
        row_height = 58,
        rounding = 11,
        panel_opacity = 0.94,
        uppercase = true,
        letter_spacing = 1.0,
        query_size = 17,
        title_size = 14,
        detail_size = 10.5,
        corner_size = 12,
        label_size = 10.5,
        corner_labels = true,
        search_height = 60,
        search_border = 1.5,
        search_border_opacity = 0.85,
        search_border_color = "accent",
        search_glow = 0.3,
      },

      -- The wallpaper as a far layer: it follows the camera at a fraction of
      -- the windows' speed and shrinks a little as you zoom out.
      parallax = {
        enabled = true,
        strength = 0.06,
        depth = 0.12,
        desktop = false,
      },

      chrome_animation = {
        enabled = true,
        top_namespace = "omarchy-bar",
        bottom_namespace = "omarchy-dock",
        top_travel = 1.35,
        bottom_travel = 1.08,
        top_scale = 0.82,
        bottom_scale = 0.92,
        opacity = 0.0,
      },

      wallpaper = 0,
      blur = false,
      blur_strength = 0.7,
      shadow = {
        enabled = false,
        range = 36,
      },
    },
  },
})

hl.curve("spatialOverviewEase", {
  type = "bezier",
  points = { { 0.22, 1.0 }, { 0.36, 1.0 } },
})

-- Settings changed with the live tuner (Ctrl+, in the zoomed-out canvas)
-- are saved to spatialoverview-tuning.lua and win over the values above.
-- Delete a line there to hand that setting back to this file.
if hl.plugin.spatialoverview.tuning_file then
  local ok, tuned = pcall(dofile, hl.plugin.spatialoverview.tuning_file())
  if ok and type(tuned) == "table" then
    local tree = {}
    for key, value in pairs(tuned) do
      local node, parts = tree, {}
      for part in string.gmatch(key, "[^:]+") do
        parts[#parts + 1] = part
      end
      for i = 1, #parts - 1 do
        node[parts[i]] = node[parts[i]] or {}
        node = node[parts[i]]
      end
      node[parts[#parts]] = value
    end
    hl.config({ plugin = { spatialoverview = tree } })
  end
end

-- The canvas. The first press of a session creates it and zooms out.
bind("SUPER + CTRL + G", "Canvas", function()
  hl.plugin.spatialoverview.overview("toggle all")
end)

-- Runs a canvas action, or the `fallback` dispatcher when the canvas is not
-- open. (Inside a function, hl.dsp.* only builds a dispatcher; hl.dispatch
-- runs it.)
local function canvas_or(action, fallback)
  return function()
    if not pcall(hl.plugin.spatialoverview.canvas, action) then
      for _, dispatcher in ipairs(fallback) do
        hl.dispatch(dispatcher)
      end
    end
  end
end

-- Alt+Tab walks windows most recently used first: a tap flips to the
-- previous one, holding Alt shows the list. Without the canvas it cycles.
hl.unbind("ALT + TAB")
hl.unbind("ALT + SHIFT + TAB")
bind("ALT + TAB", "Recent windows", canvas_or("switch next", {
  hl.dsp.window.cycle_next(),
  hl.dsp.window.bring_to_top(),
}), { repeating = true })
bind("ALT + SHIFT + TAB", "Recent windows, backwards", canvas_or("switch prev", {
  hl.dsp.window.cycle_next({ next = false }),
  hl.dsp.window.bring_to_top(),
}), { repeating = true })

-- On the canvas every window floats, so SUPER + SHIFT + arrows nudge the
-- focused window a grid step (hold to keep going); tiled desktops swap.
for _, move in ipairs({
  { key = "LEFT", direction = "left", swap = "l" },
  { key = "RIGHT", direction = "right", swap = "r" },
  { key = "UP", direction = "up", swap = "u" },
  { key = "DOWN", direction = "down", swap = "d" },
}) do
  hl.unbind("SUPER + SHIFT + " .. move.key)
  bind("SUPER + SHIFT + " .. move.key, "Move window " .. move.direction, canvas_or("nudge " .. move.direction, {
    hl.dsp.window.swap({ direction = move.swap }),
  }), { repeating = true })
end

-- SUPER + arrows focus the nearest window that way; on the canvas the camera
-- follows it.
for _, move in ipairs({
  { key = "LEFT", direction = "left", focus = "l" },
  { key = "RIGHT", direction = "right", focus = "r" },
  { key = "UP", direction = "up", focus = "u" },
  { key = "DOWN", direction = "down", focus = "d" },
}) do
  hl.unbind("SUPER + " .. move.key)
  bind("SUPER + " .. move.key, "Focus window " .. move.direction, function()
    if not pcall(hl.plugin.spatialoverview.navigate, move.direction) then
      hl.dispatch(hl.dsp.focus({ direction = move.focus }))
    end
  end)
end
