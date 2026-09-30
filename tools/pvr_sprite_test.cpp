// Desktop test: checks the Dreamcast PVR sprite path (decode -> twiddle ->
// textured quad with point sampling) against the original software sprite
// rasteriser from hwsprites.cpp, pixel for pixel, on randomly generated
// sprite ROM data and sprite lists.
//
// Build: g++ -O2 -I../src/main/dreamcast pvr_sprite_test.cpp ../src/main/dreamcast/pvr_sprite_core.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <random>
#include "pvr_sprite_core.hpp"

using namespace pvrspr;

static const int W = 320, H = 224;
static const uint32_t SPRITES_LENGTH = 0x100000 >> 2;
static const int RAM_WORDS = 128 * 8;
static const uint16_t PAL_ENTRIES = 0x1000;

static uint32_t sprites[SPRITES_LENGTH];
static uint16_t ramBuff[RAM_WORDS];

// ---------------------------------------------------------------------------
// Reference: hwsprites::render() from CannonBall (lores, no widescreen)
// ---------------------------------------------------------------------------
#define draw_pixel()                                                   \
{                                                                      \
    if (x >= 0 && x < W && pix != 0 && pix != 15)                      \
    {                                                                  \
        if (shadow && pix == 0xa)                                      \
        {                                                              \
            pPixel[x] &= 0xfff;                                        \
            pPixel[x] += PAL_ENTRIES;                                  \
        }                                                              \
        else                                                           \
            pPixel[x] = (pix | color);                                 \
    }                                                                  \
}

static void ref_render(uint16_t* pixels_out, uint8_t priority)
{
    uint16_t ram[RAM_WORDS];
    memcpy(ram, ramBuff, sizeof(ram));
    const uint32_t numbanks = SPRITES_LENGTH / 0x10000;
    for (uint16_t data = 0; data < RAM_WORDS; data += 8)
    {
        if ((ram[data+0] & 0x8000) != 0) break;
        uint32_t sprpri  = 1 << ((ram[data+3] >> 12) & 3);
        if (sprpri != priority) continue;
        int16_t hide    = (ram[data+0] & 0x5000);
        int32_t height  = (ram[data+5] >> 8) + 1;
        if (hide != 0 || height == 0) continue;
        int16_t bank    = (ram[data+0] >> 9) & 7;
        int32_t top     = (ram[data+0] & 0x1ff) - 0x100;
        uint32_t addr    = ram[data+1];
        int32_t pitch  = ((ram[data+2] >> 1) | ((ram[data+4] & 0x1000) << 3)) >> 8;
        int32_t xpos    =  ram[data+6];
        uint8_t shadow  = (ram[data+3] >> 14) & 1;
        int32_t vzoom    = ram[data+3] & 0x7ff;
        int32_t ydelta = ((ram[data+4] & 0x8000) != 0) ? 1 : -1;
        int32_t flip   = (~ram[data+4] >> 14) & 1;
        int32_t xdelta = ((ram[data+4] & 0x2000) != 0) ? 1 : -1;
        int32_t hzoom    = ram[data+4] & 0x7ff;
        int32_t color   = 0x800 + ((ram[data+5] & 0x7f) << 4);
        int32_t x, y, ytarget, yacc = 0, pix;
        if (xpos < 0x80 && xdelta < 0) xpos += 0x200;
        xpos -= 0xbe;
        ram[data+7] = addr;
        if (numbanks) bank %= numbanks;
        const uint32_t* spritedata = sprites + 0x10000 * bank;
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;
        ytarget = top + ydelta * height;
        for (y = top; y != ytarget; y += ydelta)
        {
            if (y >= 0 && y < H)
            {
                uint16_t* pPixel = &pixels_out[y * W];
                int32_t xacc = 0;
                if (flip == 0)
                {
                    ram[data+7] = (addr - 1);
                    for (x = xpos; (xdelta > 0 && x < W) || (xdelta < 0 && x >= 0); )
                    {
                        uint32_t pixels = spritedata[++ram[data+7]];
                        for (int sh = 28; sh >= 0; sh -= 4)
                        {
                            pix = (pixels >> sh) & 0xf;
                            while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        }
                        if ((pixels & 0x000000f0) == 0x000000f0) break;
                    }
                }
                else
                {
                    ram[data+7] = (addr + 1);
                    for (x = xpos; (xdelta > 0 && x < W) || (xdelta < 0 && x >= 0); )
                    {
                        uint32_t pixels = spritedata[--ram[data+7]];
                        for (int sh = 0; sh <= 28; sh += 4)
                        {
                            pix = (pixels >> sh) & 0xf;
                            while (xacc < 0x200) { draw_pixel(); x += xdelta; xacc += hzoom; } xacc -= 0x200;
                        }
                        if ((pixels & 0x0f000000) == 0x0f000000) break;
                    }
                }
            }
            yacc += vzoom;
            addr += pitch * (yacc >> 9);
            yacc &= 0x1ff;
        }
    }
}

