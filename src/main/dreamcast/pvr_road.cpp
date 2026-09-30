/***************************************************************************
    Dreamcast renderer: run-length road foreground. See pvr_road.hpp.

    Logic mirrors HWRoad::render_foreground_lores() (itself based on MAME,
    copyright Aaron Giles).
***************************************************************************/

#include "pvr_road.hpp"

namespace pvrroad
{

void build_runs(const uint8_t* roads, int rows, RunTable& t)
{
    t.start.clear();
    t.value.clear();
    t.off.clear();
    t.off.reserve(rows + 1);

    for (int r = 0; r < rows; r++)
    {
        t.off.push_back((uint32_t)t.start.size());
        const uint8_t* row = roads + r * ROAD_W;
        uint8_t cur = row[0];
        t.start.push_back(0);
        t.value.push_back(cur);
        for (int x = 1; x < ROAD_W; x++)
        {
            if (row[x] != cur)
            {
                cur = row[x];
                t.start.push_back((uint16_t)x);
                t.value.push_back(cur);
            }
        }
        // sentinel
        t.start.push_back(ROAD_W);
        t.value.push_back(3);
    }
    t.off.push_back((uint32_t)t.start.size());
}

namespace
{
    // Walks one road's pixels along a scanline as runs of equal value.
    struct Cursor
    {
        const uint16_t* st;    // run starts for this row (with sentinel)
        const uint8_t*  val;
        uint32_t h;            // current 12-bit horizontal position
        int ri;                // current run index (valid when h < ROAD_W)

        inline void init(const RunTable& t, int row, uint32_t h0)
        {
            st  = &t.start[t.off[row]];
            val = &t.value[t.off[row]];
            h   = h0;
            ri  = 0;
            if (h < (uint32_t)ROAD_W)
                while (h >= st[ri + 1]) ri++;
        }

        inline int value() const      { return h < (uint32_t)ROAD_W ? val[ri] : 3; }
        inline uint32_t remain() const { return h < (uint32_t)ROAD_W ? st[ri + 1] - h : 0x1000 - h; }

        inline void advance(uint32_t len)
        {
            h += len;
            if (h >= 0x1000)
            {
                h -= 0x1000;
                ri = 0;
                while (h >= st[ri + 1]) ri++;
            }
            else if (h < (uint32_t)ROAD_W)
            {
                while (h >= st[ri + 1]) ri++;
            }
        }
    };

    inline void fill16(uint16_t* p, uint32_t n, uint16_t c)
    {
        if (n && ((uintptr_t)p & 2))
        {
            *p++ = c;
            n--;
        }
        uint32_t* p32 = (uint32_t*)p;
        const uint32_t c2 = (uint32_t)c | ((uint32_t)c << 16);
        uint32_t pairs = n >> 1;
        while (pairs >= 4)
        {
            p32[0] = c2; p32[1] = c2; p32[2] = c2; p32[3] = c2;
            p32 += 4;
            pairs -= 4;
        }
        while (pairs--) *p32++ = c2;
        if (n & 1) *(uint16_t*)p32 = c;
    }

