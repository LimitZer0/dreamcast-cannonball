/***************************************************************************
    Dreamcast renderer: run-length road foreground.

    The OutRun road chip only scrolls each scanline horizontally (no
    per-pixel scaling), and the road graphics are long runs of the same
    value (exterior, stripes, road surface). So instead of looking up and
    colouring 320 pixels per line, each road ROM row is pre-split into runs
    and every scanline becomes a handful of RGB565 span fills.

    Output is written straight to the RGB565 background buffer, skipping the
    indexed-colour pass for road lines entirely. Portable (no KOS
    dependency) so it can be tested against the original on a desktop.
***************************************************************************/

#pragma once

#include <stdint.h>
#include <vector>

namespace pvrroad
{

const int ROAD_W    = 512;   // pixels per road ROM row
const int ROAD_ROWS = 513;   // road 0 rows 0-255, road 1 rows 256-511, dummy row 512

struct RunTable
{
    // Row r's runs are start[off[r]] .. start[off[r+1]-1]; run i covers
    // [start[i], start[i+1]) (the row's final run ends at ROAD_W).
    std::vector<uint16_t> start;
    std::vector<uint8_t>  value;
    std::vector<uint32_t> off;
};

// roads: decoded road graphics as produced by HWRoad::decode_road (rows x 512)
void build_runs(const uint8_t* roads, int rows, RunTable& t);

// Everything render_foreground_lores() reads, passed explicitly.
struct RoadState
{
    const uint16_t* roadram;     // HWRoad::ramBuff
    uint8_t  road_control;
    uint16_t color_offset1;
    uint16_t color_offset2;
    int32_t  x_offset;
    int32_t  s16_x_off;          // config.s16_x_off
    int      width;              // config.s16_width
    int      height;             // S16_HEIGHT
};

// Renders all road foreground lines as RGB565 into out (stride = width).
// lut: S16 palette index -> RGB565. line_mask[y] is set to 1 for every line
// written (and left untouched otherwise).
void render_foreground_rgb565(const RoadState& st, const RunTable& runs,
                              const uint16_t* lut, uint16_t* out, uint8_t* line_mask);

// Mark the lines render_foreground_rgb565() will cover completely (the same
// test it uses), without drawing. Lets the tile layers skip hidden lines.
void foreground_coverage(const RoadState& st, uint8_t* line_mask);

} // namespace pvrroad