// ---------------------------------------------------------------------------
// PVR emulation: textured quad, point sampling at pixel centres, UV clamp.
// Mimics the GPU: per-pixel u/v from the plane through the quad's vertices.
// ---------------------------------------------------------------------------
static int g_max_rows_seen = 0;

static void pvr_render(uint16_t* pixels_out, uint8_t priority, float sample_offset)
{
    static SprCmd cmds[128];
    const int n = extract_sprites(ramBuff, RAM_WORDS, priority, 0, SPRITES_LENGTH / 0x10000, cmds, 128);
    std::vector<uint8_t> img, tex;

    for (int i = 0; i < n; i++)
    {
        const SprCmd& s = cmds[i];
        const uint32_t* bankdata = sprites + 0x10000 * s.bank;
        const int rows = rows_needed(s);
        if (rows > g_max_rows_seen) g_max_rows_seen = rows;
        const int w  = measure_sprite(bankdata, s.addr, s.pitch, s.flip, rows);
        const int tw = pow2_at_least(w);
        const int th = pow2_at_least(rows);
        // Like the renderer: decode all th rows (speculative rows beyond
        // rows_needed are truncated at tw and never sampled)
        img.assign((size_t)tw * th, 0xEE);
        decode_sprite(bankdata, s.addr, s.pitch, s.flip, th, img.data(), tw);
        if (measure_rows(bankdata, s.addr, s.pitch, s.flip, 0, rows) != w) { printf("measure mismatch\n"); exit(1); }
        tex.assign((size_t)tw * th / 2, 0);
        twiddle_4bpp(img.data(), tw, th, tw, tex.data(), tw, th);

        Quad q;
        sprite_quad(s, w, q);
        if (!clip_quad(q, 0, 0, W, H)) continue;

        const int color = 0x800 + (s.pal << 4);
        const int px0 = (int)ceilf(q.x0 - sample_offset), px1 = (int)ceilf(q.x1 - sample_offset);
        const int py0 = (int)ceilf(q.y0 - sample_offset), py1 = (int)ceilf(q.y1 - sample_offset);
        for (int py = py0; py < py1; py++)
        {
            const float cy = py + sample_offset;
            const float v = q.v0 + (cy - q.y0) * (q.v1 - q.v0) / (q.y1 - q.y0);
            int tv = (int)floorf(v); if (tv < 0) tv = 0; if (tv >= th) tv = th - 1;
            for (int px = px0; px < px1; px++)
            {
                const float cx = px + sample_offset;
                const float u = q.u0 + (cx - q.x0) * (q.u1 - q.u0) / (q.x1 - q.x0);
                int tu = (int)floorf(u); if (tu < 0) tu = 0; if (tu >= tw) tu = tw - 1;
                const uint32_t idx = twiddle_index(tu, tv, tw, th);
                const int pix = (tex[idx >> 1] >> ((idx & 1) * 4)) & 0xf;
                if (pix == 0 || pix == 15) continue;
                uint16_t& d = pixels_out[py * W + px];
                if (s.shadow && pix == 0xa) { d &= 0xfff; d += PAL_ENTRIES; }
                else d = pix | color;
            }
        }
    }
}