    const uint8_t priority_map[2][8] =
    {
        { 0x80,0x81,0x81,0x87,0,0,0,0x00 },
        { 0x81,0x81,0x81,0x8f,0,0,0,0x80 }
    };
}

void foreground_coverage(const RoadState& s, uint8_t* line_mask)
{
    const uint16_t* roadram = s.roadram;
    const int32_t control = s.road_control & 3;
    for (int y = 0; y < s.height; y++)
    {
        const uint32_t data0 = roadram[0x000 + y];
        const uint32_t data1 = roadram[0x100 + y];
        bool drawn = true;
        if (((data0 & 0x800) != 0) && ((data1 & 0x800) != 0)) drawn = false;
        if (control == 0 && (data0 & 0x800)) drawn = false;
        if (control == 3 && (data1 & 0x800)) drawn = false;
        line_mask[y] = drawn ? 1 : 0;
    }
}

void render_foreground_rgb565(const RoadState& s, const RunTable& runs,
                              const uint16_t* lut, uint16_t* out, uint8_t* line_mask)
{
    const uint16_t* roadram = s.roadram;
    const int width = s.width;
    const uint16_t s16_x = 0x5f8 + s.s16_x_off;
    const int32_t control = s.road_control & 3;
    const bool direct = (s.road_control & 4) != 0;

    for (int y = 0; y < s.height; y++)
    {
        const uint32_t data0 = roadram[0x000 + y];
        const uint32_t data1 = roadram[0x100 + y];

        // if both roads are low priority, skip
        if (((data0 & 0x800) != 0) && ((data1 & 0x800) != 0))
            continue;
        if (control == 0 && (data0 & 0x800)) continue;
        if (control == 3 && (data1 & 0x800)) continue;

        const int row0 = (data0 & 0x800) ? 512 : (0x000 + ((data0 >> 1) & 0xff));
        const int row1 = (data1 & 0x800) ? 512 : (0x100 + ((data1 >> 1) & 0xff));
        int32_t hpos0  = roadram[0x200 + (direct ? y : (data0 & 0x1ff))] & 0xfff;
        const int32_t color0 = roadram[0x600 + (direct ? y : (data0 & 0x1ff))];
        int32_t hpos1  = roadram[0x400 + (direct ? (0x100 + y) : (data1 & 0x1ff))] & 0xfff;
        const int32_t color1 = roadram[0x600 + (direct ? (0x100 + y) : (data1 & 0x1ff))];

        // The 5 colours for each road, straight to RGB565
        uint16_t ct[32];
        const uint16_t co1 = s.color_offset1, co2 = s.color_offset2;
        uint16_t c;
        c = co1 ^ 0x00 ^ ((color0 >> 0) & 1);                 ct[0x00] = lut[c];
        c = co1 ^ 0x02 ^ ((color0 >> 1) & 1);                 ct[0x01] = lut[c];
        c = co1 ^ 0x04 ^ ((color0 >> 2) & 1);                 ct[0x02] = lut[c];
        ct[0x03] = (data0 & 0x200) ? ct[0x00] : lut[(co2 ^ 0x00 ^ ((color0 >> 8) & 0xf)) & 0xfff];
        c = co1 ^ 0x06 ^ ((color0 >> 3) & 1);                 ct[0x07] = lut[c];

        c = co1 ^ 0x08 ^ ((color1 >> 4) & 1);                 ct[0x10] = lut[c];
        c = co1 ^ 0x0a ^ ((color1 >> 5) & 1);                 ct[0x11] = lut[c];
        c = co1 ^ 0x0c ^ ((color1 >> 6) & 1);                 ct[0x12] = lut[c];
        ct[0x13] = (data1 & 0x200) ? ct[0x10] : lut[(co2 ^ 0x10 ^ ((color1 >> 8) & 0xf)) & 0xfff];
        c = co1 ^ 0x0e ^ ((color1 >> 7) & 1);                 ct[0x17] = lut[c];

        uint16_t* p = out + y * width;
        const uint32_t shift = (uint32_t)(s16_x + s.x_offset);

        if (control == 0 || control == 3)
        {
            Cursor cur;
            const int base = control == 0 ? 0x00 : 0x10;
            if (control == 0) cur.init(runs, row0, (hpos0 - shift) & 0xfff);
            else              cur.init(runs, row1, (hpos1 - shift) & 0xfff);

            int x = 0;
            while (x < width)
            {
                uint32_t len = cur.remain();
                if (len > (uint32_t)(width - x)) len = width - x;
                fill16(p + x, len, ct[base + cur.value()]);
                x += len;
                cur.advance(len);
            }
        }
        else
        {
            const uint8_t* pm = priority_map[control - 1];
            Cursor c0, c1;
            c0.init(runs, row0, (hpos0 - shift) & 0xfff);
            c1.init(runs, row1, (hpos1 - shift) & 0xfff);

            int x = 0;
            while (x < width)
            {
                uint32_t len = c0.remain();
                const uint32_t l1 = c1.remain();
                if (l1 < len) len = l1;
                if (len > (uint32_t)(width - x)) len = width - x;
                const int pix0 = c0.value();
                const int pix1 = c1.value();
                const uint16_t col = ((pm[pix0] >> pix1) & 1) ? ct[0x10 + pix1] : ct[0x00 + pix0];
                fill16(p + x, len, col);
                x += len;
                c0.advance(len);
                c1.advance(len);
            }
        }
        line_mask[y] = 1;
    }
}

} // namespace pvrroad
