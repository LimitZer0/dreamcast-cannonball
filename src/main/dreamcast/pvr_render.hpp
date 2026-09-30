/***************************************************************************
    Dreamcast native PowerVR renderer.

    Replaces the SDL2 surface renderer on Dreamcast. Instead of rasterising
    the whole OutRun display on the SH-4, the frame is split into layers:

      1. Background  (road + tilemaps)  - still rendered by the CPU into the
                                           indexed pixel buffer, converted to
                                           RGB565 and drawn as one textured quad.
      2. Sprites                         - drawn by the PVR as one 4bpp paletted
                                           textured quad per sprite, using a VRAM
                                           cache of decoded sprite images.
      3. Text layer  (HUD)               - drawn by the PVR as 8x8 textured quads
                                           from a 4bpp tile atlas.

    Sprite shadows are drawn as translucent black (hardware shadow intensity),
    which darkens the layers beneath exactly as the original resistor network.

    Copyright Chris White (original renderer), Dreamcast PVR path 2026.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include "sdl2/renderbase.hpp"

class hwsprites;
class hwtiles;

class Render : public RenderBase
{
public:
    Render();
    ~Render();
    bool init(int src_width, int src_height,
              int scale,
              int video_mode,
              int scanlines);
    void disable();
    bool supports_window() { return false; }
    bool supports_vsync()  { return true; }
    bool start_frame();
    bool finalize_frame();
    void draw_frame(uint16_t* pixels);
    void convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1);

    // Called by Video so the renderer can read sprite and text layer state
    void set_sources(hwsprites* sprites, hwtiles* tiles);

    // Background hand-off: road lines are written straight into the RGB565
    // buffer by HWRoad; lines flagged in line_mask are skipped by draw_frame().
    void begin_background();
    uint16_t* bg_buffer();
    const uint16_t* lut();
    uint8_t* line_mask();

    // False when the video layers are disabled (black screen)
    void set_layers_enabled(bool on) { layers_enabled = on; }

private:
    hwsprites* spr;
    hwtiles*   til;
    bool layers_enabled;
    bool initialised;
};

// Test hooks (used by dreamcast/pvr_selftest.cpp)
void pvr_render_set_sample_offset(float offset);
// Sprites as cut-outs (punch-through: much less GPU fill) or blended as before
void pvr_render_set_cutout(bool on);
bool pvr_render_cutout();
// Game drawn at 320x224 into a texture, then scaled to the screen
void pvr_render_set_native(bool on);
bool pvr_render_native();
// Renderer time since the last call, summed over frames (microseconds):
// out[0] background convert, [1] sprite prep, [2] scene submit, [3] wait
// for the GPU, [4] frames
void pvr_render_take_timing(uint32_t out[5]);
void pvr_render_set_rtt(void* tex, int scale = 1);
const void* pvr_selftest_lut();
int pvr_selftest_main();
void* pvr_render_game_tex();
void pvr_compare_after_frame();
