/***************************************************************************
    Dreamcast: animation on the VMU screens.

    Loops the frames in vmu_anim.h at 3 frames a second on every VMU
    (any maple device with an LCD). A VMU plugged in later picks up the
    next frame. VIDEO > VMU ANIMATION turns it off, which blanks the screens.
***************************************************************************/

#include "dreamcast/vmu_lcd.hpp"
#include "dreamcast/vmu_anim.h"
#include "frontend/config.hpp"

#include <cstring>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <arch/timer.h>

namespace vmu_lcd
{
    static const uint64_t FRAME_MS = 333;               // 3 frames a second

    static const int SLOTS = MAPLE_PORT_COUNT * MAPLE_UNIT_COUNT;
    static int shown[SLOTS];                             // frame on each screen (-1 none, -2 blank)
    static bool started = false;
    static uint64_t next_ms = 0;
    static int frame = 0;

    static const uint8_t blank[192] __attribute__((aligned(4))) = {0};

    void tick()
    {
        const uint64_t now = timer_ms_gettime64();
        if (!started)
        {
            for (int i = 0; i < SLOTS; i++) shown[i] = -1;
            started = true;
            next_ms = now + FRAME_MS;
        }
        else if (now >= next_ms)
        {
            frame = (frame + 1) % VMU_ANIM_FRAMES;
            next_ms += FRAME_MS;
            if (now >= next_ms) next_ms = now + FRAME_MS;   // after a long stall (loading)
        }

        const bool on = config.video.vmu_anim != 0;
        const int want = on ? frame : -2;
        const uint8_t* img = on ? vmu_anim_data[frame] : blank;

        maple_device_t* dev;
        for (int n = 0; (dev = maple_enum_type(n, MAPLE_FUNC_LCD)) != NULL; n++)
        {
            const int slot = dev->port * MAPLE_UNIT_COUNT + dev->unit;
            if (slot < 0 || slot >= SLOTS || shown[slot] == want)
                continue;
            // Busy (a save in progress, or the last frame still going out):
            // try again next tick
            if (vmu_draw_lcd(dev, img) == MAPLE_EOK)
                shown[slot] = want;
        }
    }
}