// KOS pvr_txr_load_ex 4bpp twiddler (copied) to cross-check our twiddle
#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)| \
                     ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )
static void kos_twiddle4(const uint8_t* pixels, uint16_t* vtex, uint32_t w, uint32_t h)
{
    uint32_t min = w < h ? w : h, mask = min - 1;
    for (uint32_t y = 0; y < h; y += 2)
        for (uint32_t x = 0; x < w; x += 2)
            vtex[TWIDOUT((x & mask) / 2, (y & mask) / 2) + (x / min + y / min)*min * min / 4] =
                (pixels[(x + y * w) >> 1] & 15) | ((pixels[(x + (y + 1) * w) >> 1] & 15) << 4) |
                ((pixels[(x + y * w) >> 1] >> 4) << 8) | ((pixels[(x + (y + 1) * w) >> 1] >> 4) << 12);
}

static bool test_twiddle(std::mt19937& rng)
{
    const int sizes[] = {8, 16, 32, 64, 128, 256};
    for (int wi : sizes) for (int hi : sizes)
    {
        std::vector<uint8_t> img(wi * hi), packed(wi * hi / 2), ours(wi * hi / 2);
        std::vector<uint16_t> kos(wi * hi / 4);
        for (auto& p : img) p = rng() & 15;
        // KOS expects linear 4bpp packed, low nibble = even x
        for (int i = 0; i < wi * hi; i += 2) packed[i / 2] = img[i] | (img[i + 1] << 4);
        kos_twiddle4(packed.data(), kos.data(), wi, hi);
        twiddle_4bpp(img.data(), wi, hi, wi, ours.data(), wi, hi);
        if (memcmp(kos.data(), ours.data(), ours.size()) != 0)
        {
            printf("twiddle mismatch at %dx%d\n", wi, hi);
            return false;
        }
    }
    return true;
}

// Fill the sprite ROM with images: rows of 'words' words, last word carries
// the end marker. Leaves plenty of random junk too.
struct Img { uint8_t bank; uint16_t addr; int words; int rows; };

static std::vector<Img> build_rom(std::mt19937& rng)
{
    for (uint32_t i = 0; i < SPRITES_LENGTH; i++) sprites[i] = rng();
    std::vector<Img> imgs;
    for (int bank = 0; bank < 4; bank++)
    {
        uint32_t a = 0x100;
        while (a < 0xF000)
        {
            Img im;
            im.bank = bank; im.addr = a;
            im.words = 1 + rng() % 12;
            im.rows = 4 + rng() % 120;
            uint32_t* d = sprites + 0x10000 * bank;
            for (int r = 0; r < im.rows; r++)
                for (int w = 0; w < im.words; w++)
                {
                    uint32_t v = 0;
                    for (int k = 0; k < 8; k++)
                    {
                        uint32_t p = rng() % 16;
                        if (p == 15) p = rng() % 15;            // avoid stray markers
                        if (rng() % 4 == 0) p = 0;               // transparency
                        if (rng() % 8 == 0) p = 0xa;             // shadow candidates
                        v |= p << (28 - 4 * k);
                    }
                    if (w == im.words - 1) v |= 0xff;             // end marker (non-flipped)
                    if (w == 0) v |= 0xff000000;                   // end marker (flipped, reading backwards)
                    else v &= ~0x0f000000u | (rng() % 2 ? 0x0e000000u : 0);
                    if (w != im.words - 1 && (v & 0xf0) == 0xf0) v &= ~0x10u;
                    if (w != 0 && (v & 0x0f000000) == 0x0f000000) v &= ~0x01000000u;
                    d[(uint16_t)(a + r * im.words + w)] = v;
                }
            imgs.push_back(im);
            a += im.words * im.rows + 16;
        }
    }
    return imgs;
}

