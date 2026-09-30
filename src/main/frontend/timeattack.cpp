/***************************************************************************
    Time Trial (full course). See timeattack.hpp.
***************************************************************************/

#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "frontend/timeattack.hpp"
#include "frontend/config.hpp"
#include "frontend/vmu_icon.h"
#include "engine/outrun.hpp"
#include "engine/ostats.hpp"
#include "engine/ohud.hpp"
#include "engine/ohiscore.hpp"
#include "engine/oinputs.hpp"
#include "engine/oaddresses.hpp"
#include "engine/audio/osoundint.hpp"
#include "engine/audio/commands.hpp"
#include "roms.hpp"
#include "video.hpp"
#include "sdl2/input.hpp"

#ifdef __DREAMCAST__
#include <kos/fs.h>
#include <kos/dbglog.h>
#include <dc/maple.h>
#include <dc/vmu_pkg.h>
#include <dc/vmufs.h>
#endif

namespace timeattack
{
namespace
{
    bool on = false;
    bool traffic_backup = false;

    Entry tables[2][2][ROUTES][RANKS];

    // Save file: "CBTT" | version 1 | tables (cs u24 LE + 3 initials each)
    const char   FILE_NAME[] = "CANNONTT";
    const size_t DATA_LEN = 8 + 2 * 2 * ROUTES * RANKS * 6;

    int modified_now()
    {
        return (config.engine.grippy_tyres || config.engine.offroad ||
                config.engine.bumper || config.engine.turbo || config.engine.prototype) ? 1 : 0;
    }

    void pack(uint8_t* d)
    {
        memset(d, 0, DATA_LEN);
        memcpy(d, "CBTT", 4);
        d[4] = 1;
        uint8_t* p = d + 8;
        for (int j = 0; j < 2; j++) for (int m = 0; m < 2; m++)
            for (int r = 0; r < ROUTES; r++) for (int k = 0; k < RANKS; k++)
            {
                const Entry& e = tables[j][m][r][k];
                p[0] = (uint8_t)e.cs; p[1] = (uint8_t)(e.cs >> 8); p[2] = (uint8_t)(e.cs >> 16);
                memcpy(p + 3, e.init, 3);
                p += 6;
            }
    }

    bool unpack(const uint8_t* d, size_t n)
    {
        for (size_t i = 0; i + DATA_LEN <= n; i++)
        {
            if (memcmp(d + i, "CBTT", 4) != 0 || d[i + 4] != 1) continue;
            const uint8_t* p = d + i + 8;
            for (int j = 0; j < 2; j++) for (int m = 0; m < 2; m++)
                for (int r = 0; r < ROUTES; r++) for (int k = 0; k < RANKS; k++)
                {
                    Entry& e = tables[j][m][r][k];
                    e.cs = p[0] | (p[1] << 8) | (p[2] << 16);
                    memcpy(e.init, p + 3, 3);
                    p += 6;
                }
            return true;
        }
        return false;
    }

#ifdef __DREAMCAST__
    maple_device_t* card()
    {
        return maple_enum_type(0, MAPLE_FUNC_MEMCARD);
    }
#endif

    bool save()
    {
#ifdef __DREAMCAST__
        maple_device_t* dev = card();
        if (!dev) return false;
        static uint8_t data[DATA_LEN];
        pack(data);
        vmu_pkg_t pkg;
        memset(&pkg, 0, sizeof(pkg));
        strcpy(pkg.desc_short, "Cannonball TT");
        strcpy(pkg.desc_long, "Cannonball Time Trial times");
        strcpy(pkg.app_id, "CANNONBALL");
        pkg.icon_cnt = 1;
        memcpy(pkg.icon_pal, VMU_ICON_PAL, sizeof(pkg.icon_pal));
        pkg.icon_data = (uint8_t*)VMU_ICON_DATA;
        pkg.eyecatch_type = VMUPKG_EC_NONE;
        pkg.data_len = DATA_LEN;
        pkg.data = data;
        uint8_t* out = NULL;
        int size = 0;
        if (vmu_pkg_build(&pkg, &out, &size) < 0 || !out) return false;
        const int padded = (size + 511) & ~511;
        uint8_t* buf = (uint8_t*)calloc(1, padded);
        if (!buf) { free(out); return false; }
        memcpy(buf, out, size);
        free(out);
        const int rv = vmufs_write(dev, FILE_NAME, buf, padded, VMUFS_OVERWRITE);
        free(buf);
        dbglog(DBG_INFO, "[VMU] Time trial times saved (%d)\n", rv);
        return rv >= 0;
#else
        return true;
#endif
    }

    // --- results screen -----------------------------------------------------
    enum { R_SHOW, R_ENTRY, R_DONE };
    int      r_state;
    int      r_route;           // 0-15, -1 = unknown
    int      r_rank;            // 0-4, -1 = not in the top 5
    uint32_t r_cs;
    int      r_initial;
    bool     r_saved, r_card;
    bool     acc_prev, start_prev;
    uint8_t  r_flash;
    // The arcade entry's letters (OHiScore alphabet row order)
    const char LETTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.";

