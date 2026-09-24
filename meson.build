project('spatialoverview', 'cpp',
  version: '0.1',
  default_options: ['buildtype=release'],
)

cpp_compiler = meson.get_compiler('cpp')
if cpp_compiler.has_argument('-std=c++23')
  add_global_arguments('-std=c++23', language: 'cpp')
elif cpp_compiler.has_argument('-std=c++2b')
  add_global_arguments('-std=c++2b', language: 'cpp')
else
  error('Could not configure current C++ compiler (' + cpp_compiler.get_id() + ' ' + cpp_compiler.version() + ') with required C++ standard (C++23)')
endif

add_project_arguments(
  [
    '-Wno-narrowing',
  ],
  language: 'cpp')

# The plugin is the top-level sources only (scripts/ and tests/ hold separate tools).
globber = run_command('find', '.', '-maxdepth', '1', '-name', '*.cpp', check: true)
src = globber.stdout().strip().split('\n')

version_header = custom_target(
  'plugin-version',
  output: 'PluginVersion.hpp',
  command: [
    find_program('sh'),
    files('scripts/generate-plugin-version.sh'),
    '@OUTPUT@',
    meson.project_source_root(),
  ],
  build_always_stale: true,
)

hyprland = dependency('hyprland')

shared_module(meson.project_name(), src + [version_header],
  include_directories: include_directories('.'),
  dependencies: [
    dependency('hyprland'),
    dependency('hyprgraphics'),
    dependency('pixman-1'),
    dependency('libdrm'),
    dependency('pangocairo'),
    dependency('libinput'),
    dependency('libudev'),
    dependency('lua5.4', 'lua', version: '>=5.4'),
    dependency('wayland-server'),
    dependency('xkbcommon'),
  ],
  install: true,
)
