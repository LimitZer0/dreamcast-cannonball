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

# Force the road-split fork direction instead of letting the AI's car
# position decide -- for capturing a specific route deterministically.
# 0 = natural (default), positive = force left, negative = force right.
set(DREAMCAST_FORCE_FORK 0 CACHE STRING "Force road-split fork direction: 0=natural (AI decides), positive=force left, negative=force right")
add_definitions(-DDREAMCAST_FORCE_FORK=${DREAMCAST_FORCE_FORK})

# Exit automatically (same arch_exit() path as the /pc/exit_now sentinel)
# the moment ostats.cur_stage advances past its value at game start -- i.e.
# once the current stage's road split has actually been played through and
# the next stage has begun. Lets a route-matrix capture stop precisely at
# one stage's fork instead of an arbitrary frame/time budget.
option(DREAMCAST_EXIT_ON_STAGE_ADVANCE "Auto-exit via arch_exit() once ostats.cur_stage advances past its start-of-run value" OFF)
if(DREAMCAST_EXIT_ON_STAGE_ADVANCE)
    add_definitions(-DDREAMCAST_EXIT_ON_STAGE_ADVANCE)
endif()

# How many cur_stage advances to let pass before DREAMCAST_EXIT_ON_STAGE_ADVANCE
# actually exits -- 1 (default) stops at the first fork, 2 lets stage 1's
# fork go by and stops at stage 2's, etc.
set(DREAMCAST_EXIT_AFTER_N_ADVANCES 1 CACHE STRING "Number of cur_stage advances to let pass before DREAMCAST_EXIT_ON_STAGE_ADVANCE exits")
add_definitions(-DDREAMCAST_EXIT_AFTER_N_ADVANCES=${DREAMCAST_EXIT_AFTER_N_ADVANCES})

# Stage 5's ending (GS_INIT_BONUS -> GS_BONUS -> hiscore) never touches
# ostats.cur_stage, so DREAMCAST_EXIT_ON_STAGE_ADVANCE can never fire for it
# -- a start-level-5 capture would otherwise loop forever through
# GS_REINIT/GS_INIT with no exit condition. This exits the instant a full
# ending cycle (bonus animation + hiscore screen) completes, detected as
# ostats.game_completed clearing back to 0 in the next OInitEngine::init()
# (mirrors DREAMCAST_SKIP_HISCORE_ENTRY's use case: unattended AI captures
# with no controller to trigger a manual exit).
option(DREAMCAST_EXIT_ON_GAME_COMPLETE "Auto-exit via arch_exit() once a full stage-5 ending cycle (bonus + hiscore) completes and the game reinitializes" OFF)
if(DREAMCAST_EXIT_ON_GAME_COMPLETE)
    add_definitions(-DDREAMCAST_EXIT_ON_GAME_COMPLETE)
endif()

# GS_ATTRACT has a bounded demo timer (decrement_timers() in outrun.cpp)
# that resets the whole demo back to stage 1 regardless of whether a fork
# was reached -- confirmed on hardware to fire before a full stage-1
# traversal can complete. Real credited gameplay (GS_INGAME) has no such
# timeout. DREAMCAST_FORCE_AI keeps the AI driving once there (globals.hpp)
# instead of handing control to a real controller; DREAMCAST_SKIP_CREDITS
# bypasses the credit/Start-button gate (check_freeplay_start()/
# OMusic::check_start()) so a capture reaches GS_INGAME without physical
# input, same rationale as DREAMCAST_AUTOSTART but one gate further in --
# use both together for unattended full-route captures.
option(DREAMCAST_FORCE_AI "Keep the AI driving during real credited gameplay (GS_INGAME), not just GS_ATTRACT's timeout-limited demo" OFF)
if(DREAMCAST_FORCE_AI)
    add_definitions(-DDREAMCAST_FORCE_AI)
endif()

option(DREAMCAST_SKIP_CREDITS "Bypass the arcade credit/Start-button gate so boot reaches real gameplay (GS_INGAME) without physical input" OFF)
if(DREAMCAST_SKIP_CREDITS)
    add_definitions(-DDREAMCAST_SKIP_CREDITS)
endif()

# Hiscore name-entry (OHiScore::check_name_entry()) normally waits
# indefinitely for controller input to type initials -- fine for a human
# player, but it stalls an unattended DREAMCAST_FORCE_AI/DREAMCAST_SKIP_CREDITS
# capture forever with no controller present. This auto-confirms the
# screen immediately instead of waiting for input.
option(DREAMCAST_SKIP_HISCORE_ENTRY "Auto-confirm the hiscore initials-entry screen instead of waiting for controller input, for unattended AI captures" OFF)
if(DREAMCAST_SKIP_HISCORE_ENTRY)
    add_definitions(-DDREAMCAST_SKIP_HISCORE_ENTRY)
endif()

# Platform Specific Libraries
set(platform_link_libs
)

# Platform Specific Link Directories
set(platform_link_dirs
)
