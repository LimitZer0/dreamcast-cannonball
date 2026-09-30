/***************************************************************************
    Dreamcast PVR renderer self-test (no OutRun ROMs needed).

    Builds random sprite/tile/road graphics, palettes and video RAM, renders
    each test frame twice:
      - with the original CannonBall software renderer (reference), and
      - with the PowerVR renderer (rendered to a texture and read back),
    and compares the two pixel by pixel. Results are printed to the debug
    console and shown on screen, for each candidate PVR sampling offset.

    Build with -DDREAMCAST_PVR_SELFTEST=ON.
***************************************************************************/

#ifdef DREAMCAST_PVR_SELFTEST

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <vector>

#include <dc/pvr.h>
#include <dc/pvr/pvr_regs.h>
#include <dc/video.h>
#include <dc/biosfont.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <kos/dbglog.h>
#include <kos/thread.h>

#include "video.hpp"
#include "roms.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "frontend/config.hpp"
#include "dreamcast/pvr_render.hpp"

#define ST_LOG(...) dbglog(DBG_INFO, __VA_ARGS__)

namespace
{
    uint32_t rng_state = 0x1234567;
    inline uint32_t rnd()
    {
        uint32_t x = rng_state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return rng_state = x;
    }

    struct Img { uint8_t bank; uint16_t addr; int words; int rows; };
    std::vector<Img> imgs;

    // Sprite ROM with real-looking images (rows ending in end markers)
    uint8_t* make_sprite_rom()
    {
        const uint32_t words = 0x100000 >> 2;
        uint32_t* w = new uint32_t[words];
        for (uint32_t i = 0; i < words; i++) w[i] = rnd();

        for (int bank = 0; bank < 4; bank++)
        {
            uint32_t a = 0x100;
            while (a < 0xF000)
            {
                Img im;
                im.bank = bank; im.addr = a;
                im.words = 1 + rnd() % 12;
                im.rows = 4 + rnd() % 120;
                uint32_t* d = w + 0x10000 * bank;
                for (int r = 0; r < im.rows; r++)
                    for (int x = 0; x < im.words; x++)
                    {
                        uint32_t v = 0;
                        for (int k = 0; k < 8; k++)
                        {
                            uint32_t p = rnd() % 15;
                            if (rnd() % 4 == 0) p = 0;
                            if (rnd() % 8 == 0) p = 0xa;
                            v |= p << (28 - 4 * k);
                        }
                        if (x == im.words - 1) v |= 0xff;
                        if (x == 0) v |= 0xff000000;
                        if (x != im.words - 1 && (v & 0xf0) == 0xf0) v &= ~0x10u;
                        if (x != 0 && (v & 0x0f000000) == 0x0f000000) v &= ~0x01000000u;
                        d[(uint16_t)(a + r * im.words + x)] = v;
                    }
                imgs.push_back(im);
                a += im.words * im.rows + 16;
            }
        }

        // Back to the ROM byte order hwsprites::init() expects
        uint8_t* rom = new uint8_t[0x100000];
        for (uint32_t i = 0; i < words; i++)
        {
            rom[i * 4 + 0] = (w[i] >> 0) & 0xff;
            rom[i * 4 + 1] = (w[i] >> 8) & 0xff;
            rom[i * 4 + 2] = (w[i] >> 16) & 0xff;
            rom[i * 4 + 3] = (w[i] >> 24) & 0xff;
        }
        delete[] w;
        return rom;
    }

    uint8_t* make_tile_rom()
    {
        uint8_t* rom = new uint8_t[0x30000];
        for (int i = 0; i < 0x30000; i++)
            rom[i] = (rnd() % 3 == 0) ? 0 : (uint8_t)rnd();
        return rom;
    }

    // Road bitplanes: mostly long runs, some noise
    uint8_t* make_road_rom()
    {
        uint8_t* rom = new uint8_t[0x10000];
        memset(rom, 0, 0x10000);
        for (int row = 0; row < 256; row++)
        {
            const int half = 2 + rnd() % 28;
            for (int plane = 0; plane < 2; plane++)
                for (int b = 0; b < 64; b++)
                {
                    const int d = b < 32 ? 31 - b : b - 32;
                    uint8_t v = (d < half) ? 0x00 : 0xff;
                    if (plane == 1 && d == half) v = 0x0f;
                    if (row % 17 == 0) v = (uint8_t)rnd();
                    rom[row * 0x40 + b + plane * 0x4000] = v;
                }
        }
        return rom;
    }

