/***************************************************************************
    Dreamcast PVR renderer: portable sprite helpers.

    Everything in this file is plain C++ with no KallistiOS dependency, so it
    can be unit-tested on a desktop machine against the original software
    sprite rasteriser (see tools/pvr_sprite_test.cpp).

    The OutRun sprite chip draws each sprite by walking the sprite ROM one
    32-bit word (8 x 4-bit pixels) at a time and stretching every source
    pixel horizontally/vertically using 9-bit fixed point zoom accumulators.
    That is mathematically the same as nearest-neighbour sampling of an
    un-zoomed source image, which is exactly what the PowerVR does for free.

    So instead of rasterising pixels on the SH-4 we:
      1. Parse the sprite list into SprCmd records     (extract_sprites)
      2. Decode each distinct source image once into a  (decode_sprite)
         4bpp texture and keep it cached in VRAM.
      3. Draw one textured quad per sprite whose UVs    (sprite_quad)
         reproduce the hardware's zoom arithmetic.
***************************************************************************/

#pragma once

#include <stdint.h>

namespace pvrspr
{

// Pixel values with special meaning in the sprite ROM
const uint8_t PIX_TRANSPARENT = 0x0;
const uint8_t PIX_END         = 0xF;   // also transparent
const uint8_t PIX_SHADOW      = 0xA;   // shadow pixel when shadow flag set

// Hardware limits we impose on decoded images
const int MAX_ROW_WORDS = 128;          // 1024 pixels
const int MAX_ROWS      = 1024;

// One sprite, as parsed from sprite RAM.
struct SprCmd
{
    uint16_t addr;      // word offset within bank of first pixel word
    uint8_t  bank;      // 0-15
    uint8_t  flip;      // 1 = read words backwards, nibbles low->high
    int16_t  pitch;     // words between source rows (signed)
    uint8_t  shadow;    // 1 = pixel 0xA darkens what is underneath
    uint8_t  pal;       // sprite palette 0-127 (colour = 0x800 + pal*16 + pix)
    int16_t  xpos;      // screen x of first pixel drawn
    int16_t  top;       // screen y of first row drawn
    int16_t  height;    // number of screen rows drawn
    int8_t   xdelta;    // +1 = draw left to right, -1 = right to left
    int8_t   ydelta;    // +1 = draw downwards,     -1 = upwards
    uint16_t hzoom;     // 0x200 = 1:1, clamped >= 0x40
    uint16_t vzoom;     // 0x200 = 1:1, clamped >= 0x40
};

// Parse the sprite list (the 'ramBuff' half of sprite RAM) for sprites of the
// given priority mask (1 << pri, the same value hwsprites::render() takes).
// x_off is config.s16_x_off (widescreen offset). Returns number written.
int extract_sprites(const uint16_t* ram, int ram_words, uint8_t priority,
                    int32_t x_off, int32_t numbanks, SprCmd* out, int max_out);

// Number of source rows the sprite actually touches.
inline int rows_needed(const SprCmd& s)
{
    return (int)(((uint32_t)(s.height - 1) * s.vzoom) >> 9) + 1;
}

// Key identifying one source image in the sprite ROM
inline uint32_t image_key(const SprCmd& s)
{
    return ((uint32_t)s.addr) | ((uint32_t)s.bank << 16) |
           ((uint32_t)(s.flip & 1) << 20) | ((uint32_t)((uint16_t)s.pitch & 0x7ff) << 21);
}

// Decode a source image into 'out' as one byte per pixel (values 0-15),
// row-major, 'stride' bytes per row. Pixel k of a row is the k-th pixel the
// hardware would draw, so the quad handles direction/flip in geometry.
// Pixels past a row's end marker are written as 0 (transparent).
// Returns the image width in pixels (multiple of 8, >= 8).
int decode_sprite(const uint32_t* bankdata, uint16_t addr, int16_t pitch,
                  uint8_t flip, int rows, uint8_t* out, int stride);

// Width (pixels) the image would decode to, without writing anything.
int measure_sprite(const uint32_t* bankdata, uint16_t addr, int16_t pitch,
                   uint8_t flip, int rows);

// Same, for source rows [r0, r1) only.
int measure_rows(const uint32_t* bankdata, uint16_t addr, int16_t pitch,
                 uint8_t flip, int r0, int r1);

// Smallest power of two >= v, min 8
inline int pow2_at_least(int v)
{
    int p = 8;
    while (p < v) p <<= 1;
    return p;
}

// PowerVR twiddled (Morton) index for a w x h power-of-two texture.
inline uint32_t twid_bits(uint32_t v)
{
    uint32_t r = 0;
    for (int b = 0; b < 10; b++)
        r |= ((v >> b) & 1) << (2 * b);
    return r;
}

// Lookup table version of twid_bits for 0-1023
extern uint32_t twid_table[1024];
void init_twid_table();

inline uint32_t twiddle_index(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    const uint32_t m = w < h ? w : h;
    const uint32_t mask = m - 1;
    return (twid_table[y & mask] | (twid_table[x & mask] << 1)) + (x / m + y / m) * m * m;
}

// Pack an 8-bit-per-pixel image (values 0-15) into a twiddled 4bpp texture.
// dst must hold tw*th/2 bytes.
void twiddle_4bpp(const uint8_t* src, int src_w, int src_h, int src_stride,
                  uint8_t* dst, int tw, int th);

// Screen-space quad for a sprite, before clipping.
// Coordinates are in game pixels (320x224 space); the texture coordinates are
// in texels (divide by texture size for the PVR). Sampling assumes pixel
// centres at +0.5, which reproduces the hardware's floor(k * zoom / 512)
// source pixel selection exactly.
struct Quad
{
    float x0, y0, x1, y1;   // x0 < x1, y0 < y1
    float u0, v0, u1, v1;   // texel coordinates at the corresponding edges
};

void sprite_quad(const SprCmd& s, int img_w, Quad& q);

// Clip a quad to [cx0,cx1) x [cy0,cy1), adjusting UVs. Returns false if empty.
bool clip_quad(Quad& q, float cx0, float cy0, float cx1, float cy1);

} // namespace pvrspr