static void build_list(std::mt19937& rng, const std::vector<Img>& imgs, int count)
{
    memset(ramBuff, 0, sizeof(ramBuff));
    int e = 0;
    for (; e < count && e < 127; e++)
    {
        uint16_t* d = ramBuff + e * 8;
        const Img& im = imgs[rng() % imgs.size()];
        const bool flip = rng() % 2;
        const int hzoom = (rng() % 3 == 0) ? 0x200 : (0x30 + rng() % 0x7d0);
        const int vzoom = (rng() % 3 == 0) ? 0x200 : (0x30 + rng() % 0x7d0);
        // screen rows such that source rows stay inside the image
        int maxh = (int)(((int64_t)im.rows * 512) / (vzoom < 0x40 ? 0x40 : vzoom));
        if (maxh < 1) maxh = 1;
        if (maxh > 256) maxh = 256;
        const int height = 1 + rng() % maxh;
        const int top = (int)(rng() % 300) - 40;           // -40 .. 259
        const int xpos = rng() % 0x300;                    // raw register value
        uint16_t addr = im.addr;
        if (flip) addr = im.addr + im.words - 1;           // start from the right end
        const int pitch = im.words;

        d[0] = ((im.bank & 7) << 9) | ((top + 0x100) & 0x1ff);
        if (rng() % 3 == 0) d[0] |= ((rng() % 2) << 12) * 0;  // (hide bits left clear)
        d[1] = addr;
        d[2] = (pitch & 0x7f) << 9;                         // pitch bits 7-14 of word 2 >>1 >>8
        d[3] = (3 << 12) | ((rng() % 3 == 0) << 14) | (vzoom & 0x7ff);
        d[4] = ((rng() % 2) << 15) | ((!flip) << 14) | ((rng() % 2) << 13) | (hzoom & 0x7ff);
        d[5] = ((height - 1) << 8) | (rng() % 128);
        d[6] = xpos;
    }
    ramBuff[e * 8] = 0x8000;
}

int main(int argc, char** argv)
{
    const int iterations = argc > 1 ? atoi(argv[1]) : 300;
    const float sample_offset = argc > 2 ? (float)atof(argv[2]) : 0.5f;
    std::mt19937 rng(12345);

    if (!test_twiddle(rng)) return 1;
    printf("twiddle: matches KOS pvr_txr_load_ex\n");

    std::vector<Img> imgs = build_rom(rng);
    std::vector<uint16_t> a(W * H), b(W * H);
    long long total_px = 0, bad_px = 0, drawn_px = 0;
    int bad_frames = 0;

    for (int it = 0; it < iterations; it++)
    {
        build_list(rng, imgs, 1 + rng() % 100);
        for (int i = 0; i < W * H; i++) a[i] = b[i] = (uint16_t)(i * 7 & 0x7ff);
        std::vector<uint16_t> base = a;
        ref_render(a.data(), 8);
        pvr_render(b.data(), 8, sample_offset);
        int bad = 0;
        for (int i = 0; i < W * H; i++)
        {
            if (a[i] != base[i]) drawn_px++;
            if (a[i] != b[i]) bad++;
        }
        total_px += W * H;
        bad_px += bad;
        if (bad) bad_frames++;
        if (bad && bad_frames <= 3)
        {
            for (int i = 0; i < W * H; i++)
                if (a[i] != b[i]) { printf("  frame %d first diff at (%d,%d): ref %04x pvr %04x\n", it, i % W, i / W, a[i], b[i]); break; }
        }
    }
    printf("frames=%d  sprite pixels=%lld  mismatched pixels=%lld (%.4f%% of sprite pixels)  frames with diffs=%d  max rows=%d\n",
           iterations, drawn_px, bad_px, drawn_px ? 100.0 * bad_px / drawn_px : 0.0, bad_frames, g_max_rows_seen);
    return bad_px ? 2 : 0;
}
