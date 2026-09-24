cmake_minimum_required(VERSION 3.27)

project(spatialoverview
    DESCRIPTION "spatial overview plugin for Hyprland"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)

# The plugin is the top-level sources only (scripts/ and tests/ hold separate tools).
file(GLOB SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(spatialoverview SHARED ${SRC})

set(VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(VERSION_HEADER "${VERSION_HEADER_DIR}/PluginVersion.hpp")
add_custom_target(spatialoverview-version ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VERSION_HEADER_DIR}"
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-plugin-version.sh" "${VERSION_HEADER}" "${CMAKE_CURRENT_SOURCE_DIR}"
    BYPRODUCTS "${VERSION_HEADER}"
    VERBATIM
)
add_dependencies(spatialoverview spatialoverview-version)
target_include_directories(spatialoverview PRIVATE "${VERSION_HEADER_DIR}")

find_package(PkgConfig REQUIRED)
pkg_search_module(lua REQUIRED IMPORTED_TARGET "lua5.4>=5.4" "lua>=5.4")
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    hyprgraphics
    libdrm
    libinput
    libudev
    pangocairo
    pixman-1
    wayland-server
    xkbcommon
)
target_link_libraries(spatialoverview PRIVATE rt PkgConfig::deps PkgConfig::lua)

install(TARGETS spatialoverview)
