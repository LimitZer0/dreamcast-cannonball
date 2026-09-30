// Desktop test: run-length RGB565 road renderer vs the original per-pixel
// HWRoad::render_foreground_lores() followed by palette conversion.
//
// Build: g++ -O2 -I../src/main/dreamcast pvr_road_test.cpp ../src/main/dreamcast/pvr_road.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <chrono>
#include "pvr_road.hpp"

using namespace pvrroad;

static const int W = 320, H = 224;
static uint8_t roads[0x40200];
static uint16_t ramBuff[0x800];

static void decode_like_hwroad(std::mt19937& rng, bool noisy)
{
    for (int y = 0; y < 512; y++)
    {
        // A road-like row: exterior | outer stripe | road | centre | road | outer stripe | exterior
        int c = 256, half = 20 + rng() % 230;
        for (int x = 0; x < 512; x++)
        {
            int d = abs(x - c);
            int v = 3;
            if (d < half) v = 0;
            if (d >= half - 6 && d < half) v = 2;
            if (d >= half - 12 && d < half - 8) v = 1;
            if (d < 3) v = 1;
            if (noisy) v = rng() % 4;
            roads[y * 512 + x] = v;
            if (x >= 256 - 8 && x < 256 && roads[y * 512 + x] == 3)
                roads[y * 512 + x] |= 4;
        }
    }
    for (int i = 0; i < 512; i++) roads[512 * 512 + i] = 3;
}

// Original (from hwroad.cpp)
static void ref_fg(uint16_t* pixels, uint8_t road_control, uint16_t color_offset1, uint16_t color_offset2,
                   int32_t x_offset, uint8_t* mask)
{
    int x, y;
    uint16_t* roadram = ramBuff;
    for (y = 0; y < H; y++)
    {
        uint16_t color_table[32];
        static const uint8_t priority_map[2][8] =
        {
            { 0x80,0x81,0x81,0x87,0,0,0,0x00 },
            { 0x81,0x81,0x81,0x8f,0,0,0,0x80 }
        };
        const uint32_t data0 = roadram[0x000 + y];
        const uint32_t data1 = roadram[0x100 + y];
        if (((data0 & 0x800) != 0) && ((data1 & 0x800) != 0)) continue;
        uint16_t* pPixel = pixels + (y * W);
        int32_t hpos0, hpos1, color0, color1;
        int32_t control = road_control & 3;
        uint8_t *src0, *src1;
        int32_t bgcolor;
        src0   = ((data0 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x000 + ((data0 >> 1) & 0xff)) * 512);
        hpos0  = roadram[0x200 + (((road_control & 4) != 0) ? y : (data0 & 0x1ff))] & 0xfff;
        color0 = roadram[0x600 + (((road_control & 4) != 0) ? y : (data0 & 0x1ff))];
        src1   = ((data1 & 0x800) != 0) ? roads + 256 * 2 * 512 : (roads + (0x100 + ((data1 >> 1) & 0xff)) * 512);
        hpos1  = roadram[0x400 + (((road_control & 4) != 0) ? (0x100 + y) : (data1 & 0x1ff))] & 0xfff;
        color1 = roadram[0x600 + (((road_control & 4) != 0) ? (0x100 + y) : (data1 & 0x1ff))];
        color_table[0x00] = color_offset1 ^ 0x00 ^ ((color0 >> 0) & 1);
        color_table[0x01] = color_offset1 ^ 0x02 ^ ((color0 >> 1) & 1);
        color_table[0x02] = color_offset1 ^ 0x04 ^ ((color0 >> 2) & 1);
        bgcolor = (color0 >> 8) & 0xf;
        color_table[0x03] = ((data0 & 0x200) != 0) ? color_table[0x00] : (color_offset2 ^ 0x00 ^ bgcolor);
        color_table[0x07] = color_offset1 ^ 0x06 ^ ((color0 >> 3) & 1);
        color_table[0x10] = color_offset1 ^ 0x08 ^ ((color1 >> 4) & 1);
        color_table[0x11] = color_offset1 ^ 0x0a ^ ((color1 >> 5) & 1);
        color_table[0x12] = color_offset1 ^ 0x0c ^ ((color1 >> 6) & 1);
        bgcolor = (color1 >> 8) & 0xf;
        color_table[0x13] = ((data1 & 0x200) != 0) ? color_table[0x10] : (color_offset2 ^ 0x10 ^ bgcolor);
        color_table[0x17] = color_offset1 ^ 0x0e ^ ((color1 >> 7) & 1);
        uint16_t s16_x = 0x5f8 + 0;
        switch (control)
        {
            case 0:
                if (data0 & 0x800) continue;
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < W; x++) { int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3; pPixel[x] = color_table[0x00 + pix0]; hpos0 = (hpos0 + 1) & 0xfff; }
                break;
            case 1: case 2:
                hpos0 = (hpos0 - (s16_x + x_offset)) & 0xfff;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < W; x++)
                {
                    int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
                    int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
                    if (((priority_map[control - 1][pix0] >> pix1) & 1) != 0) pPixel[x] = color_table[0x10 + pix1];
                    else pPixel[x] = color_table[0x00 + pix0];
                    hpos0 = (hpos0 + 1) & 0xfff; hpos1 = (hpos1 + 1) & 0xfff;
                }
                break;
            case 3:
                if (data1 & 0x800) continue;
                hpos1 = (hpos1 - (s16_x + x_offset)) & 0xfff;
                for (x = 0; x < W; x++) { int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3; pPixel[x] = color_table[0x10 + pix1]; hpos1 = (hpos1 + 1) & 0xfff; }
                break;
        }
        mask[y] = 1;
    }
}