    void random_sprite_list()
    {
        const int count = 1 + rnd() % 100;
        const int shadow_sprite = rnd() % count;
        int e = 0;
        for (; e < count && e < 127; e++)
        {
            uint16_t d[8];
            const Img& im = imgs[rnd() % imgs.size()];
            const bool flip = rnd() % 2;
            const int hzoom = (rnd() % 3 == 0) ? 0x200 : (0x30 + rnd() % 0x7d0);
            const int vzoom = (rnd() % 3 == 0) ? 0x200 : (0x30 + rnd() % 0x7d0);
            int maxh = (int)(((int64_t)im.rows * 512) / (vzoom < 0x40 ? 0x40 : vzoom));
            if (maxh < 1) maxh = 1;
            if (maxh > 256) maxh = 256;
            const int height = 1 + rnd() % maxh;
            const int top = (int)(rnd() % 300) - 40;
            const int xpos = rnd() % 0x300;
            const uint16_t addr = flip ? im.addr + im.words - 1 : im.addr;

            d[0] = ((im.bank & 7) << 9) | ((top + 0x100) & 0x1ff);
            d[1] = addr;
            d[2] = (im.words & 0x7f) << 9;
            // Only one shadow sprite per frame: overlapping shadows darken
            // twice on the PVR (translucent blend) but once on the arcade.
            d[3] = (3 << 12) | ((e == shadow_sprite) << 14) | (vzoom & 0x7ff);
            d[4] = ((rnd() % 2) << 15) | ((!flip) << 14) | ((rnd() % 2) << 13) | (hzoom & 0x7ff);
            d[5] = ((height - 1) << 8) | (rnd() % 128);
            d[6] = xpos;
            d[7] = 0;
            for (int i = 0; i < 8; i++)
                video.sprite_layer->write((e * 8 + i) * 2, d[i]);
        }
        video.sprite_layer->write(e * 8 * 2, 0x8000);
        video.sprite_layer->swap();
    }

    void random_road()
    {
        for (int i = 0; i < 0x800; i++)
        {
            uint16_t v = rnd();
            if (i < 0x200 && (rnd() % 3)) v &= ~0x800;
            hwroad.write16(i * 2, v);
        }
        hwroad.read_road_control();          // swap into the render buffer
        hwroad.write_road_control(rnd() % 8);
    }

    void random_text()
    {
        for (int i = 0; i < 0x1000; i++)
            video.tile_layer->text_ram[i] = (rnd() % 4 == 0) ? 0 : (uint8_t)rnd();
    }

    void random_tiles()
    {
        for (int i = 0; i < 0x10000; i++)
            video.tile_layer->tile_ram[i] = (uint8_t)rnd();
    }

    uint8_t ref_shadow[S16_WIDTH * S16_HEIGHT];   // 1 = shadowed pixel

    // Software reference for the whole frame, as RGB565 (with shadows)
    void reference_frame(uint16_t* out, int shadow_multi)
    {
        uint16_t* px = video.pixels;
        // The reference draws every tile row: the game's skip list (rows the
        // road covers) is left over from the previous frame
        const uint8_t* const skip = video.tile_layer->skip_lines;
        video.tile_layer->skip_lines = NULL;
        video.tile_layer->update_tile_values();
        (hwroad.*hwroad.render_background)(px);
        video.tile_layer->render_tile_layer(px, 1, 0);
        video.tile_layer->render_tile_layer(px, 0, 0);
        (hwroad.*hwroad.render_foreground)(px);
        video.sprite_layer->render(8);
        video.tile_layer->render_text_layer(px, 1);
        video.tile_layer->skip_lines = skip;

        const uint16_t* lut = static_cast<const uint16_t*>(pvr_selftest_lut());
        for (int i = 0; i < S16_WIDTH * S16_HEIGHT; i++)
        {
            const uint16_t idx = px[i];
            ref_shadow[i] = idx >= S16_PALETTE_ENTRIES;
            if (idx < S16_PALETTE_ENTRIES)
                out[i] = lut[idx];
            else
            {
                // Shadow: marked with bit 12, the underlying colour darkened
                const uint16_t c = lut[idx & 0xfff];
                const uint32_t r = ((c >> 11) & 31) * shadow_multi / 255;
                const uint32_t g = ((c >> 5) & 63) * shadow_multi / 255;
                const uint32_t b = (c & 31) * shadow_multi / 255;
                out[i] = (r << 11) | (g << 5) | b;
            }
        }
    }

