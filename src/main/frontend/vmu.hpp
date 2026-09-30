/***************************************************************************
    Dreamcast VMU Save/Load Helpers.

    Dreamcast-only unified archive for arcade hi-scores and menu settings.

    Copyright Troy Davis.
***************************************************************************/

#pragma once

#ifdef __DREAMCAST__
// High score tables. list = (japanese tracks ? 1 : 0)
//                        | (continuous mode  ? 2 : 0)
//                        | (assists on       ? 4 : 0)
static const int VMU_SCORE_LISTS = 8;
bool vmu_load_scores(int list);
bool vmu_save_scores(int list);
bool vmu_clear_scores();
bool vmu_load_config();
bool vmu_save_config();
bool vmu_clear_config();
#endif
