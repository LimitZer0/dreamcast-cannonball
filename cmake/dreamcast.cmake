# -----------------------------------------------------------------------------
# CannonBall Dreamcast / KallistiOS Setup
# -----------------------------------------------------------------------------

# SDL2 installed by KOS ports.
set(sdl2_dir /opt/toolchains/dc/kos/addons/lib/dreamcast/cmake/SDL2)

# The Dreamcast build uses lightweight local parsers/helpers instead of Boost.
set(USE_BOOST 0)

# The Dreamcast SDL2 port provides the video backend; do not enable CannonBall's
# standalone OpenGL renderer.
add_definitions(-DDREAMCAST)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

option(DREAMCAST_SKIP_SPRITE_SHADOWS "Skip generated sprite shadow entries on Dreamcast" OFF)
if(DREAMCAST_SKIP_SPRITE_SHADOWS)
    add_definitions(-DDREAMCAST_SKIP_SPRITE_SHADOWS)
endif()

option(DREAMCAST_FAST_SPRITES "Enable experimental Dreamcast sprite rasterizer fast paths" OFF)
if(DREAMCAST_FAST_SPRITES)
    add_definitions(-DDREAMCAST_FAST_SPRITES)
endif()

option(DREAMCAST_FAST_SPRITES_VALIDATE "Compare experimental Dreamcast sprite fast paths against the original rasterizer" OFF)
if(DREAMCAST_FAST_SPRITES_VALIDATE)
    add_definitions(-DDREAMCAST_FAST_SPRITES -DDREAMCAST_FAST_SPRITES_VALIDATE)
endif()

option(DREAMCAST_SPRITE_ZOOM_PROFILE "Report per-interval and cumulative Dreamcast sprite horizontal zoom histograms" OFF)
if(DREAMCAST_SPRITE_ZOOM_PROFILE)
    add_definitions(-DDREAMCAST_SPRITE_ZOOM_PROFILE)
endif()

option(DREAMCAST_AUTOSTART "Skip the attract-mode credit/Start-button gate so boot goes straight into AI-driven gameplay, for unattended perf A/B testing" OFF)
if(DREAMCAST_AUTOSTART)
    add_definitions(-DDREAMCAST_AUTOSTART)
endif()

# Internal stage-lookup byte to start AI-driven (attract-mode) gameplay at,
# instead of always stage 1 -- see STAGE_LOOKUP in src/main/frontend/ttrial.cpp
# for the full table. 0 = stage 1 (default, no behavior change). Examples:
# 0x09/0x08 = stage 2 (two branches), 0x12/0x11/0x10 = stage 3,
# 0x1B/0x1A/0x19/0x18 = stage 4, 0x24/0x23/0x22/0x21/0x20 = stage 5.
set(DREAMCAST_START_LEVEL 0 CACHE STRING "Internal stage-lookup byte to start attract-mode gameplay at, for A/B perf testing of a specific stage without driving through earlier ones")
add_definitions(-DDREAMCAST_START_LEVEL=${DREAMCAST_START_LEVEL})

# Platform Specific Libraries
set(platform_link_libs
)

# Platform Specific Link Directories
set(platform_link_dirs
)
