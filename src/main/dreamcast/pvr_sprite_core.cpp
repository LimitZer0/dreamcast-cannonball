/***************************************************************************
    Dreamcast PVR renderer: portable sprite helpers.
    See pvr_sprite_core.hpp for an overview.
***************************************************************************/

#include <string.h>
#include "pvr_sprite_core.hpp"

namespace pvrspr
{

uint32_t twid_table[1024];

void init_twid_table()
{
    static bool done = false;
    if (done) return;
    for (uint32_t i = 0; i < 1024; i++)
        twid_table[i] = twid_bits(i);
    done = true;
}

int extract_sprites(const uint16_t* ram, int ram_words, uint8_t priority,
                    int32_t x_off, int32_t numbanks, SprCmd* out, int max_out)
{
    int n = 0;

    // Mirrors the parameter decoding in hwsprites::render() exactly.
    for (int data = 0; data < ram_words && n < max_out; data += 8)
    {
        if ((ram[data + 0] & 0x8000) != 0) break;

        const uint32_t sprpri = 1 << ((ram[data + 3] >> 12) & 3);
        if (sprpri != priority) continue;

        const int16_t hide   = (ram[data + 0] & 0x5000);
        const int32_t height = (ram[data + 5] >> 8) + 1;
        if (hide != 0 || height == 0) continue;

        int16_t bank   = (ram[data + 0] >> 9) & 7;
        int32_t top    = (ram[data + 0] & 0x1ff) - 0x100;
        uint32_t addr  = ram[data + 1];
        int32_t pitch  = ((ram[data + 2] >> 1) | ((ram[data + 4] & 0x1000) << 3)) >> 8;
        int32_t xpos   = ram[data + 6];
        uint8_t shadow = (ram[data + 3] >> 14) & 1;
        int32_t vzoom  = ram[data + 3] & 0x7ff;
        int32_t ydelta = ((ram[data + 4] & 0x8000) != 0) ? 1 : -1;
        int32_t flip   = (~ram[data + 4] >> 14) & 1;
        int32_t xdelta = ((ram[data + 4] & 0x2000) != 0) ? 1 : -1;
        int32_t hzoom  = ram[data + 4] & 0x7ff;
        int32_t pal    = ram[data + 5] & 0x7f;

        if (xpos < 0x80 && xdelta < 0)
            xpos += 0x200;
        xpos -= 0xbe;

        if (numbanks)
            bank %= numbanks;

        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;

        xpos += x_off;

        SprCmd& s = out[n++];
        s.addr   = (uint16_t)addr;
        s.bank   = (uint8_t)bank;
        s.flip   = (uint8_t)flip;
        s.pitch  = (int16_t)pitch;
        s.shadow = shadow;
        s.pal    = (uint8_t)pal;
        s.xpos   = xpos;
        s.top    = top;
        s.height = height;
        s.xdelta = (int8_t)xdelta;
        s.ydelta = (int8_t)ydelta;
        s.hzoom  = (uint16_t)hzoom;
        s.vzoom  = (uint16_t)vzoom;
    }
    return n;
}

// Number of words in the row starting at 'a' (including the terminating word)
static inline int row_words(const uint32_t* bankdata, uint16_t a, uint8_t flip)
{
    int w = 0;
    if (!flip)
    {
        while (w < MAX_ROW_WORDS)
        {
            const uint32_t pixels = bankdata[(uint16_t)(a + w)];
            w++;
            if ((pixels & 0x000000f0) == 0x000000f0) break;
        }
    }
    else
    {
        while (w < MAX_ROW_WORDS)
        {
            const uint32_t pixels = bankdata[(uint16_t)(a - w)];
            w++;
            if ((pixels & 0x0f000000) == 0x0f000000) break;
        }
    }
    return w;
}

int measure_sprite(const uint32_t* bankdata, uint16_t addr, int16_t pitch,
                   uint8_t flip, int rows)
{
    return measure_rows(bankdata, addr, pitch, flip, 0, rows);
}

int measure_rows(const uint32_t* bankdata, uint16_t addr, int16_t pitch,
                 uint8_t flip, int r0, int r1)
{
    int maxw = 1;
    for (int r = r0; r < r1; r++)
    {
        const uint16_t a = (uint16_t)(addr + pitch * r);
        const int w = row_words(bankdata, a, flip);
        if (w > maxw) maxw = w;
    }
    return maxw * 8;
}

int decode_sprite(const uint32_t* bankdata, uint16_t addr, int16_t pitch,
                  uint8_t flip, int rows, uint8_t* out, int stride)
{
    int maxw = 1;
    for (int r = 0; r < rows; r++)
    {
        const uint16_t a = (uint16_t)(addr + pitch * r);
        uint8_t* dst = out + r * stride;
        int w = 0;

        if (!flip)
        {
            while (w < MAX_ROW_WORDS && (w + 1) * 8 <= stride)
            {
                const uint32_t p = bankdata[(uint16_t)(a + w)];
                dst[0] = (p >> 28) & 0xf; dst[1] = (p >> 24) & 0xf;
                dst[2] = (p >> 20) & 0xf; dst[3] = (p >> 16) & 0xf;
                dst[4] = (p >> 12) & 0xf; dst[5] = (p >>  8) & 0xf;
                dst[6] = (p >>  4) & 0xf; dst[7] = (p >>  0) & 0xf;
                dst += 8;
                w++;
                if ((p & 0x000000f0) == 0x000000f0) break;
            }
        }
        else
        {
            while (w < MAX_ROW_WORDS && (w + 1) * 8 <= stride)
            {
                const uint32_t p = bankdata[(uint16_t)(a - w)];
                dst[0] = (p >>  0) & 0xf; dst[1] = (p >>  4) & 0xf;
                dst[2] = (p >>  8) & 0xf; dst[3] = (p >> 12) & 0xf;
                dst[4] = (p >> 16) & 0xf; dst[5] = (p >> 20) & 0xf;
                dst[6] = (p >> 24) & 0xf; dst[7] = (p >> 28) & 0xf;
                dst += 8;
                w++;
                if ((p & 0x0f000000) == 0x0f000000) break;
            }
        }

        // Pad the rest of the row with its last pixel. The sprite chip never
        // draws past it, but scaled output (2x) samples up to half a pixel
        // beyond the image edge; repeating the edge keeps that sample equal
        // to the arcade's last pixel instead of transparent.
        const int used = w * 8;
        if (used < stride)
            memset(out + r * stride + used, used ? out[r * stride + used - 1] : 0, stride - used);
        if (w > maxw) maxw = w;
    }
    return maxw * 8;
}

void twiddle_4bpp(const uint8_t* src, int src_w, int src_h, int src_stride,
                  uint8_t* dst, int tw, int th)
{
    // In the PowerVR's twiddled order the lowest index bit is y, so pixels
    // (x, y) and (x, y + 1) (y even) share one byte: low nibble = y, high
    // nibble = y + 1. Walk the image two rows at a time and write each byte
    // once, with the index split into per-column and per-row parts from
    // tables (no divisions, no read-modify-write).
    init_twid_table();

    const uint32_t m = tw < th ? tw : th;
    const uint32_t mask = m - 1;
    int lm = 0;
    while ((1u << lm) < m) lm++;
    const uint32_t mm = m * m;

    // Byte offset contributed by each column
    static uint32_t col_off[1024];
    const int w = src_w < tw ? src_w : tw;
    for (int x = 0; x < w; x++)
        col_off[x] = ((twid_table[x & mask] << 1) + (uint32_t)(x >> lm) * mm) >> 1;

    if (w < tw || src_h < th)
        memset(dst, 0, (size_t)tw * th / 2);

    const int h = src_h < th ? src_h : th;
    for (int y = 0; y < h; y += 2)
    {
        const uint8_t* r0 = src + (size_t)y * src_stride;
        const uint8_t* r1 = (y + 1 < h) ? r0 + src_stride : NULL;
        uint8_t* d = dst + ((twid_table[y & mask] + (uint32_t)(y >> lm) * mm) >> 1);
        if (r1)
            for (int x = 0; x < w; x++)
                d[col_off[x]] = (uint8_t)(r0[x] | (r1[x] << 4));
        else
            for (int x = 0; x < w; x++)
                d[col_off[x]] = r0[x];
    }
}

// Where in a texel the screen pixel centres sample.
//
// Full size (zoom 0x200): the texel's centre. Any rounding in the PowerVR's
// texture coordinate interpolation then still lands in the right texel.
// (Sampling at the texel's edge plus 1/1024 was exact in Flycast, but the
// real hardware landed in the previous texel: the first row/column was
// drawn twice and the last one, e.g. the bottom line of the title logo,
// went missing.)
//
// Shrunk: the hardware picks texel floor(k * zoom / 512), so samples must sit
// just past the texel edge; zoom steps are 1/512 texel, so any bias above
// that can occasionally pick the next texel. 1/256 texel (4x the old bias)
// changes 0.4% of shrunk sprite pixels to a neighbouring texel
// (tools/pvr_sprite_test.cpp; 1/64 would be 2.2%).
static inline float sample_bias(uint16_t zoom)
{
    return zoom == 0x200 ? 0.5f : 1.0f / 256.0f;
}

void sprite_quad(const SprCmd& s, int img_w, Quad& q)
{
    const float sh = s.hzoom / 512.0f;   // texels per screen pixel
    const float sv = s.vzoom / 512.0f;
    const float bu = sample_bias(s.hzoom);
    const float bv = sample_bias(s.vzoom);

    // Screen pixels needed to show img_w texels: smallest n with n*hzoom >= img_w*512
    const int n = (int)(((uint32_t)img_w * 512 + s.hzoom - 1) / s.hzoom);

    if (s.xdelta > 0)
    {
        q.x0 = (float)s.xpos;
        q.x1 = (float)(s.xpos + n);
        q.u0 = -0.5f * sh + bu;
        q.u1 = (n - 0.5f) * sh + bu;
    }
    else
    {
        q.x0 = (float)(s.xpos - n + 1);
        q.x1 = (float)(s.xpos + 1);
        q.u0 = (n - 0.5f) * sh + bu;
        q.u1 = -0.5f * sh + bu;
    }

    if (s.ydelta > 0)
    {
        q.y0 = (float)s.top;
        q.y1 = (float)(s.top + s.height);
        q.v0 = -0.5f * sv + bv;
        q.v1 = (s.height - 0.5f) * sv + bv;
    }
    else
    {
        q.y0 = (float)(s.top - s.height + 1);
        q.y1 = (float)(s.top + 1);
        q.v0 = (s.height - 0.5f) * sv + bv;
        q.v1 = -0.5f * sv + bv;
    }
}

bool clip_quad(Quad& q, float cx0, float cy0, float cx1, float cy1)
{
    if (q.x1 <= cx0 || q.x0 >= cx1 || q.y1 <= cy0 || q.y0 >= cy1)
        return false;

    const float du = (q.u1 - q.u0) / (q.x1 - q.x0);
    const float dv = (q.v1 - q.v0) / (q.y1 - q.y0);

    if (q.x0 < cx0) { q.u0 += (cx0 - q.x0) * du; q.x0 = cx0; }
    if (q.x1 > cx1) { q.u1 -= (q.x1 - cx1) * du; q.x1 = cx1; }
    if (q.y0 < cy0) { q.v0 += (cy0 - q.y0) * dv; q.y0 = cy0; }
    if (q.y1 > cy1) { q.v1 -= (q.y1 - cy1) * dv; q.y1 = cy1; }
    return true;
}

} // namespace pvrspr