    // BCD digits for OHud::draw_lap_timer from 1/100 s
    void time_digits(uint32_t cs, uint8_t* d, uint8_t& hund)
    {
        const uint32_t m = cs / 6000, s = (cs / 100) % 60, c = cs % 100;
        d[0] = (uint8_t)(m > 9 ? 9 : m);
        d[1] = (uint8_t)((s / 10) << 4 | (s % 10));
        hund = (uint8_t)((c / 10) << 4 | (c % 10));
        d[2] = hund;
    }

    void draw_time(uint16_t x, uint16_t y, uint32_t cs)
    {
        uint8_t d[3], h;
        time_digits(cs, d, h);
        ohud.draw_lap_timer(ohud.translate(x, y), d, h);
    }

    void centre(uint16_t y, const char* s, uint16_t pal)
    {
        ohud.blit_text_centre(y, s, pal);
    }

    void draw_results()
    {
        video.clear_text_ram();
        centre(2, "TIME TRIAL", OHud::GREEN);

        char line[40];
        const int jap = config.engine.jap ? 1 : 0, mod = modified_now();
        if (r_route >= 0)
        {
            const int ones = __builtin_popcount(r_route);
            snprintf(line, sizeof(line), "%s  GOAL %c%s", jap ? "JAPAN" : "WORLD", 'A' + ones, mod ? "  MODIFIED" : "");
        }
        else
            snprintf(line, sizeof(line), "%s", jap ? "JAPAN" : "WORLD");
        centre(4, line, OHud::GREY);

        ohud.blit_text_new(10, 7, "YOUR TIME", OHud::GREEN);
        draw_time(21, 7, r_cs);

        if (r_route >= 0)
        {
            const Entry* t = tables[jap][mod][r_route];
            ohud.blit_text_new(10, 10, "BEST TIMES", OHud::GREEN);
            for (int k = 0; k < RANKS; k++)
            {
                const uint16_t y = 12 + k * 2;
                const uint16_t pal = k == r_rank ? OHud::PINK : OHud::GREY;
                snprintf(line, sizeof(line), "%d.", k + 1);
                ohud.blit_text_new(10, y, line, pal);
                if (t[k].cs)
                {
                    char in[4] = { t[k].init[0], t[k].init[1], t[k].init[2], 0 };
                    for (int i = 0; i < 3; i++) if (!in[i]) in[i] = ' ';
                    ohud.blit_text_new(14, y, in, pal);
                    draw_time(21, y, t[k].cs);
                }
                else
                    ohud.blit_text_new(14, y, "---", OHud::GREY);
            }
        }

        if (r_state == R_ENTRY)
        {
            // Arcade style: the chosen letter flashes in the table row; the
            // alphabet row is drawn by ohiscore.alpha_tick()
            const int sel = ohiscore.alpha_selected();
            if ((r_flash & 8) && sel < OHiScore::ALPHA_DELETE)
            {
                char c[2] = { LETTERS[sel], 0 };
                ohud.blit_text_new(14 + r_initial, 12 + r_rank * 2, c, OHud::PINK);
            }
        }
        else
        {
            if (r_route < 0)
                centre(23, "ROUTE NOT RECOGNISED", OHud::GREY);
            else if (r_rank < 0)
                centre(23, "NOT A TOP 5 TIME", OHud::GREY);
            else if (!r_card)
                centre(23, "NO MEMORY CARD  TIME NOT SAVED", OHud::GREY);
            else if (r_saved)
                centre(23, "TIME SAVED", OHud::GREEN);
            centre(25, "PRESS START", OHud::GREY);
        }
    }