int main()
{
    std::mt19937 rng(777);
    static uint16_t lut[0x1000];
    for (int i = 0; i < 0x1000; i++) lut[i] = (uint16_t)(rng() & 0xffff);

    long long bad = 0, lines = 0;
    double t_ref = 0, t_new = 0;
    for (int pass = 0; pass < 2; pass++)
    {
        decode_like_hwroad(rng, pass == 1);
        RunTable rt;
        build_runs(roads, ROAD_ROWS, rt);
        printf("%s rom: %zu runs total (%.1f per row)\n", pass ? "noisy" : "road-like", rt.start.size(), rt.start.size() / 513.0);

        for (int it = 0; it < 2000; it++)
        {
            for (int i = 0; i < 0x800; i++) ramBuff[i] = rng() & 0xffff;
            // make low-priority flags less common, like in game
            for (int y = 0; y < 0x200; y++) if (rng() % 3) ramBuff[y] &= ~0x800;
            uint8_t control = rng() % 8;
            int32_t x_offset = (int32_t)(rng() % 64) - 32;
            std::vector<uint16_t> ref(W * H, 0), idx(W * H, 0), out(W * H, 0xDEAD);
            uint8_t m0[H] = {0}, m1[H] = {0};

            auto t0 = std::chrono::high_resolution_clock::now();
            ref_fg(idx.data(), control, 0x400, 0x420, x_offset, m0);
            for (int i = 0; i < W * H; i++) ref[i] = lut[idx[i] & 0xfff];
            auto t1 = std::chrono::high_resolution_clock::now();
            RoadState st = { ramBuff, control, 0x400, 0x420, x_offset, 0, W, H };
            render_foreground_rgb565(st, rt, lut, out.data(), m1);
            auto t2 = std::chrono::high_resolution_clock::now();
            t_ref += std::chrono::duration<double>(t1 - t0).count();
            t_new += std::chrono::duration<double>(t2 - t1).count();

            for (int y = 0; y < H; y++)
            {
                if (m0[y] != m1[y]) { bad++; if (bad < 5) printf("mask diff line %d\n", y); continue; }
                if (!m0[y]) continue;
                lines++;
                for (int x = 0; x < W; x++)
                    if (ref[y * W + x] != out[y * W + x]) { bad++; if (bad < 5) printf("pixel diff (%d,%d) ctrl=%d\n", x, y, control); break; }
            }
        }
    }
    printf("road lines compared=%lld  mismatching lines=%lld  host time ref=%.3fs new=%.3fs\n", lines, bad, t_ref, t_new);
    return bad ? 1 : 0;
}
