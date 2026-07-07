/***************************************************************************
    Dreamcast VMU Save/Load Helpers.

    Dreamcast-only unified archive for arcade hi-scores and menu settings.

    Copyright Troy Davis.
***************************************************************************/

#pragma once

#ifdef __DREAMCAST__
bool vmu_load_scores();
bool vmu_save_scores();
bool vmu_clear_scores();
bool vmu_load_config();
bool vmu_save_config();
bool vmu_clear_config();
#endif