    struct Result
    {
        uint32_t exact, close, shadow_close, bad, bad_frames, frames;
    };

    bool use_rtt = true;
    int st_scale = 1;   // render scale under test (2 = pixel-perfect 640x448 path)

    Result run_suite(float offset, int frames, pvr_ptr_t rtt, uint16_t* ref, int shadow_multi)
    {
        Result res = {0, 0, 0, 0, 0, 0};
        pvr_render_set_sample_offset(offset);
        rng_state = 0xC0FFEE;   // same frames for every offset

        for (int f = 0; f < frames; f++)
        {
            random_tiles();
            random_text();
            random_road();
            random_sprite_list();

            // Random palette each frame
            for (uint32_t a = 0; a < 0x2000; a += 2)
            {
                uint32_t pa = a;
                video.write_pal16(&pa, (uint16_t)rnd());
            }

            // Reference first (it scribbles scratch words into sprite RAM,
            // which the PVR path ignores)
            reference_frame(ref, shadow_multi);

#ifdef DREAMCAST_PVR_SELFTEST_DUMP
            if (f == 0 && offset == 0.5f)
            {
                // Show the frame on screen for a while (for screenshots)
                for (int k = 0; k < 400; k++)
                {
                    video.prepare_frame();
                    video.render_frame();
                }
            }
#endif
            const uint16_t* got;
            if (use_rtt)
            {
                pvr_render_set_rtt(rtt, st_scale);
                video.prepare_frame();
                video.render_frame();
                pvr_wait_ready();
                pvr_wait_render_done();
                pvr_render_set_rtt(NULL);
                got = (const uint16_t*)rtt;
            }
            else
            {
                // Render to the screen, then read back the displayed frame
                video.prepare_frame();
                video.render_frame();
                pvr_wait_ready();
                pvr_wait_render_done();
                vid_waitvbl();
                vid_waitvbl();
                const uintptr_t fb = (uintptr_t)pvr_get_front_buffer();
                const uint32_t off = (uint32_t)((fb - PVR_RAM_BASE) / 2);
                const uintptr_t cands[6] = {
                    fb, PVR_RAM_BASE + off, PVR_RAM_INT_BASE + off,
                    PVR_RAM_INT_BASE + (fb - PVR_RAM_BASE), (uintptr_t)vram_s,
                    PVR_RAM_BASE + (PVR_GET(PVR_FB_ADDR) & 0x7fffff) };
                int bestc = 0, bests = -1;
                for (int c = 0; c < 6; c++)
                {
                    const uint16_t* g = (const uint16_t*)cands[c];
                    int m = 0;
                    for (int y = 20; y < 200; y += 9)
                        for (int x = 10; x < 310; x += 5)
                        {
                            const uint16_t a = g[(y + 8) * 320 + x], b = ref[y * 320 + x];
                            if (((a ^ b) & 0xe79c) == 0) m++;   // top bits of each channel
                        }
                    if (f == 0)
                    {
                        ST_LOG("cannonball: selftest fb candidate %d %08lx score %d\n", c, (unsigned long)cands[c], m);
                        ST_LOG("   c%d: %04x %04x %04x %04x %04x %04x %04x %04x  ref %04x %04x %04x %04x\n", c,
                               g[108*320+0], g[108*320+1], g[108*320+2], g[108*320+3], g[108*320+160], g[108*320+161], g[108*320+320], g[108*320+640],
                               ref[100*320+0], ref[100*320+1], ref[100*320+2], ref[100*320+160]);
                    }
                    if (m > bests) { bests = m; bestc = c; }
                }
                got = (const uint16_t*)cands[bestc];
            }
#ifdef DREAMCAST_PVR_SELFTEST_DUMP
            if (f == 0 && offset == 0.5f)
            {
                static char line[S16_WIDTH * 4 + 1];
                static const char hx[] = "0123456789abcdef";
                for (int pass = 0; pass < 2; pass++)
                    for (int y = 0; y < S16_HEIGHT; y++)
                    {
                        const uint16_t* row = pass ? ref + y * S16_WIDTH : got + y * 320;
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
#endif
            uint32_t bad_this = 0;
            for (int y = 0; y < S16_HEIGHT; y++)
            {
                // At 2x, check the first sample of each 2x2 block
                const uint16_t* g0 = got + (y * st_scale) * (320 * st_scale);
                uint16_t g[S16_WIDTH];
                for (int x = 0; x < S16_WIDTH; x++) g[x] = g0[x * st_scale];
                const uint16_t* r = ref + y * S16_WIDTH;
                for (int x = 0; x < S16_WIDTH; x++)
                {
                    const uint16_t want = r[x];
                    const bool is_shadow = ref_shadow[y * S16_WIDTH + x] != 0;
                    if (!is_shadow && g[x] == want)
                    {
                        res.exact++;
                        continue;
                    }
                    // Allow 1 LSB per channel: colour conversion rounding in
                    // emulators (real hardware should be exact).
                    const int dr = (int)((g[x] >> 11) & 31) - (int)((want >> 11) & 31);
                    const int dg = (int)((g[x] >> 5) & 63) - (int)((want >> 5) & 63);
                    const int db = (int)(g[x] & 31) - (int)(want & 31);
                    // Shadows get 2 LSB: emulators may quantise the palette alpha.
                    const int t = is_shadow ? 2 : 1;
                    if (dr >= -t && dr <= t && dg >= -2 * t && dg <= 2 * t && db >= -t && db <= t)
                    {
                        if (is_shadow) res.shadow_close++; else res.close++;
                    }
                    else
                    {
                        if (bad_this < 3)
                            ST_LOG("  offset %.2f frame %d diff at (%d,%d): want %04x%s got %04x\n",
                                   (double)offset, f, x, y, want, is_shadow ? "(shadow)" : "", g[x]);
                        res.bad++;
                        bad_this++;
                    }
                }
            }
            if (bad_this) res.bad_frames++;
            ST_LOG("cannonball: selftest offset %.2f frame %d bad=%lu\n", (double)offset, f, (unsigned long)bad_this);
            res.frames++;
        }
        return res;
    }

    void show_results(const char* lines[], int n, bool pass)
    {
        const int TW = 512, TH = 256;
        uint16_t* buf = (uint16_t*)memalign(32, TW * TH * 2);
        const uint16_t bgc = pass ? 0x0200 : 0x6000;   // dark green / dark red
        for (int i = 0; i < TW * TH; i++) buf[i] = bgc;
        for (int i = 0; i < n; i++)
            bfont_draw_str_ex(buf + (8 + i * 26) * TW + 8, TW, 0xFFFF, bgc, 16, false, lines[i]);

        pvr_ptr_t tex = pvr_mem_malloc(TW * TH * 2);
        pvr_txr_load(buf, tex, TW * TH * 2);
        free(buf);

        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                         TW, TH, tex, PVR_FILTER_NONE);
        cxt.gen.culling = PVR_CULLING_NONE;
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.txr.env = PVR_TXRENV_REPLACE;
        pvr_poly_compile(&hdr, &cxt);

        for (;;)
        {
            pvr_wait_ready();
            pvr_scene_begin();
            pvr_list_begin(PVR_LIST_OP_POLY);
            pvr_prim(&hdr, sizeof(hdr));
            pvr_vertex_t v;
            v.argb = 0xffffffff; v.oargb = 0; v.z = 1.0f;
            v.flags = PVR_CMD_VERTEX;     v.x = 0;   v.y = 0;   v.u = 0;            v.v = 0;            pvr_prim(&v, sizeof(v));
            v.flags = PVR_CMD_VERTEX;     v.x = 640; v.y = 0;   v.u = 320.0f / TW;  v.v = 0;            pvr_prim(&v, sizeof(v));
            v.flags = PVR_CMD_VERTEX;     v.x = 0;   v.y = 480; v.u = 0;            v.v = 240.0f / TH;  pvr_prim(&v, sizeof(v));
            v.flags = PVR_CMD_VERTEX_EOL; v.x = 640; v.y = 480; v.u = 320.0f / TW;  v.v = 240.0f / TH;  pvr_prim(&v, sizeof(v));
            pvr_list_finish();
            pvr_list_begin(PVR_LIST_TR_POLY);
            pvr_list_finish();
            pvr_scene_finish();
        }
    }
}

int pvr_selftest_main()
{
    ST_LOG("cannonball: PVR self-test starting\n");

    config.load();                 // defaults if there is no config.xml
    config.video.hires      = 0;
    config.video.widescreen = 0;
    config.video.shadow     = 0;   // hardware-accurate shadow intensity
    config.engine.fix_bugs  = 0;
    config.sound.enabled    = 0;   // no SDL in the self-test

    ST_LOG("cannonball: selftest config done\n");
    Roms fake;
    fake.sprites.rom = make_sprite_rom();
    ST_LOG("cannonball: selftest sprite rom done\n");
    fake.tiles.rom   = make_tile_rom();
    fake.road.rom    = make_road_rom();
    ST_LOG("cannonball: selftest fake ROMs ready (%d sprite images)\n", (int)imgs.size());

    if (!video.init(&fake, &config.video))
    {
        ST_LOG("cannonball: self-test video.init failed\n");
        return 1;
    }
    video.sprite_layer->set_x_clip(false);
    video.enabled = true;
    ST_LOG("cannonball: selftest video ready\n");

    pvr_ptr_t rtt = pvr_mem_malloc(640 * 448 * 2);

#ifdef DREAMCAST_PVR_SELFTEST_DUMP
    // Minimal RTT probe: OP red full screen, TR green half, TR paletted?
    {
        pvr_poly_cxt_t cxt; pvr_poly_hdr_t hdr; pvr_vertex_t v;
        pvr_wait_ready();
        pvr_scene_begin_rtt(rtt, 320, 240, 320);
        pvr_list_begin(PVR_LIST_OP_POLY);
        pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
        cxt.gen.culling = PVR_CULLING_NONE; cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        pvr_poly_compile(&hdr, &cxt); pvr_prim(&hdr, sizeof(hdr));
        v.argb = 0xffff0000; v.oargb = 0; v.z = 1.0f; v.u = v.v = 0;
        v.flags = PVR_CMD_VERTEX; v.x = 0; v.y = 0; pvr_prim(&v, sizeof(v));
        v.x = 320; v.y = 0; pvr_prim(&v, sizeof(v));
        v.x = 0; v.y = 240; pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX_EOL; v.x = 320; v.y = 240; pvr_prim(&v, sizeof(v));
        pvr_list_finish();
        pvr_list_begin(PVR_LIST_TR_POLY);
        pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
        cxt.gen.culling = PVR_CULLING_NONE; cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS; cxt.depth.write = false;
        pvr_poly_compile(&hdr, &cxt); pvr_prim(&hdr, sizeof(hdr));
        v.argb = 0xff00ff00; v.z = 2.0f;
        {
            // paletted 8x8 texture: all index 1, bank 8 entry 1 = blue
            static uint8_t tx[32] __attribute__((aligned(32)));
            memset(tx, 0x11, 32);
            pvr_ptr_t t = pvr_mem_malloc(32);
            pvr_txr_load(tx, t, 32);
            pvr_set_pal_format(PVR_PAL_ARGB8888);
            pvr_set_pal_entry(8 * 16 + 1, 0xff0000ff);
            pvr_poly_hdr_t h2; pvr_poly_cxt_t c2;
            pvr_poly_cxt_txr(&c2, PVR_LIST_TR_POLY, PVR_TXRFMT_PAL4BPP | PVR_TXRFMT_4BPP_PAL(8) | PVR_TXRFMT_TWIDDLED, 8, 8, t, PVR_FILTER_NONE);
            c2.gen.culling = PVR_CULLING_NONE; c2.depth.comparison = PVR_DEPTHCMP_ALWAYS; c2.depth.write = false;
            c2.txr.env = PVR_TXRENV_REPLACE;
            pvr_poly_compile(&h2, &c2); pvr_prim(&h2, sizeof(h2));
            pvr_vertex_t w; w.argb = 0xffffffff; w.oargb = 0; w.z = 3.0f;
            w.flags = PVR_CMD_VERTEX; w.x = 200; w.y = 0; w.u = 0; w.v = 0; pvr_prim(&w, sizeof(w));
            w.x = 320; w.u = 1; pvr_prim(&w, sizeof(w));
            w.x = 200; w.y = 120; w.u = 0; w.v = 1; pvr_prim(&w, sizeof(w));
            w.flags = PVR_CMD_VERTEX_EOL; w.x = 320; w.u = 1; pvr_prim(&w, sizeof(w));
            pvr_poly_compile(&hdr, &cxt); pvr_prim(&hdr, sizeof(hdr));
        }
        v.flags = PVR_CMD_VERTEX; v.x = 0; v.y = 0; pvr_prim(&v, sizeof(v));
        v.x = 160; v.y = 0; pvr_prim(&v, sizeof(v));
        v.x = 0; v.y = 240; pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX_EOL; v.x = 160; v.y = 240; pvr_prim(&v, sizeof(v));
        pvr_list_finish();
        pvr_scene_finish();
        pvr_wait_ready();
        pvr_wait_render_done();
        const uint16_t* g = (const uint16_t*)rtt;
        ST_LOG("cannonball: RTT probe left=%04x right=%04x paltex=%04x\n", g[100 * 320 + 50], g[200 * 320 + 250], g[60 * 320 + 250]);
    }
#endif
    uint16_t* ref = new uint16_t[S16_WIDTH * S16_HEIGHT];
    const int shadow_multi = (int)((255.0f * shadow::ORIGINAL) + 0.5f);

    const int FRAMES = 40;
    const float offsets[2] = { 0.5f, 0.0f };
    Result r[3];
    for (int i = 0; i < 3; i++)
    {
        st_scale = i == 2 ? 2 : 1;
        r[i] = run_suite(i == 2 ? 0.5f : offsets[i], FRAMES, rtt, ref, shadow_multi);
        ST_LOG("cannonball: selftest %s offset=%.2f frames=%lu exact=%lu within1lsb=%lu shadow_ok=%lu bad=%lu bad_frames=%lu\n",
               i == 2 ? "2x" : "1x", (double)(i == 2 ? 0.5f : offsets[i]), (unsigned long)r[i].frames, (unsigned long)r[i].exact, (unsigned long)r[i].close,
               (unsigned long)r[i].shadow_close, (unsigned long)r[i].bad, (unsigned long)r[i].bad_frames);
    }

    const int best = r[1].bad < r[0].bad ? 1 : 0;
    // Shrunk sprites sample 1/256 texel past the texel edge (pvr_sprite_core
    // sample_bias), so a few pixels take the neighbouring texel: allow 0.1%
    auto ok = [](const Result& x) {
        const uint32_t total = x.exact + x.close + x.shadow_close + x.bad;
        return (uint64_t)x.bad * 1000 <= total;
    };
    const bool pass = ok(r[best]) && ok(r[2]);

    static char l[9][64];
    snprintf(l[0], 64, "CannonBall PVR self-test");
    snprintf(l[1], 64, "%d random frames", FRAMES);
    snprintf(l[2], 64, "offset 0.5 bad: %lu", (unsigned long)r[0].bad);
    snprintf(l[3], 64, "offset 0.0 bad: %lu", (unsigned long)r[1].bad);
    snprintf(l[4], 64, "640x448 bad: %lu", (unsigned long)r[2].bad);
    snprintf(l[5], 64, "exact: %lu", (unsigned long)r[best].exact);
    snprintf(l[6], 64, "within 1: %lu", (unsigned long)r[best].close);
    snprintf(l[7], 64, pass ? "PASS (offset %.1f)" : "FAIL (best %.1f)", (double)offsets[best]);
    const char* lines[8] = { l[0], l[1], l[2], l[3], l[4], l[5], l[6], l[7] };
    ST_LOG("cannonball: selftest %s\n", l[7]);

    show_results(lines, 8, pass);
    return 0;
}

#endif // DREAMCAST_PVR_SELFTEST
