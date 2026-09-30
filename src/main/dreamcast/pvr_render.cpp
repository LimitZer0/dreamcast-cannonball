/***************************************************************************
    Dreamcast native PowerVR renderer. See pvr_render.hpp for an overview.
***************************************************************************/

#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <malloc.h>

#include <dc/pvr.h>
#include <dc/video.h>
#include <kos/dbglog.h>
#include <kos/timer.h>

#include "dreamcast/pvr_render.hpp"
#include "dreamcast/pvr_sprite_core.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "frontend/config.hpp"
#include "frontend/leaderboard.hpp"
#include "globals.hpp"
#include "main.hpp"
#include "engine/outrun.hpp"
#include "engine/oroad.hpp"
#include "engine/ocrash.hpp"
#include "engine/oferrari.hpp"
#include "engine/osprites.hpp"
#include "dreamcast/pvr_hood.h"

#ifndef DREAMCAST_PVR_SAMPLE_OFFSET
// Where, within a screen pixel, the PowerVR evaluates texture coordinates.
// 0.5 = pixel centre (matches Flycast). If sprites look shifted by a pixel on
// real hardware, rebuild with -DDREAMCAST_PVR_SAMPLE_OFFSET=0.0
#define DREAMCAST_PVR_SAMPLE_OFFSET 0.5f
#endif

#define PVR_TRACE(...) dbglog(DBG_INFO, __VA_ARGS__)

using namespace pvrspr;

namespace
{
    // The game (320x224) is rendered at native resolution into a texture,
    // then shown on a 640x480 display either pixel perfect (2x = 640x448 with
    // black borders) or stretched to fill the screen.
    const int GAME_W    = 320;
    const int GAME_H    = 224;
    const int GAME_Y    = 0;        // y offset of the game inside its render target
    // Output (framebuffer) size: 640x480, or 320x240 in the low resolution
    // mode, where the video hardware doubles every pixel and the GPU fills a
    // quarter of the pixels (pixel perfect display without scanlines)
    int OUT_W = 640;
    int OUT_H = 480;
    // RES_640: 640x480. RES_240: 320x240, the video hardware doubles every
    // pixel and line (optional see-through scanline overlay on every other
    // game line). RES_LINES (VGA only): 320x480, pixels doubled across, the
    // game on the even lines and solid black bars on the odd ones; the bars
    // are opaque and in front, so the GPU skips everything behind them and
    // the cost is about the same as 320x240.
    enum { RES_640, RES_240, RES_LINES };
    int res = RES_640;
    bool low_res = false;       // res != RES_640
    bool lines_mode = false;    // res == RES_LINES: depth test everything against the bars
    const float BAR_Z = 10.0f;

    pvr_ptr_t game_tex  = NULL;     // 320x224 render target, debug compare builds only

    // Vertex offset that maps "sample at pixel centre" onto the PVR's actual
    // sampling position (no-op when it samples at centres).
    float VOFF = DREAMCAST_PVR_SAMPLE_OFFSET - 0.5f;

    // Self-test hook: render into this texture instead of the screen
    pvr_ptr_t rtt_target = NULL;
    int rtt_scale = 1;

    // PVR 4bpp palette banks (64 banks x 16 entries, ARGB8888)
    const int TEXT_BANK_BASE   = 0;    // 8 banks: text layer colours 0-7
    const int SPRITE_BANK_BASE = 8;    // 56 banks: sprite palette/shadow combos
    const int SPRITE_BANKS     = 55;
    const int HOOD_BANK        = 63;   // in-car view bonnet

    // Text tile atlas: 8192 tiles of 8x8, 128 per row
    const int ATLAS_W = 1024;
    const int ATLAS_H = 512;

    // Background texture: strided RGB565, 320 wide
    const int BG_TEX_W = 512;
    const int BG_TEX_H = 256;

    // Keeps texture lookups off the outer edge of a quad's texel range
    const float TEXEL_INSET = 1.0f / 64.0f;

    // VRAM budget for cached sprite images
    const uint32_t SPRITE_CACHE_BUDGET = 3 * 1024 * 1024;

    struct CacheEntry
    {
        pvr_ptr_t tex;
        uint32_t  bytes;
        uint32_t  last_used;
        uint16_t  rows;      // source rows decoded
        uint16_t  w;         // image width in pixels
        uint16_t  tw, th;    // texture size
    };

    // ------------------------------------------------------------------------
    // Renderer state (single instance)
    // ------------------------------------------------------------------------
    pvr_ptr_t bg_tex    = NULL;
    uint16_t* bg_buf    = NULL;     // RAM staging for the background
    uint8_t   bg_line_mask[S16_HEIGHT]; // 1 = line already RGB565 (road)
    pvr_ptr_t atlas_tex = NULL;
    uint32_t  atlas_version = 0xffffffff;

    std::unordered_map<uint64_t, CacheEntry> cache;   // image_key, bit 40: Ferrari badge fixed
    uint32_t cache_bytes = 0;

    // Textures replaced while still referenced by the frame being built;
    // freed once that frame has finished rendering.
    std::vector<std::pair<pvr_ptr_t, uint32_t> > deferred_free;
    uint32_t frame_no = 0;

    std::vector<uint8_t> stage_img;   // decoded sprite image (1 byte / pixel)
    std::vector<uint8_t> stage_tex;   // twiddled 4bpp texture

    // Palette lookups built from the S16 palette
    uint16_t rgb565[S16_PALETTE_ENTRIES];
    uint32_t argb888[S16_PALETTE_ENTRIES];
    uint32_t shadow_argb = 0x5E000000;  // translucent black for shadows

    // Per frame sprite palette bank allocation, indexed by pal*2 + shadow
    uint32_t bank_stamp[256];
    uint8_t  bank_of[256];
    uint8_t  bank_owner[SPRITE_BANKS];
    int      banks_used;

    // Stats
    uint32_t st_frames, st_sprites, st_new_tex, st_overflow, st_skipped, st_text;
    uint64_t st_convert_us, st_sprite_us, st_submit_us, st_wait_us;
    uint64_t st_last_report;

    SprCmd cmds[128];

