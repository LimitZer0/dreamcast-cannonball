/***************************************************************************
    Debug build only (-DDREAMCAST_PVR_COMPARE): after every frame of the real
    game, re-render it with the original software renderer and compare with
    what the PVR produced. Logs mismatch counts and hex-dumps a few frames.
***************************************************************************/

#ifdef DREAMCAST_PVR_COMPARE

#include <cstring>
#include <dc/pvr.h>
#include <kos/dbglog.h>

#include "video.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "dreamcast/pvr_render.hpp"

void pvr_compare_after_frame()
{
    static int frame = 0, dumps = 0;
    static uint16_t ref[S16_WIDTH * S16_HEIGHT];
    static uint8_t shadow[S16_WIDTH * S16_HEIGHT];
    frame++;
    if (!video.enabled || (frame % 10) != 0) return;

    pvr_wait_ready();
    pvr_wait_render_done();

    uint16_t* px = video.pixels;
    const uint8_t* const skip = video.tile_layer->skip_lines;   // reference draws every row
    video.tile_layer->skip_lines = NULL;
    video.tile_layer->update_tile_values();
    (hwroad.*hwroad.render_background)(px);
    video.tile_layer->render_tile_layer(px, 1, 0);
    video.tile_layer->render_tile_layer(px, 0, 0);
    (hwroad.*hwroad.render_foreground)(px);
    video.sprite_layer->render(8);
    video.tile_layer->render_text_layer(px, 1);
    video.tile_layer->skip_lines = skip;

    const uint16_t* lut = (const uint16_t*)pvr_selftest_lut();
    for (int i = 0; i < S16_WIDTH * S16_HEIGHT; i++)
    {
        shadow[i] = px[i] >= S16_PALETTE_ENTRIES;
        ref[i] = lut[px[i] & 0xfff];
    }

    const uint16_t* got = (const uint16_t*)pvr_render_game_tex();
    int bad = 0, fx = -1, fy = -1;
    for (int y = 0; y < S16_HEIGHT; y++)
        for (int x = 0; x < S16_WIDTH; x++)
        {
            const int i = y * S16_WIDTH + x;
            if (shadow[i]) continue;
            const uint16_t a = got[i], b = ref[i];
            const int dr = (int)((a >> 11) & 31) - (int)((b >> 11) & 31);
            const int dg = (int)((a >> 5) & 63) - (int)((b >> 5) & 63);
            const int db = (int)(a & 31) - (int)(b & 31);
            if (dr < -1 || dr > 1 || dg < -2 || dg > 2 || db < -1 || db > 1)
            {
                if (fx < 0) { fx = x; fy = y; }
                bad++;
            }
        }
    dbglog(DBG_INFO, "cannonball: compare frame %d bad=%d first=(%d,%d)\n", frame, bad, fx, fy);

    if (bad > 200 && dumps < 3)
    {
        dumps++;
        static char line[S16_WIDTH * 4 + 1];
        static const char hx[] = "0123456789abcdef";
        for (int pass = 0; pass < 2; pass++)
            for (int y = 0; y < S16_HEIGHT; y++)
            {
                const uint16_t* row = pass ? ref + y * S16_WIDTH : got + y * S16_WIDTH;
                for (int x = 0; x < S16_WIDTH; x++)
                {
                    line[x * 4 + 0] = hx[(row[x] >> 12) & 15];
                    line[x * 4 + 1] = hx[(row[x] >> 8) & 15];
                    line[x * 4 + 2] = hx[(row[x] >> 4) & 15];
                    line[x * 4 + 3] = hx[row[x] & 15];
                }
                line[S16_WIDTH * 4] = 0;
                dbglog(DBG_INFO, "DUMP%c%03d %s\n", pass ? 'R' : 'G', y, line);
            }
    }
}

#endif
