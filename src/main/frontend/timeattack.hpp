/***************************************************************************
    Time Trial (full course): the arcade course from Coconut Beach to a
    goal with no traffic, no countdown and no score. The total time is kept
    per route (16 routes) in a top 5 with initials, saved to the memory card
    (file CANNONTT), and can be sent to the online leaderboard.

    Tables: [Japanese tracks][modified][route 0-15][rank 0-4].
    "Modified" = grippy tyres, off-road, bumper, turbo or the prototype stage 1.
    Times are in 1/100 s, the sum of the stage lap times the game shows.
***************************************************************************/

#pragma once

#include <stdint.h>
#include <string>

namespace timeattack
{
    const int ROUTES = 16, RANKS = 5;

    struct Entry
    {
        uint32_t cs;        // 0 = empty
        char     init[3];
    };

    // Mode on/off. start() before starting the game from the menu; end()
    // when back in the menu (restores the traffic setting).
    void start();
    void end();
    bool active();

    // Running total of the stage lap times, in 1/100 s
    uint32_t total_cs();

    // In game: draw the running total where the score normally is
    void draw_hud();

    // Results screen at the end of the run (replaces the high score entry).
    // tick returns true when the player has finished with it.
    void results_init();
    bool results_tick();

    // Saved tables (loaded from the memory card on start())
    bool load();
    bool clear();
    const Entry* table(int jap, int modified, int route);

    // Route index 0-15 from the game's final route map tiles, or -1
    int route_of_maptiles(uint32_t maptiles);
}
