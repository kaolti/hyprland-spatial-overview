-- Isolated compositor used by scripts/run-nested.sh.
-- It intentionally avoids importing the user's Omarchy session.

hl.monitor({
  output = "",
  mode = "1280x720@60",
  position = "auto",
  scale = 1,
})

hl.config({
  general = {
    gaps_in = 8,
    gaps_out = 16,
    border_size = 2,
  },
  decoration = {
    rounding = 12,
    blur = { enabled = true, size = 4, passes = 2 },
  },
  animations = { enabled = true },
  misc = {
    disable_hyprland_logo = true,
    force_default_wallpaper = 0,
  },
})

hl.curve("spatialTest", { type = "bezier", points = { { 0.22, 1 }, { 0.36, 1 } } })
hl.animation({ leaf = "global", enabled = true, speed = 8, bezier = "spatialTest" })

-- The nested test window can always be closed with Super+Shift+Escape.
hl.bind("SUPER + SHIFT + ESCAPE", hl.dsp.exit())
hl.bind("SUPER + Q", hl.dsp.window.close())

local plugin_path = "@PLUGIN@" -- filled in by the test harness
hl.plugin.load(plugin_path)

local plugin_loaded = false
for _, plugin in ipairs(hl.get_loaded_plugins()) do
  if plugin.name == "spatialoverview" then
    plugin_loaded = true
    break
  end
end

if plugin_loaded then
  hl.curve("spatialOverviewEase", {
    type = "bezier",
    points = { { 0.22, 1.0 }, { 0.36, 1.0 } },
  })

  hl.config({
    plugin = {
      spatialoverview = {
        layout = "grid",
        scale = 0.36,
        workspace_gap = 0,
        grid = { columns = 3, stagger = 0.0 },
        animation = { enabled = true, speed = 8.0, bezier = "spatialOverviewEase" },
        distortion = {
          enabled = true,
          strength = 0.14,
          edge_scale = 1.08,
          feather = 0.025,
          transition_power = 1.0,
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
          initial_zoom = 0.72,
          min_zoom = 0.15,
          max_zoom = 2.5,
          zoom_step = 0.12,
          auto_float = true,
          auto_place = true,
          placement_gap = 40,
          arrange_context_grouping = true,
          arrange_size_similarity = 0.12,
          arrange_resize_limit = 0.15,
          space_pan = true,
          direct_input = true,
          grid_enabled = true,
          grid_size = 80,
          grid_width = 1,
          grid_opacity = 0.16,
          grid_style = 1,
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
          snap_size = 80,
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
        blur = false,
        blur_strength = 0.7,
      },
    },
  })
end