    void finish_entry()
    {
        Entry& e = tables[config.engine.jap ? 1 : 0][modified_now()][r_route][r_rank];
        for (int i = r_initial; i < 3; i++) e.init[i] = ' ';
#ifdef __DREAMCAST__
        r_card = card() != NULL;
#else
        r_card = true;
#endif
        r_saved = r_card && save();
        r_state = R_DONE;
    }
}

void start()
{
    on = true;
    traffic_backup = config.engine.disable_traffic;
    config.engine.disable_traffic = true;
    load();
}

void end()
{
    if (!on) return;
    on = false;
    config.engine.disable_traffic = traffic_backup;
}

bool active()
{
    return on;
}

uint32_t total_cs()
{
    uint32_t total = 0;
    for (int s = 0; s <= ostats.cur_stage && s < 15; s++)
    {
        const uint8_t* t = ostats.stage_times[s];
        const uint32_t m = (t[0] >> 4) * 10 + (t[0] & 0xF);
        const uint32_t sec = (t[1] >> 4) * 10 + (t[1] & 0xF);
        const uint8_t hund = ostats.lap_ms ? ostats.lap_ms[t[2] & 0x3F] : 0;
        total += m * 6000 + sec * 100 + (hund >> 4) * 10 + (hund & 0xF);
    }
    return total;
}

void draw_hud()
{
    if (!on || outrun.game_state < GS_START1 || outrun.game_state > GS_BONUS) return;
    // In place of SCORE: "TOTAL" and the running time
    ohud.blit_text_new(12, 2, "TOTAL", OHud::PINK);
    draw_time(18, 2, total_cs());
}

// Clear the saved times (CLEAR HISCORES). True if there were any.
bool clear()
{
    memset(tables, 0, sizeof(tables));
#ifdef __DREAMCAST__
    maple_device_t* dev = card();
    return dev && vmufs_delete(dev, FILE_NAME) == 0;
#else
    return false;
#endif
}

bool load()
{
    memset(tables, 0, sizeof(tables));
#ifdef __DREAMCAST__
    maple_device_t* dev = card();
    if (!dev) return false;
    char path[32];
    snprintf(path, sizeof(path), "/vmu/%c%d/%s", 'a' + dev->port, dev->unit, FILE_NAME);
    file_t f = fs_open(path, O_RDONLY);
    if (f == FILEHND_INVALID) return false;
    static uint8_t buf[4096];
    const ssize_t n = fs_read(f, buf, sizeof(buf));
    fs_close(f);
    const bool ok = n > 0 && unpack(buf, (size_t)n);
    dbglog(DBG_INFO, "[VMU] Time trial times %s\n", ok ? "loaded" : "unreadable");
    return ok;
#else
    return false;
#endif
}

const Entry* table(int jap, int modified, int route)
{
    return tables[jap ? 1 : 0][modified ? 1 : 0][route];
}

int route_of_maptiles(uint32_t maptiles)
{
    for (int n = 15; n <= 30; n++)
        if (roms.rom0.read32(TILES_MINIMAP + (n << 2)) == maptiles)
            return n - 15;
    return -1;
}

void results_init()
{
    r_cs = total_cs();
    r_route = route_of_maptiles(roms.rom0.read32(ohud.setup_mini_map()));
    r_rank = -1;
    r_saved = false;
    r_card = true;
    r_initial = 0;
    r_flash = 0;
    acc_prev = start_prev = true;   // wait for a fresh press
    ohiscore.alpha_reset();

    const int jap = config.engine.jap ? 1 : 0, mod = modified_now();
    if (r_route >= 0 && r_cs > 0)
    {
        Entry* t = tables[jap][mod][r_route];
        for (int k = 0; k < RANKS; k++)
            if (!t[k].cs || r_cs < t[k].cs) { r_rank = k; break; }
        if (r_rank >= 0)
        {
            for (int k = RANKS - 1; k > r_rank; k--) t[k] = t[k - 1];
            t[r_rank].cs = r_cs;
            memset(t[r_rank].init, ' ', 3);
        }
    }
    r_state = r_rank >= 0 ? R_ENTRY : R_SHOW;
    osoundint.queue_sound(sound::MUSIC_LASTWAVE);
    video.enabled = true;       // init_best_outrunners turned the screen off
    draw_results();
}

bool results_tick()
{
    const bool acc   = input.accel_held() || input.is_pressed(Input::START);
    const bool start = input.is_pressed(Input::START);
    const bool acc_hit = acc && !acc_prev, start_hit = start && !start_prev;
    acc_prev = acc; start_prev = start;

#ifdef DREAMCAST_DEBUG_AUTO_END
    // Test builds: enter "AI" and leave after a few seconds
#ifndef DREAMCAST_DEBUG_ENTRY_FRAMES
#define DREAMCAST_DEBUG_ENTRY_FRAMES 60
#endif
    static int auto_frames = 0;
    if (++auto_frames == DREAMCAST_DEBUG_ENTRY_FRAMES && r_state == R_ENTRY)
    {
        Entry& e = tables[config.engine.jap ? 1 : 0][modified_now()][r_route][r_rank];
        e.init[0] = 'A'; e.init[1] = 'I'; r_initial = 2;
        finish_entry();
    }
    if (auto_frames == DREAMCAST_DEBUG_ENTRY_FRAMES + 240) { auto_frames = 0; return true; }
#endif
    if (r_state == R_ENTRY)
    {
        r_flash++;
        draw_results();
        const int pick = ohiscore.alpha_tick();     // arcade entry controls
        Entry& e = tables[config.engine.jap ? 1 : 0][modified_now()][r_route][r_rank];
        if (start_hit || pick == OHiScore::ALPHA_END)
            finish_entry();
        else if (pick == OHiScore::ALPHA_DELETE)
        {
            if (r_initial > 0) e.init[--r_initial] = ' ';
        }
        else if (pick >= 0)
        {
            e.init[r_initial++] = LETTERS[pick];
            if (r_initial == 3) finish_entry();
        }
        if (r_state != R_ENTRY)
        {
            acc_prev = start_prev = true;
            draw_results();
        }
        return false;
    }

    if (start_hit || (acc_hit && r_state != R_ENTRY))
        return true;
    draw_results();
    return false;
}

}