    inline uint32_t argb_from5(uint32_t r, uint32_t g, uint32_t b)
    {
        r = (r << 3) | (r >> 2);
        g = (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    float cur_z = 1.0f;   // background 1.0, translucent layers 2.0

    // Cut-out sprites: normal sprites go in the punch-through list (the GPU
    // only shades the front-most pixel), each at its own depth so the draw
    // order is kept; shadows still blend in the translucent list.
#ifndef DREAMCAST_PVR_CUTOUT_DEFAULT
#define DREAMCAST_PVR_CUTOUT_DEFAULT false
#endif
#ifndef DREAMCAST_PVR_NATIVE_DEFAULT
#define DREAMCAST_PVR_NATIVE_DEFAULT true
#endif
    // Native: use the 320x240 video mode when the display allows it
    bool native = DREAMCAST_PVR_NATIVE_DEFAULT;
    bool cutout = DREAMCAST_PVR_CUTOUT_DEFAULT;
    const float SPRITE_Z0 = 2.0f, SPRITE_DZ = 1.0f / 1024.0f;
    const float HOOD_Z = 3.0f, TEXT_Z = 4.0f;

    // Timing for the on-screen readout
    uint32_t ro_convert, ro_sprite, ro_submit, ro_wait, ro_frames;

    // In-car view: the scene is zoomed about the bottom centre and a bonnet
    // is drawn where the car would be (see tools/hood/make_hood.py)
    const float INCAR_ZOOM = 0.5f;      // 1.5x: 3 screen pixels per game pixel at 2x
    const int   HOOD_TW = 256, HOOD_TH = 32;
    pvr_ptr_t   hood_tex = NULL;
    float       incar_t = 0.0f;         // bonnet: 0 = hidden, 1 = fully in place
    float       zoom_t  = 0.0f;         // zoom: 0 = none, 1 = full

    inline uint32_t argb_from_s16(uint16_t w)
    {
        const uint32_t r = ((w & 0xF) << 1) | ((w >> 12) & 1);
        const uint32_t g = (((w >> 4) & 0xF) << 1) | ((w >> 13) & 1);
        const uint32_t b = (((w >> 8) & 0xF) << 1) | ((w >> 14) & 1);
        return 0xFF000000 | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
    }


    // Game (320x224) -> screen transform for the current scene.
    //   screen = game * scale + origin
    // Sprite UV maths assumes one sample at each game pixel centre. When
    // scaled, the first sample inside a game pixel sits 'bias' before the
    // centre, so sprite texture coordinates are shifted by that amount:
    // unzoomed sprites stay exact at any integer scale and zoomed sprites get
    // half-pixel precision.
    struct Xform { float sx, sy, ox, oy, bx, by; };
    Xform xf = { 1, 1, 0, 0, 0, 0 };

    void set_xform(float x0, float y0, float x1, float y1)
    {
        xf.sx = (x1 - x0) / GAME_W;
        xf.sy = (y1 - y0) / GAME_H;
        xf.ox = x0;
        xf.oy = y0;
        xf.bx = 0.5f / xf.sx - 0.5f;
        xf.by = 0.5f / xf.sy - 0.5f;
    }

    // Vertices go straight to the Tile Accelerator through the store queues
    // (KOS "direct rendering"); only valid between pvr_list_begin/finish.
    inline void submit_quad(float x0, float y0, float x1, float y1,
                            float u0, float v0, float u1, float v1,
                            bool centre_sampled = false)
    {
        const float z = cur_z;
        // Sprite UVs put texel edges exactly on game pixel centres, so shift
        // their mapping (not the geometry) by the bias. Background and text
        // map texels to whole game pixels and need no correction.
        if (centre_sampled && x1 != x0)
        {
            const float du = (u1 - u0) / (x1 - x0) * -xf.bx;
            u0 += du; u1 += du;
        }
        if (centre_sampled && y1 != y0)
        {
            const float dv = (v1 - v0) / (y1 - y0) * -xf.by;
            v0 += dv; v1 += dv;
        }
        x0 = x0 * xf.sx + xf.ox + VOFF;
        x1 = x1 * xf.sx + xf.ox + VOFF;
        y0 = y0 * xf.sy + xf.oy + VOFF;
        y1 = y1 * xf.sy + xf.oy + VOFF;

        pvr_vertex_t* v = (pvr_vertex_t*)pvr_dr_target();
        v->flags = PVR_CMD_VERTEX; v->x = x0; v->y = y0; v->z = z; v->u = u0; v->v = v0;
        v->argb = 0xFFFFFFFF; v->oargb = 0;
        pvr_dr_commit(v);

        v = (pvr_vertex_t*)pvr_dr_target();
        v->flags = PVR_CMD_VERTEX; v->x = x1; v->y = y0; v->z = z; v->u = u1; v->v = v0;
        v->argb = 0xFFFFFFFF; v->oargb = 0;
        pvr_dr_commit(v);

        v = (pvr_vertex_t*)pvr_dr_target();
        v->flags = PVR_CMD_VERTEX; v->x = x0; v->y = y1; v->z = z; v->u = u0; v->v = v1;
        v->argb = 0xFFFFFFFF; v->oargb = 0;
        pvr_dr_commit(v);

        v = (pvr_vertex_t*)pvr_dr_target();
        v->flags = PVR_CMD_VERTEX_EOL; v->x = x1; v->y = y1; v->z = z; v->u = u1; v->v = v1;
        v->argb = 0xFFFFFFFF; v->oargb = 0;
        pvr_dr_commit(v);
    }

    static_assert(sizeof(pvr_poly_hdr_t) == 32, "PVR header must be 32 bytes");

    inline void submit_header(const pvr_poly_hdr_t* hdr)
    {
        uint32_t* d = (uint32_t*)pvr_dr_target();
        const uint32_t* s = (const uint32_t*)hdr;
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        d[4] = s[4]; d[5] = s[5]; d[6] = s[6]; d[7] = s[7];
        pvr_dr_commit(d);
    }

    void compile_txr_header(pvr_poly_hdr_t* hdr, pvr_list_t list, int fmt,
                            int tw, int th, pvr_ptr_t tex,
                            int depthcmp = PVR_DEPTHCMP_ALWAYS, bool depthwrite = false)
    {
        pvr_poly_cxt_t cxt;
        pvr_poly_cxt_txr(&cxt, list, fmt, tw, th, tex, PVR_FILTER_NONE);
        cxt.gen.culling      = PVR_CULLING_NONE;
        cxt.gen.shading      = false;
        // Solid scanlines: everything tests depth, so what the bars cover is
        // never shaded
        if (lines_mode && depthcmp == PVR_DEPTHCMP_ALWAYS)
            depthcmp = PVR_DEPTHCMP_GEQUAL;
        cxt.depth.comparison = (pvr_depthcmp_mode_t)depthcmp;
        cxt.depth.write      = depthwrite;
        cxt.txr.env          = PVR_TXRENV_REPLACE;
        cxt.txr.uv_clamp     = PVR_UVCLAMP_UV;
        if (list == PVR_LIST_TR_POLY)
        {
            // Premultiplied alpha: every palette entry is either opaque,
            // fully transparent black, or shadow (black), so it already is
            // premultiplied. This keeps edges clean if the texture is ever
            // filtered (e.g. an emulator forcing bilinear filtering).
            cxt.blend.src = PVR_BLEND_ONE;
            cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
        }
        pvr_poly_compile(hdr, &cxt);
    }

    // Build the text-layer tile atlas from the converted tile graphics
    void build_atlas(const uint32_t* tiles)
    {
        init_twid_table();
        const uint32_t bytes = ATLAS_W * ATLAS_H / 2;
        std::vector<uint8_t> buf(bytes, 0);

        for (uint32_t n = 0; n < 0x2000; n++)
        {
            const uint32_t ox = (n & 127) * 8;
            const uint32_t oy = (n >> 7) * 8;
            for (uint32_t py = 0; py < 8; py++)
            {
                const uint32_t row = tiles[(n << 3) + py];
                if (!row) continue;
                for (uint32_t px = 0; px < 8; px++)
                {
                    const uint8_t p = (row >> (28 - 4 * px)) & 0xf;
                    if (!p) continue;
                    const uint32_t idx = twiddle_index(ox + px, oy + py, ATLAS_W, ATLAS_H);
                    buf[idx >> 1] |= (idx & 1) ? (uint8_t)(p << 4) : p;
                }
            }
        }

        if (!atlas_tex)
            atlas_tex = pvr_mem_malloc(bytes);
        if (atlas_tex)
            pvr_txr_load(buf.data(), atlas_tex, bytes);
    }

    void evict_for(uint32_t needed)
    {
        if (cache_bytes + needed <= SPRITE_CACHE_BUDGET)
            return;

        // Oldest first, never anything used this frame
        std::vector<std::pair<uint32_t, uint64_t> > victims; // (last_used, key)
        victims.reserve(cache.size());
        for (auto& kv : cache)
            if (kv.second.last_used != frame_no)
                victims.push_back(std::make_pair(kv.second.last_used, kv.first));
        std::sort(victims.begin(), victims.end());

        for (size_t i = 0; i < victims.size() && cache_bytes + needed > SPRITE_CACHE_BUDGET; i++)
        {
            auto it = cache.find(victims[i].second);
            pvr_mem_free(it->second.tex);
            cache_bytes -= it->second.bytes;
            cache.erase(it);
        }
    }

    // Returns the cache entry for this sprite, decoding/uploading if needed.
    //
    // Only the rows the sprite chip actually reads are decoded; the last row
    // is repeated to the texture edge. Scaled output samples a little past
    // the last row, and decoding further would show whatever follows the
    // image in the sprite ROM as a stray line.
    // ------------------------------------------------------------------------
    // Player's car badge and number plate
    //
    // The game draws the Ferrari turning left as the right-turn frames
    // mirrored, so the badge and the number plate on its tail read backwards.
    // With MIRROR CAR BADGE off, a mirrored Ferrari frame gets its own copy
    // of the texture with those two areas mirrored back.
    // ------------------------------------------------------------------------
    uint32_t ferrari_keys[48];
    int ferrari_key_count = -1;

    bool is_ferrari_frame(uint32_t key)
    {
        if (ferrari_key_count < 0)
        {
            // The nine driving frames (3 turns x 3 slopes) and the skid
            // frames, size 0, read forwards and backwards (the two
            // image_key forms)
            ferrari_key_count = 0;
            RomLoader* r = roms.rom0p;
            for (int i = 0; i < 9 + 12; i++)       // driving frames, then skid frames
            {
                const uint32_t fa = i < 9 ? r->read32(outrun.adr.sprite_ferrari_frames + i * 8)
                                          : r->read32(outrun.adr.sprite_skid_frames + (i - 9) * 8);
                const uint32_t pitch = r->read8(fa + 5), bank = r->read8(fa + 7) % video.sprite_layer->pvr_num_banks();
                const uint32_t off = r->read16(fa + 8);
                const uint32_t k0 = off | (bank << 16) | (pitch << 21);
                bool dup = false;
                for (int j = 0; j < ferrari_key_count; j++) dup |= ferrari_keys[j] == k0;
                if (dup) continue;
                ferrari_keys[ferrari_key_count++] = k0;
                ferrari_keys[ferrari_key_count++] = ((off + pitch - 1) & 0xffff) | (bank << 16) | (1u << 20) | (pitch << 21);
            }
        }
        for (int i = 0; i < ferrari_key_count; i++)
            if (ferrari_keys[i] == key) return true;
        return false;
    }

    // Mirror the badge and the number plate of a decoded Ferrari frame in
    // place. Found from the art: the tail lights are the rows with blue
    // (colour 0xB) runs; the badge is the black (7) pixels between them; the
    // plate is the longest black run (a plate edge) below them, extended up
    // and down over the rows of black and dark (0xC, 1) pixels.
    static void mirror_box(uint8_t* img, int stride, int x0, int y0, int x1, int y1)
    {
        for (int y = y0; y < y1; y++)
        {
            uint8_t* r = img + (size_t)y * stride;
            for (int a = x0, b = x1 - 1; a < b; a++, b--) std::swap(r[a], r[b]);
        }
    }

    static void fix_ferrari_badge(uint8_t* img, int w, int h, int stride, bool reversed)
    {
        // The skid frame (136 wide, the car sliding sideways) has the badge
        // and plate at fixed places (columns as read forwards; a frame read
        // backwards has them from the other end)
        if (w > 100)
        {
            static const int BOX[2][4] = { { 34, 19, 41, 26 },     // badge
                                           { 33, 28, 42, 32 } };   // plate lettering (the plate is
                                                                   // skewed: its outline stays)
            for (int b = 0; b < 2; b++)
            {
                int x0 = BOX[b][0], x1 = BOX[b][2];
                if (reversed) { const int t = w - x1; x1 = w - x0; x0 = t; }
                if (x0 >= 0 && x1 <= w && BOX[b][3] <= h)
                    mirror_box(img, stride, x0, BOX[b][1], x1, BOX[b][3]);
            }
            return;
        }
        auto px = [&](int x, int y) { return img[(size_t)y * stride + x]; };
        int t0 = -1, t1 = -1;
        for (int y = 0; y < h; y++)
            for (int x = 0; x + 4 <= w; x++)
                if (px(x, y) == 0xB && px(x + 1, y) == 0xB && px(x + 2, y) == 0xB && px(x + 3, y) == 0xB)
                {
                    if (t0 < 0) t0 = y;
                    t1 = y;
                    break;
                }
        if (t0 < 0) return;

        // Inner edges of the tail lights: end of the first black run and
        // start of the last one on the first tail light row
        int lx = -1, rx = -1;
        for (int x = 0; x < w; x++)
        {
            const bool k = px(x, t0) == 7, prev = x > 0 && px(x - 1, t0) == 7;
            if (k && !prev && lx >= 0) rx = x;              // start of a later run
            if (!k && prev && lx < 0) lx = x;               // end of the first run
        }
        if (lx < 0 || rx <= lx + 4) return;

        // Badge
        int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
        for (int y = std::max(0, t0 - 2); y < std::min(h, t1 + 3); y++)
            for (int x = lx + 2; x < rx - 2; x++)
                if (px(x, y) == 7)
                {
                    bx0 = std::min(bx0, x); bx1 = std::max(bx1, x + 1);
                    by0 = std::min(by0, y); by1 = std::max(by1, y + 1);
                }
        if (bx1 > bx0 && bx1 - bx0 <= 12)
            mirror_box(img, stride, bx0, by0, bx1, by1);

        // Number plate
        int py = -1, px0 = 0, px1 = 0;
        for (int y = t1 + 1; y < h; y++)
            for (int x = 0; x < w; )
            {
                if (px(x, y) != 7) { x++; continue; }
                int e = x;
                while (e < w && px(e, y) == 7) e++;
                if (e - x >= 10 && e - x > px1 - px0) { py = y; px0 = x; px1 = e; }
                x = e;
            }
        if (py < 0) return;
        auto plate_row = [&](int y) {
            bool any = false;
            for (int x = px0; x < px1; x++)
            {
                const uint8_t c = px(x, y);
                if (c == 7) any = true;
                else if (c != 0xC && c != 1) return false;
            }
            return any;
        };
        int y0 = py, y1 = py + 1;
        while (y0 - 1 > t1 && plate_row(y0 - 1)) y0--;
        while (y1 < h && plate_row(y1)) y1++;
        mirror_box(img, stride, px0, y0, px1, y1);
    }

    CacheEntry* resolve_sprite(const SprCmd& s, const uint32_t* spritedata)
    {
        // A mirrored Ferrari frame with the badge and plate the right way
        // round has its own texture (key bit 40)
        const bool badge_fix = !config.video.mirror_badge && is_ferrari_frame(image_key(s)) &&
                               ((s.flip ^ (s.xdelta < 0 ? 1 : 0)) != 0);
        const uint64_t key = image_key(s) | (badge_fix ? (1ull << 40) : 0);
        int rows = rows_needed(s);
        if (rows > MAX_ROWS) rows = MAX_ROWS;

        const uint32_t* bank = spritedata + 0x10000 * s.bank;

        auto it = cache.find(key);
        if (it != cache.end() && rows <= it->second.rows)
        {
            it->second.last_used = frame_no;
            return &it->second;
        }

        int w = measure_sprite(bank, s.addr, s.pitch, s.flip, rows);
        if (it != cache.end() && it->second.w > w)
            w = it->second.w;
        const int tw = pow2_at_least(w);
        const int th = pow2_at_least(rows);
        if (tw > 1024 || th > 1024)
            return NULL;

        const uint32_t bytes = (uint32_t)tw * th / 2;

        if (stage_img.size() < (size_t)tw * th) stage_img.resize((size_t)tw * th);
        if (stage_tex.size() < bytes)           stage_tex.resize(bytes);

        decode_sprite(bank, s.addr, s.pitch, s.flip, rows, stage_img.data(), tw);
        if (badge_fix)
        {
            fix_ferrari_badge(stage_img.data(), w, rows, tw, s.flip != 0);
#ifdef DREAMCAST_DEBUG_INCAR
            printf("BADGEFIX key=%08x frame=%u\n", image_key(s), frame_no);
#endif
        }
        // Repeat the last row down to the texture edge (see decode_sprite)
        for (int r = rows; r < th; r++)
            memcpy(stage_img.data() + (size_t)r * tw, stage_img.data() + (size_t)(rows - 1) * tw, tw);
        twiddle_4bpp(stage_img.data(), tw, th, tw, stage_tex.data(), tw, th);

        // Drop the old (smaller) version. If an earlier sprite in this frame
        // already references it, keep the memory alive until next frame.
        if (it != cache.end())
        {
            if (it->second.last_used == frame_no)
                deferred_free.push_back(std::make_pair(it->second.tex, it->second.bytes));
            else
            {
                pvr_mem_free(it->second.tex);
                cache_bytes -= it->second.bytes;
            }
            cache.erase(it);
        }

        evict_for(bytes);
        pvr_ptr_t tex = pvr_mem_malloc(bytes);
        if (!tex)
        {
            // Fragmentation: flush everything not used this frame and retry
            evict_for(SPRITE_CACHE_BUDGET);
            tex = pvr_mem_malloc(bytes);
            if (!tex)
                return NULL;
        }
        pvr_txr_load(stage_tex.data(), tex, bytes);
        st_new_tex++;

        CacheEntry e;
        e.tex = tex;
        e.bytes = bytes;
        e.last_used = frame_no;
        e.rows = (uint16_t)rows;
        e.w = (uint16_t)w;
        e.tw = (uint16_t)tw;
        e.th = (uint16_t)th;
        cache_bytes += bytes;
        return &(cache[key] = e);
    }

    int sprite_bank(const SprCmd& s)
    {
        const int idx = (s.pal << 1) | (s.shadow & 1);
        if (bank_stamp[idx] == frame_no)
            return bank_of[idx];

        int b;
        if (banks_used < SPRITE_BANKS)
            b = banks_used++;
        else
        {
            // Out of palette banks: reuse the first one (wrong colours, rare)
            st_overflow++;
            b = 0;
        }
        bank_stamp[idx] = frame_no;
        bank_of[idx] = (uint8_t)b;
        bank_owner[b] = (uint8_t)idx;
        return b;
    }

    void write_palettes()
    {
        // Text layer: colour c of bank n = S16 colour n*8 + c
        // (hwtiles::render8x8_tile_mask ignores TILEMAP_COLOUR_OFFSET)
        for (int n = 0; n < 8; n++)
        {
            const int base = n << 3;
            const uint32_t pe = (TEXT_BANK_BASE + n) * 16;
            pvr_set_pal_entry(pe, 0);
            for (int c = 1; c < 16; c++)
                pvr_set_pal_entry(pe + c, argb888[(base + c) & 0xfff]);
        }

        // In-car bonnet: the car's body colours and highlight
        if (incar_t > 0.0f)
        {
            uint16_t body[7];
            osprites.car_body_colours(oferrari.ferrari_pal, body);
            const uint32_t pe = HOOD_BANK * 16;
            pvr_set_pal_entry(pe, 0);
            for (int c = 0; c < 7; c++)
                pvr_set_pal_entry(pe + 1 + c, argb_from_s16(body[c]));
            // Badge (the same on every car)
            static const uint32_t BADGE[7] = { 0xFFFFD000, 0xFFD1AB00, 0xFFEFE4B0, 0xFF4F4100,
                                               0xFF000000, 0xFF8A7100, 0xFF4F4F4F };
            for (int c = 0; c < 7; c++) pvr_set_pal_entry(pe + 8 + c, BADGE[c]);
            pvr_set_pal_entry(pe + 15, 0);
        }

        // Sprites
        for (int b = 0; b < banks_used; b++)
        {
            const int idx    = bank_owner[b];
            const int pal    = idx >> 1;
            const int shadow = idx & 1;
            const int base   = 0x800 + (pal << 4);
            const uint32_t pe = (SPRITE_BANK_BASE + b) * 16;
            for (int c = 0; c < 16; c++)
            {
                uint32_t v;
                if (c == 0 || c == 15)             v = 0;
                else if (shadow && c == 0xa)        v = shadow_argb;
                else                                v = argb888[base + c];
                pvr_set_pal_entry(pe + c, v);
            }
        }
    }

    void submit_background()
    {
        pvr_poly_hdr_t hdr;
        compile_txr_header(&hdr, PVR_LIST_OP_POLY,
                           PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_X32_STRIDE,
                           BG_TEX_W, BG_TEX_H, bg_tex,
                           PVR_DEPTHCMP_ALWAYS, true);   // sets the depth the cut-outs test against
        submit_header(&hdr);

        const float w = (float)config.s16_width;
        const float h = (float)S16_HEIGHT;
        submit_quad(0, GAME_Y, w, GAME_Y + h,
                    TEXEL_INSET / BG_TEX_W, TEXEL_INSET / BG_TEX_H,
                    (w - TEXEL_INSET) / BG_TEX_W, (h - TEXEL_INSET) / BG_TEX_H);
    }

    // A sprite ready to submit: everything resolved before the scene starts,
    // so all VRAM and palette updates happen before any TA traffic.
    struct SpriteDraw
    {
        pvr_ptr_t tex;
        uint16_t tw, th;
        uint8_t bank;
        uint8_t shadow;     // has shadow pixels (blended)
        Quad q;
    };
    SpriteDraw draws[128];
    int draw_count = 0;

    void prepare_sprites(hwsprites* spr)
    {
        draw_count = 0;
        const int n = extract_sprites(spr->pvr_list(), spr->pvr_list_words(), 8,
                                      config.s16_x_off, spr->pvr_num_banks(), cmds, 128);
        const uint32_t* data = spr->pvr_data();
        float cx0 = spr->pvr_clip_x1();
        float cx1 = spr->pvr_clip_x2();
        // Clip window is only set once the game first calls set_x_clip()
        if (cx0 < 0.0f || cx1 > config.s16_width || cx1 <= cx0)
        {
            cx0 = 0.0f;
            cx1 = (float)config.s16_width;
        }

        for (int i = 0; i < n; i++)
        {
            const SprCmd& s = cmds[i];

            // Cheap reject before touching the cache: vertical extent
            const int ytop = s.ydelta > 0 ? s.top : s.top - s.height + 1;
            if (ytop >= S16_HEIGHT || ytop + s.height <= 0)
                continue;

            CacheEntry* e = resolve_sprite(s, data);
            if (!e) { st_skipped++; continue; }

            SpriteDraw& d = draws[draw_count];
            sprite_quad(s, e->w, d.q);
            if (!clip_quad(d.q, cx0, 0.0f, cx1, (float)S16_HEIGHT))
                continue;

            d.tex  = e->tex;
            d.tw   = e->tw;
            d.th   = e->th;
            d.bank = (uint8_t)(SPRITE_BANK_BASE + sprite_bank(s));
            d.shadow = s.shadow & 1;
            draw_count++;
        }
    }

    // list: PVR_LIST_TR_POLY (blended: every sprite, or with only_shadow the
    // shadow sprites over the cut-outs) or PVR_LIST_PT_POLY (cut-outs)
    void submit_sprites(pvr_list_t list = PVR_LIST_TR_POLY, bool only_shadow = false)
    {
        const bool depth = list == PVR_LIST_PT_POLY || only_shadow;
        for (int i = 0; i < draw_count; i++)
        {
            const SpriteDraw& d = draws[i];
            if (only_shadow && !d.shadow) continue;
            pvr_poly_hdr_t hdr;
            compile_txr_header(&hdr, list,
                               PVR_TXRFMT_PAL4BPP | PVR_TXRFMT_4BPP_PAL(d.bank) | PVR_TXRFMT_TWIDDLED,
                               d.tw, d.th, d.tex,
                               depth ? PVR_DEPTHCMP_GEQUAL : PVR_DEPTHCMP_ALWAYS,
                               list == PVR_LIST_PT_POLY);
            submit_header(&hdr);
            if (depth) cur_z = SPRITE_Z0 + i * SPRITE_DZ;

            const float itw = 1.0f / d.tw, ith = 1.0f / d.th;
            submit_quad(d.q.x0, d.q.y0 + GAME_Y, d.q.x1, d.q.y1 + GAME_Y,
                        d.q.u0 * itw, d.q.v0 * ith, d.q.u1 * itw, d.q.v1 * ith, true);
            st_sprites++;
        }
    }

    void submit_hood(pvr_list_t list = PVR_LIST_TR_POLY)
    {
        if (!hood_tex) return;
        pvr_poly_hdr_t hdr;
        const bool pt = list == PVR_LIST_PT_POLY;
        compile_txr_header(&hdr, list,
                           PVR_TXRFMT_PAL4BPP | PVR_TXRFMT_4BPP_PAL(HOOD_BANK) | PVR_TXRFMT_TWIDDLED,
                           HOOD_TW, HOOD_TH, hood_tex,
                           pt ? PVR_DEPTHCMP_GEQUAL : PVR_DEPTHCMP_ALWAYS, pt);
        if (pt) cur_z = HOOD_Z;
        submit_header(&hdr);
        // Slides up from the bottom as the view changes; bounces with the
        // car (the off-road shake moves the hidden Ferrari sprite)
        int shake = oferrari.spr_ferrari->y - 221;
        if (shake < -2) shake = -2; else if (shake > 2) shake = 2;
        // The art runs HOOD_BELOW rows past the bottom of the screen, so the
        // bounce never opens a gap there
        const float dy = (1.0f - incar_t) * (HOOD_H - HOOD_BELOW) + shake;
        const float x0 = (GAME_W - HOOD_W) / 2.0f, y0 = GAME_H - (HOOD_H - HOOD_BELOW) + dy;
        submit_quad(x0, y0 + GAME_Y, x0 + HOOD_W, y0 + HOOD_H + GAME_Y,
                    0.0f, 0.0f, (float)HOOD_W / HOOD_TW, (float)HOOD_H / HOOD_TH);
    }

    // CRT FRAME: rounded black corners on the game area (like the curved
    // glass of an arcade monitor), and with SHADE the picture darkening
    // gently towards its edges. Flat-shaded / Gouraud black polygons in the
    // blended list, over everything: a few hundred pixels of fill.
    void submit_crt_frame(float gx0, float gy0, float gx1, float gy1)
    {
        if (config.video.crt <= 0) return;
        const float sy = (gy1 - gy0) / GAME_H;             // screen pixels per game line
        const float sx = (gx1 - gx0) / GAME_W;

        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
        cxt.gen.culling      = PVR_CULLING_NONE;
        cxt.gen.shading      = PVR_SHADE_GOURAUD;
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write      = false;
        cxt.blend.src        = PVR_BLEND_SRCALPHA;
        cxt.blend.dst        = PVR_BLEND_INVSRCALPHA;
        pvr_poly_compile(&hdr, &cxt);
        submit_header(&hdr);

        auto quad = [](float x0, float y0, float x1, float y1,
                       uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11)
        {
            const float xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
            const uint32_t cs[4] = { c00, c10, c01, c11 };
            for (int i = 0; i < 4; i++)
            {
                pvr_vertex_t* v = (pvr_vertex_t*)pvr_dr_target();
                v->flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
                v->x = xs[i]; v->y = ys[i]; v->z = 3.0f;
                v->u = 0; v->v = 0;
                v->argb = cs[i]; v->oargb = 0;
                pvr_dr_commit(v);
            }
        };

        // Corner radius in game pixels
        const float R = 20.0f;

        // Edge shade: a band around the picture fading from dark at the edge
        // to clear, following the rounded corners (one strip around a
        // rounded rectangle: outer edge = the corner outline, inner edge a
        // more rounded rectangle inset by the band width), so the sides
        // don't overlap and brighten or darken the corners
        if (config.video.crt == 2)
        {
            const float B   = 22.0f;                       // band width (game pixels)
            const float RIN = R + B;                       // inner corner radius
            const uint32_t E = 0x38000000, C = 0x00000000;
            const float W = (float)GAME_W, H = (float)GAME_H;
            // Corner centres (game pixels) for the outer and inner outlines,
            // clockwise from top left, and the angle each corner starts at
            const float ocx[4] = { R, W - R, W - R, R },      ocy[4] = { R, R, H - R, H - R };
            const float icx[4] = { B + RIN, W - B - RIN, W - B - RIN, B + RIN };
            const float icy[4] = { B + RIN, B + RIN, H - B - RIN, H - B - RIN };
            const float a0[4]  = { 3.14159265f, 4.71238898f, 0.0f, 1.57079633f };
            const int STEPS = 8;
            const int total = 4 * (STEPS + 1) + 1;
            int k = 0;
            for (int c = 0; c < 4 + 1; c++)
            {
                const int cc = c & 3;
                const int n = c < 4 ? STEPS + 1 : 1;       // close the loop
                for (int i = 0; i < n; i++, k++)
                {
                    const float t  = a0[cc] + 1.57079633f * i / STEPS;
                    const float dx = cosf(t), dy = sinf(t);
                    // The outer edge runs 2 pixels outside the corner
                    // outline, under the black corners drawn after it, so
                    // no pixel on the curve is left uncovered by both
                    const float ox = gx0 + (ocx[cc] + (R + 2.0f) * dx) * sx, oy = gy0 + (ocy[cc] + (R + 2.0f) * dy) * sy;
                    const float ix = gx0 + (icx[cc] + RIN * dx) * sx, iy = gy0 + (icy[cc] + RIN * dy) * sy;
                    for (int j = 0; j < 2; j++)
                    {
                        pvr_vertex_t* v = (pvr_vertex_t*)pvr_dr_target();
                        v->flags = (k == total - 1 && j == 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
                        v->x = j ? ix : ox; v->y = j ? iy : oy; v->z = 3.0f;
                        v->u = 0; v->v = 0;
                        v->argb = j ? C : E; v->oargb = 0;
                        pvr_dr_commit(v);
                    }
                }
            }
        }

        // Corners: black outside a quarter circle, one screen line at a time
        // (lines exactly on the screen's pixels, so the edge stays crisp)
        const float r = R * sy;
        const int lines = (int)(r + 0.5f);
        const uint32_t K = 0xFF000000;
        for (int i = 0; i < lines; i++)
        {
            const float dy = r - (i + 0.5f);               // distance above the circle's centre
            const float w  = (r - sqrtf(r * r - dy * dy)) * (sx / sy);
            if (w <= 0.0f) continue;
            const float ya = gy0 + i, yb = ya + 1.0f;          // top corners
            const float yc = gy1 - i - 1.0f, yd = yc + 1.0f;   // bottom corners
            quad(gx0, ya, gx0 + w, yb, K, K, K, K);
            quad(gx1 - w, ya, gx1, yb, K, K, K, K);
            quad(gx0, yc, gx0 + w, yd, K, K, K, K);
            quad(gx1 - w, yc, gx1, yd, K, K, K, K);
        }
    }

    // Black bars over everything outside the game area (screen coordinates),
    // for when the zoomed scene spills past it
    void submit_border_mask(float gx0, float gy0, float gx1, float gy1)
    {
        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
        cxt.gen.culling      = PVR_CULLING_NONE;
        cxt.gen.shading      = PVR_SHADE_FLAT;
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write      = false;
        cxt.blend.src        = PVR_BLEND_ONE;
        cxt.blend.dst        = PVR_BLEND_ZERO;
        pvr_poly_compile(&hdr, &cxt);
        submit_header(&hdr);
        const float W = (float)OUT_W, H = (float)OUT_H;
        const float r[4][4] = { { 0, 0, W, gy0 }, { 0, gy1, W, H },
                                { 0, gy0, gx0, gy1 }, { gx1, gy0, W, gy1 } };
        for (int q = 0; q < 4; q++)
        {
            if (r[q][2] <= r[q][0] || r[q][3] <= r[q][1]) continue;
            const float xs[4] = { r[q][0], r[q][2], r[q][0], r[q][2] };
            const float ys[4] = { r[q][1], r[q][1], r[q][3], r[q][3] };
            for (int i = 0; i < 4; i++)
            {
                pvr_vertex_t* v = (pvr_vertex_t*)pvr_dr_target();
                v->flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
                v->x = xs[i]; v->y = ys[i]; v->z = 2.5f;
                v->u = 0; v->v = 0;
                v->argb = 0xFF000000; v->oargb = 0;
                pvr_dr_commit(v);
            }
        }
    }

    // Solid scanlines (320x480): an opaque black bar on every odd line of the
    // game area. In the opaque list with depth written they keep the
    // background out of those lines, and everything else depth-tests against
    // them. That worked in Flycast, but on a real Dreamcast the sprites and
    // text were still drawn over the lines, so the bars are drawn again,
    // solid black, at the end of the blended list, over everything.
    void submit_scanline_bars(pvr_list_t list = PVR_LIST_OP_POLY)
    {
        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        pvr_poly_cxt_col(&cxt, list);
        cxt.gen.culling      = PVR_CULLING_NONE;
        cxt.gen.shading      = PVR_SHADE_FLAT;
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write      = list == PVR_LIST_OP_POLY;
        if (list == PVR_LIST_TR_POLY)
        {
            cxt.blend.src = PVR_BLEND_ONE;      // solid black
            cxt.blend.dst = PVR_BLEND_ZERO;
        }
        pvr_poly_compile(&hdr, &cxt);
        submit_header(&hdr);

        const float x0 = (float)(int)(xf.ox + 0.5f);
        const float x1 = (float)(int)(GAME_W * xf.sx + xf.ox + 0.5f);
        const int top = (int)(xf.oy + 0.5f);
        const int bottom = (int)(GAME_H * xf.sy + xf.oy + 0.5f);
        for (int y = top + 1; y < bottom; y += 2)
        {
            const float ya = (float)y, yb = (float)(y + 1);
            const float xs[4] = { x0, x1, x0, x1 }, ys[4] = { ya, ya, yb, yb };
            for (int i = 0; i < 4; i++)
            {
                pvr_vertex_t* v = (pvr_vertex_t*)pvr_dr_target();
                v->flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
                v->x = xs[i]; v->y = ys[i]; v->z = BAR_Z;
                v->u = 0; v->v = 0;
                v->argb = 0xFF000000; v->oargb = 0;
                pvr_dr_commit(v);
            }
        }
    }

    // Scanlines: darken every second line of the 480-line display.
    // Drawn as flat-shaded translucent black bars on whole output lines
    // (no texture), so they stay crisp at any emulator internal resolution
    // and in FULL mode. In PIXEL PERFECT mode (game area starts on an even
    // line, 2 lines per game line) this is exactly one dark line per game line.
    void build_scanline_texture() {}

    void submit_scanlines()
    {
        const int pct = config.video.scanlines;
        if (pct <= 0) return;
        const uint32_t a = (uint32_t)(pct > 100 ? 100 : pct) * 255 / 100;

        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
        cxt.gen.culling      = PVR_CULLING_NONE;
        cxt.gen.shading      = PVR_SHADE_FLAT;
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write      = false;
        cxt.blend.src        = PVR_BLEND_SRCALPHA;
        cxt.blend.dst        = PVR_BLEND_INVSRCALPHA;
        pvr_poly_compile(&hdr, &cxt);
        submit_header(&hdr);

        const float x0 = (float)(int)(xf.ox + 0.5f);
        const float x1 = (float)(int)(GAME_W * xf.sx + xf.ox + 0.5f);
        int top = (int)(xf.oy + 0.5f);
        const int bottom = (int)(GAME_H * xf.sy + xf.oy + 0.5f);
        const uint32_t col = a << 24;

        for (int y = top + 1; y < bottom; y += 2)
        {
            const float ya = (float)y, yb = (float)(y + 1);
            const float xs[4] = { x0, x1, x0, x1 }, ys[4] = { ya, ya, yb, yb };
            for (int i = 0; i < 4; i++)
            {
                pvr_vertex_t* v = (pvr_vertex_t*)pvr_dr_target();
                v->flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
                v->x = xs[i]; v->y = ys[i]; v->z = 3.0f;
                v->u = 0; v->v = 0;
                v->argb = col; v->oargb = 0;
                pvr_dr_commit(v);
            }
        }
    }

    // Leaderboard QR code over the high score screen: one quad with a
    // 128x128 RGB565 texture (one texel per module, white border), point
    // sampled at a whole-number scale so every module is the same size.
    const int QR_TEX   = 128;
    const int QR_QUIET = 3;                 // white border, in modules
    pvr_ptr_t qr_tex    = NULL;
    unsigned  qr_serial = 0;                // leaderboard QR last uploaded
    int       qr_total  = 0;                // modules incl. border, 0 = none

    // Before pvr_scene_begin: upload a new QR code
    void prepare_qr()
    {
        int size = 0;
        const uint8_t* m = leaderboard::overlay(&size);
        if (!m || size + QR_QUIET * 2 > QR_TEX) { qr_total = 0; return; }
        if (!qr_tex)
        {
            qr_tex = pvr_mem_malloc(QR_TEX * QR_TEX * 2);
            if (!qr_tex) { qr_total = 0; return; }
        }
        if (leaderboard::overlay_serial() != qr_serial)
        {
            static uint16_t img[QR_TEX * QR_TEX] __attribute__((aligned(32)));
            for (int i = 0; i < QR_TEX * QR_TEX; i++) img[i] = 0xFFFF;
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                    if (m[y * size + x]) img[(y + QR_QUIET) * QR_TEX + x + QR_QUIET] = 0x0000;
            pvr_txr_load(img, qr_tex, sizeof(img));
            qr_serial = leaderboard::overlay_serial();
        }
        qr_total = size + QR_QUIET * 2;
    }

    void submit_qr()
    {
        if (!qr_total) return;
        int scale = 360 / qr_total;         // leaves the bottom text row clear
        if (scale < 1) scale = 1;
        const int px = qr_total * scale;
        const float x0 = (float)((OUT_W - px) / 2);
        const float y0 = (float)((OUT_H - px) / 2 - 12);
        const float uv = (float)qr_total / QR_TEX;

        pvr_poly_cxt_t cxt;
        pvr_poly_hdr_t hdr;
        pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                         QR_TEX, QR_TEX, qr_tex, PVR_FILTER_NONE);
        cxt.gen.culling      = PVR_CULLING_NONE;
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write      = false;
        cxt.txr.env          = PVR_TXRENV_REPLACE;
        cxt.blend.src        = PVR_BLEND_ONE;
        cxt.blend.dst        = PVR_BLEND_ZERO;
        pvr_poly_compile(&hdr, &cxt);
        submit_header(&hdr);

        const float xs[4] = { x0, x0 + px, x0, x0 + px };
        const float ys[4] = { y0, y0, y0 + px, y0 + px };
        const float us[4] = { 0, uv, 0, uv }, vs[4] = { 0, 0, uv, uv };
        for (int i = 0; i < 4; i++)
        {
            pvr_vertex_t* v = (pvr_vertex_t*)pvr_dr_target();
            v->flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
            v->x = xs[i]; v->y = ys[i]; v->z = 10.0f;
            v->u = us[i]; v->v = vs[i];
            v->argb = 0xFFFFFFFF; v->oargb = 0;
            pvr_dr_commit(v);
        }
    }

    // Text layer (priority 1 tiles only, as Video::prepare_frame does)
    void submit_text(hwtiles* til, pvr_list_t list = PVR_LIST_TR_POLY)
    {
        if (list == PVR_LIST_PT_POLY) cur_z = TEXT_Z;
        if (!atlas_tex) return;

        static uint16_t tiles_by_colour[8][64 * 32];
        static uint16_t pos_by_colour[8][64 * 32];
        int count[8] = {0};

        const uint8_t* text_ram = til->text_ram;
        const uint16_t bank = til->pvr_text_bank();
        const int width = til->pvr_width();

        for (int my = 0; my < 28; my++)
        {
            for (int mx = 24; mx < 64; mx++)
            {
                const int ti = (my * 64 + mx) * 2;
                uint16_t code = (text_ram[ti] << 8) | text_ram[ti + 1];
                if (!(code & 0x8000)) continue;
                const int colour = (code >> 9) & 7;
                code &= 0x1ff;
                code += bank * 0x1000;
                code &= 0x1fff;
                if (!code) continue;
                const int x = 8 * mx - 192;
                if (x >= width) continue;
                const int c = count[colour]++;
                tiles_by_colour[colour][c] = code;
                pos_by_colour[colour][c] = (uint16_t)((mx << 8) | my);
            }
        }

        for (int colour = 0; colour < 8; colour++)
        {
            if (!count[colour]) continue;
            pvr_poly_hdr_t hdr;
            compile_txr_header(&hdr, list,
                               PVR_TXRFMT_PAL4BPP | PVR_TXRFMT_4BPP_PAL(TEXT_BANK_BASE + colour) | PVR_TXRFMT_TWIDDLED,
                               ATLAS_W, ATLAS_H, atlas_tex,
                               list == PVR_LIST_PT_POLY ? PVR_DEPTHCMP_GEQUAL : PVR_DEPTHCMP_ALWAYS,
                               list == PVR_LIST_PT_POLY);
            submit_header(&hdr);

            for (int i = 0; i < count[colour]; i++)
            {
                const uint16_t code = tiles_by_colour[colour][i];
                const int mx = pos_by_colour[colour][i] >> 8;
                const int my = pos_by_colour[colour][i] & 0xff;
                const float x = (float)(8 * mx - 192 + config.s16_x_off + (my < 32 ? til->text_row_xoff[my] : 0));
                const float y = (float)(8 * my + GAME_Y);
                const float u = ((code & 127) * 8) / (float)ATLAS_W;
                const float v = ((code >> 7) * 8) / (float)ATLAS_H;
                // Inset by a fraction of a texel so upscaled output (or an
                // emulator's higher internal resolution) never samples the
                // neighbouring tile in the atlas.
                const float eu = TEXEL_INSET / ATLAS_W, ev = TEXEL_INSET / ATLAS_H;
                submit_quad(x, y, x + 8, y + 8,
                            u + eu, v + ev, u + 8.0f / ATLAS_W - eu, v + 8.0f / ATLAS_H - ev);
                st_text++;
            }
        }
    }
    // ------------------------------------------------------------------------
    // Video mode + PVR setup. The low resolution mode (320x240, doubled by the
    // video hardware) needs the PVR set up again: everything in video memory
    // (sprite cache, text atlas, textures) is rebuilt afterwards.
    // ------------------------------------------------------------------------
    bool setup_pvr(int r)
    {
        res = r;
        low_res = r != RES_640;
        lines_mode = r == RES_LINES;
        OUT_W = low_res ? 320 : 640;
        OUT_H = r == RES_240 ? 240 : 480;
        if (r == RES_LINES)
        {
            // 640x480 VGA timing, each pixel shown twice across, no line doubling
            static vid_mode_t m;
            m = vid_builtin[DM_640x480_VGA];
            m.width = 320;
            m.flags |= VID_PIXELDOUBLE;
            m.pm = PM_RGB565;
            vid_set_mode_ex(&m);
        }
        else
            vid_set_mode(r == RES_240 ? DM_320x240 : DM_640x480, PM_RGB565);

        pvr_init_params_t params;
        memset(&params, 0, sizeof(params));
        params.opb_sizes[0] = PVR_BINSIZE_16;   // opaque
        params.opb_sizes[1] = PVR_BINSIZE_0;
        params.opb_sizes[2] = PVR_BINSIZE_32;   // translucent: sprites + text
        params.opb_sizes[3] = PVR_BINSIZE_0;
        params.opb_sizes[4] = PVR_BINSIZE_32;   // punch-through: cut-out sprites + text
        params.vertex_buf_size   = 256 * 1024;
        params.dma_enabled       = 0;
        params.fsaa_enabled      = 0;
        params.autosort_disabled = 1;             // draw translucent polys in submission order
        params.opb_overflow_count = 2;

        if (pvr_init(&params) < 0)
        {
            PVR_TRACE("cannonball: pvr_init failed\n");
            return false;
        }

        pvr_set_bg_color(0.0f, 0.0f, 0.0f);
        // Cut-outs: palette alpha is 0 (transparent), 255 (solid) or the
        // shadow's partial alpha, which the cut-out pass skips
        PVR_SET(PVR_PT_ALPHA_REF, 0xF0);
        // The game's colours are 15-bit, so dithering would only add noise
        vid_set_dithering(false);
        pvr_set_pal_format(PVR_PAL_ARGB8888);
        pvr_txr_set_stride(S16_WIDTH);

        bg_tex   = pvr_mem_malloc(BG_TEX_W * BG_TEX_H * 2);
        game_tex = pvr_mem_malloc(BG_TEX_W * BG_TEX_H * 2);
        if (!bg_tex || !game_tex)
        {
            PVR_TRACE("cannonball: PVR renderer out of memory\n");
            return false;
        }
#ifdef DREAMCAST_PVR_COMPARE
        // Debug compare build renders into a readable texture
        rtt_target = game_tex;
#endif
        hood_tex = pvr_mem_malloc(HOOD_TW * HOOD_TH / 2);
        if (hood_tex)
        {
            std::vector<uint8_t> t(HOOD_TW * HOOD_TH / 2);
            twiddle_4bpp(HOOD_PIXELS, HOOD_W, HOOD_H, HOOD_W, t.data(), HOOD_TW, HOOD_TH);
            pvr_txr_load(t.data(), hood_tex, t.size());
        }
        PVR_TRACE("cannonball: PVR %dx%d, VRAM free %lu\n", OUT_W, OUT_H,
                  (unsigned long)pvr_mem_available());
        return true;
    }

    void teardown_pvr()
    {
        pvr_shutdown();
        cache.clear();
        cache_bytes = 0;
        deferred_free.clear();
        atlas_tex = NULL;
        atlas_version = 0xffffffff;
        qr_tex = NULL;
        qr_serial = 0;
        bg_tex = game_tex = hood_tex = NULL;
        memset(bank_stamp, 0xff, sizeof(bank_stamp));
    }
}

// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------

Render::Render()
{
    spr = NULL;
    til = NULL;
    layers_enabled = false;
    initialised = false;
    shadow_multi = 160;
    for (int i = 0; i < S16_PALETTE_ENTRIES; i++)
    {
        rgb565[i]  = 0;
        argb888[i] = 0xFF000000;
    }
}

Render::~Render()
{
}

void Render::set_sources(hwsprites* sprites, hwtiles* tiles)
{
    spr = sprites;
    til = tiles;
}

bool Render::init(int src_width, int src_height, int scale, int video_mode, int scanlines)
{
    this->src_width  = src_width;
    this->src_height = src_height;
    this->scale      = scale;
    this->video_mode = video_mode;
    this->scanlines  = scanlines;
    scn_width  = OUT_W;
    scn_height = OUT_H;

    if (src_width > GAME_W)
    {
        PVR_TRACE("cannonball: PVR renderer supports 320 wide output only (got %d)\n", src_width);
        return false;
    }

    if (!initialised)
    {
        PVR_TRACE("cannonball: PVR renderer init\n");
        bg_buf = (uint16_t*)memalign(32, S16_WIDTH * S16_HEIGHT * 2);
        if (!bg_buf || !setup_pvr(false))
        {
            PVR_TRACE("cannonball: PVR renderer setup failed\n");
            return false;
        }
        memset(bg_buf, 0, S16_WIDTH * S16_HEIGHT * 2);
        build_scanline_texture();
        cache.reserve(1024);
        memset(bank_stamp, 0xff, sizeof(bank_stamp));
        st_last_report = timer_ms_gettime64();
        initialised = true;

        PVR_TRACE("cannonball: PVR renderer ready, VRAM free %lu\n",
                  (unsigned long)pvr_mem_available());
    }
    return true;
}

void Render::disable()
{
}

bool Render::start_frame()
{
    return initialised;
}

void Render::begin_background()
{
    memset(bg_line_mask, 0, sizeof(bg_line_mask));
}

uint16_t* Render::bg_buffer()      { return bg_buf; }
const uint16_t* Render::lut()      { return rgb565; }
uint8_t* Render::line_mask()       { return bg_line_mask; }

void Render::draw_frame(uint16_t* pixels)
{
    // Convert the CPU-rendered background (sky + tilemaps) to RGB565.
    // Road lines were already written as RGB565 by HWRoad.
    const uint64_t t0 = timer_us_gettime64();
    const int w = S16_WIDTH;
    for (int y = 0; y < S16_HEIGHT; y++)
    {
        if (bg_line_mask[y]) continue;
        uint32_t* dst = (uint32_t*)(bg_buf + y * w);
        const uint16_t* src = pixels + y * w;
        for (int x = 0; x < w; x += 4)
        {
            const uint32_t a = rgb565[src[0] & 0xfff];
            const uint32_t b = rgb565[src[1] & 0xfff];
            const uint32_t c = rgb565[src[2] & 0xfff];
            const uint32_t d = rgb565[src[3] & 0xfff];
            dst[0] = a | (b << 16);
            dst[1] = c | (d << 16);
            dst += 2;
            src += 4;
        }
    }
    st_convert_us += timer_us_gettime64() - t0;
    ro_convert += (uint32_t)(timer_us_gettime64() - t0);
}

bool Render::finalize_frame()
{
    if (!initialised) return false;

    uint64_t t0 = timer_us_gettime64();
    pvr_wait_ready();
    pvr_wait_render_done();   // nothing may read VRAM/palettes we touch below
    uint64_t t1 = timer_us_gettime64();

    // Video mode for the pixel perfect display: 320x240, or 320x480 with
    // solid scanlines on VGA (SCANLINES: LINES; a TV gets 240p, which has
    // real scanlines). ORIGINAL and the QR code screen keep 640x480.
    {
        static int cable = -2;
        if (cable == -2) { cable = vid_check_cable(); PVR_TRACE("cannonball: video cable %d (0 = VGA)\n", cable); }
        int qr_size = 0;
        int want = RES_640;
        if (native && !rtt_target && config.video.mode == video_settings_t::MODE_FULL &&
            !leaderboard::overlay(&qr_size))
            want = (config.video.scanlines > 100 && cable == CT_VGA) ? RES_LINES : RES_240;
        if (want != res)
        {
            teardown_pvr();
            if (!setup_pvr(want))
                return false;
            t1 = timer_us_gettime64();
        }
    }
    st_wait_us += t1 - t0;

    frame_no++;
    banks_used = 0;

    for (size_t i = 0; i < deferred_free.size(); i++)
    {
        pvr_mem_free(deferred_free[i].first);
        cache_bytes -= deferred_free[i].second;
    }
    deferred_free.clear();

    {
        const int sm = shadow_multi < 0 ? 0 : (shadow_multi > 255 ? 255 : shadow_multi);
        shadow_argb = (uint32_t)(255 - sm) << 24;
    }

    pvr_txr_load(bg_buf, bg_tex, S16_WIDTH * S16_HEIGHT * 2);
    prepare_qr();

    if (til && til->tiles_version != atlas_version)
    {
        build_atlas(til->pvr_tiles());
        atlas_version = til->tiles_version;
    }

    // Bonnet view; never in test render targets. The bonnet slides in over a
    // few frames and goes at once when the view changes or the race ends
    // (time up, or the goal's ending sequence); the zoom eases in and out.
    {
        const bool game = !rtt_target && cannonball::state == cannonball::STATE_GAME;
        const bool racing = outrun.game_state >= GS_START1 && outrun.game_state <= GS_BONUS &&
                            oferrari.state != OFerrari::FERRARI_END_SEQ;
        const bool on = game && racing && oroad.hood_view();
        const float step = 1.0f / 12.0f;
        incar_t = on ? (incar_t + step > 1.0f ? 1.0f : incar_t + step) : 0.0f;
        if (!game)    zoom_t = 0.0f;
        else if (on)  zoom_t = zoom_t + step > 1.0f ? 1.0f : zoom_t + step;
        else          zoom_t = zoom_t - step < 0.0f ? 0.0f : zoom_t - step;
    }

    // Resolve sprites (texture uploads) and write palettes before the scene
    draw_count = 0;
    if (layers_enabled && spr)
    {
        const uint64_t ts = timer_us_gettime64();
        prepare_sprites(spr);
        st_sprite_us += timer_us_gettime64() - ts;
        ro_sprite += (uint32_t)(timer_us_gettime64() - ts);
    }
    write_palettes();

    // Drawing to the screen (not a test texture): overlays go on top
    const bool on_screen = !rtt_target;

    if (rtt_target)
    {
        // Self-test / compare: native 320x224 into a texture
        set_xform(0, 0, GAME_W * rtt_scale, GAME_H * rtt_scale);
        pvr_scene_begin_rtt(rtt_target, GAME_W * rtt_scale, GAME_H * rtt_scale, GAME_W * rtt_scale);
    }
    else
    {
        // video.mode:  MODE_STRETCH = FULL: fill the 640x480 screen
        //              MODE_WINDOW  = ORIGINAL: 1:1 320x224 box in the middle
        //              MODE_FULL    = PIXEL PERFECT: 2x (640x448), black borders
        if (config.video.mode == video_settings_t::MODE_STRETCH)
            set_xform(0, 0, OUT_W, OUT_H);
        else if (config.video.mode == video_settings_t::MODE_WINDOW)
            set_xform((OUT_W - GAME_W) / 2, (OUT_H - GAME_H) / 2,
                      (OUT_W + GAME_W) / 2, (OUT_H + GAME_H) / 2);
        else
        {
            // 320 wide modes: the video hardware doubles across (and down at 240)
            const int kx = low_res ? 1 : 2, ky = res == RES_240 ? 1 : 2;
            set_xform((OUT_W - GAME_W * kx) / 2, (OUT_H - GAME_H * ky) / 2,
                      (OUT_W + GAME_W * kx) / 2, (OUT_H + GAME_H * ky) / 2);
        }
        pvr_scene_begin();
    }

    // In-car view: zoom the game (not the HUD) about its bottom centre
    auto zoomed_of = [](Xform x) {
        const float z = 1.0f + INCAR_ZOOM * zoom_t;
        x.ox += (GAME_W / 2) * (1.0f - z) * x.sx;
        x.oy += (GAME_Y + GAME_H) * (1.0f - z) * x.sy;
        x.sx *= z;
        x.sy *= z;
        x.bx = 0.5f / x.sx - 0.5f;
        x.by = 0.5f / x.sy - 0.5f;
        return x;
    };
    const Xform base = xf;
    const bool zoom_here = zoom_t > 0.0f;
    const bool text_here = true;
    if (zoom_here) xf = zoomed_of(base);
    const Xform scene_xf = xf;

    pvr_list_begin(PVR_LIST_OP_POLY);
    cur_z = 1.0f;
    submit_background();
    if (lines_mode)
    {
        const Xform zx = xf;
        xf = base;                          // bars on the screen's lines, not zoomed
        submit_scanline_bars();
        xf = zx;
    }
    pvr_list_finish();
    cur_z = 2.0f;

    const bool hood = zoom_t > 0.0f && incar_t > 0.0f && layers_enabled && !ocrash.is_flip();
    if (cutout)
    {
        // Cut-outs: sprites, bonnet, then the HUD text (not zoomed)
        pvr_list_begin(PVR_LIST_PT_POLY);
        submit_sprites(PVR_LIST_PT_POLY);
        if (hood) submit_hood(PVR_LIST_PT_POLY);
        xf = base;
        if (text_here && layers_enabled && til)
            submit_text(til, PVR_LIST_PT_POLY);
        pvr_list_finish();

        // Blended: shadows (depth tested against the cut-outs), then overlays
        xf = scene_xf;
        pvr_list_begin(PVR_LIST_TR_POLY);
        submit_sprites(PVR_LIST_TR_POLY, true);
        xf = base;
        cur_z = 5.0f;
        if (zoom_here && on_screen)
            submit_border_mask(xf.ox, xf.oy + GAME_Y * xf.sy,
                               GAME_W * xf.sx + xf.ox, (GAME_Y + GAME_H) * xf.sy + xf.oy);
        if (on_screen)
        {
            if (!lines_mode) submit_scanlines();
            submit_qr();
        }
        if (lines_mode) submit_scanline_bars(PVR_LIST_TR_POLY);
        if (on_screen && !qr_total)
            submit_crt_frame(xf.ox, xf.oy + GAME_Y * xf.sy,
                             GAME_W * xf.sx + xf.ox, (GAME_Y + GAME_H) * xf.sy + xf.oy);
        pvr_list_finish();
    }
    else
    {
        pvr_list_begin(PVR_LIST_TR_POLY);
        submit_sprites();
        if (hood) submit_hood();
        xf = base;
        if (zoom_here && on_screen)
            submit_border_mask(xf.ox, xf.oy + GAME_Y * xf.sy,
                               GAME_W * xf.sx + xf.ox, (GAME_Y + GAME_H) * xf.sy + xf.oy);
        if (text_here && layers_enabled && til)
            submit_text(til);
        if (on_screen)
        {
            if (!lines_mode) submit_scanlines();
            submit_qr();
        }
        if (lines_mode) submit_scanline_bars(PVR_LIST_TR_POLY);
        if (on_screen && !qr_total)
            submit_crt_frame(xf.ox, xf.oy + GAME_Y * xf.sy,
                             GAME_W * xf.sx + xf.ox, (GAME_Y + GAME_H) * xf.sy + xf.oy);
        pvr_list_finish();
    }

    pvr_scene_finish();

    st_submit_us += timer_us_gettime64() - t1;
    ro_submit += (uint32_t)(timer_us_gettime64() - t1);
    ro_wait   += (uint32_t)(t1 - t0);
    ro_frames++;

    // Periodic stats on the debug console
    st_frames++;
    const uint64_t now = timer_ms_gettime64();
    if (now - st_last_report >= 5000)
    {
        const uint32_t f = st_frames ? st_frames : 1;
        PVR_TRACE("cannonball: pvrperf fps=%lu convert_us=%lu sprite_us=%lu submit_us=%lu wait_us=%lu "
                  "sprites/f=%lu text/f=%lu newtex=%lu cache=%luKB entries=%lu bankovf=%lu skipped=%lu\n",
                  (unsigned long)(st_frames * 1000 / (now - st_last_report)),
                  (unsigned long)(st_convert_us / f), (unsigned long)(st_sprite_us / f),
                  (unsigned long)(st_submit_us / f), (unsigned long)(st_wait_us / f),
                  (unsigned long)(st_sprites / f), (unsigned long)(st_text / f),
                  (unsigned long)st_new_tex, (unsigned long)(cache_bytes >> 10),
                  (unsigned long)cache.size(), (unsigned long)st_overflow, (unsigned long)st_skipped);
        st_frames = st_sprites = st_new_tex = st_overflow = st_skipped = st_text = 0;
        st_convert_us = st_sprite_us = st_submit_us = st_wait_us = 0;
        st_last_report = now;
    }
    return true;
}

void Render::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
    adr >>= 1;
    adr &= 0xfff;
    const uint16_t r = (r1 & 0x1F);
    const uint16_t g = ((g1 & 0x1F) << 1) | ((g1 & 0x10) >> 4);
    const uint16_t b = (b1 & 0x1F);
    rgb565[adr]  = (r << 11) | (g << 5) | b;
    argb888[adr] = argb_from5(r1 & 0x1f, g1 & 0x1f, b1 & 0x1f);
}

// ----------------------------------------------------------------------------
// Test hooks (used by dreamcast/pvr_selftest.cpp)
// ----------------------------------------------------------------------------
void pvr_render_set_sample_offset(float offset) { VOFF = offset - 0.5f; }
void pvr_render_set_rtt(void* tex, int scale)  { rtt_target = (pvr_ptr_t)tex; rtt_scale = scale; }
const void* pvr_selftest_lut()                { return rgb565; }
void* pvr_render_game_tex()                    { return game_tex; }
void pvr_render_set_cutout(bool on)            { cutout = on; }
void pvr_render_set_native(bool on)            { native = on; }
bool pvr_render_native()                       { return native; }
bool pvr_render_cutout()                       { return cutout; }
void pvr_render_take_timing(uint32_t out[5])
{
    out[0] = ro_convert; out[1] = ro_sprite; out[2] = ro_submit; out[3] = ro_wait; out[4] = ro_frames;
    ro_convert = ro_sprite = ro_submit = ro_wait = ro_frames = 0;
}
