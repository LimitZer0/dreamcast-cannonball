/***************************************************************************
    Dreamcast: animation on the VMU screens.

    Loops tools/vmu_anim/frames on every VMU plugged into a controller.
    Call vmu_lcd::tick() once per frame.
***************************************************************************/

#pragma once

namespace vmu_lcd
{
    void tick();
}
