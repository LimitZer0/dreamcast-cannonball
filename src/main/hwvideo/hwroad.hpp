#pragma once

#include "stdint.hpp"
#ifdef DREAMCAST_PVR_RENDERER
#include "dreamcast/pvr_road.hpp"
#endif

class HWRoad
{
public:
    HWRoad();
    ~HWRoad();

    void init(const uint8_t*, const bool hires);
    void write16(uint32_t adr, const uint16_t data);
    void write16(uint32_t* adr, const uint16_t data);
    void write32(uint32_t* adr, const uint32_t data);
    uint16_t read_road_control();
    void write_road_control(const uint8_t);
    void (HWRoad::*render_background)(uint16_t*);
    void (HWRoad::*render_foreground)(uint16_t*);

#ifdef DREAMCAST_PVR_RENDERER
    // Dreamcast: draw road foreground lines straight to RGB565 using
    // run-length spans (see dreamcast/pvr_road.cpp). Sets line_mask[y] = 1
    // for each line written.
    void render_foreground_rgb565(const uint16_t* lut, uint16_t* out, uint8_t* line_mask);
    void foreground_coverage(uint8_t* line_mask);
#endif
  
private:
    uint8_t road_control;
    uint16_t color_offset1;
    uint16_t color_offset2;
    uint16_t color_offset3;
    int32_t x_offset;

    static const uint16_t ROAD_RAM_SIZE = 0x1000;
    static const uint16_t rom_size = 0x8000;

    // Decoded road graphics
    uint8_t roads[0x40200];

    // Two halves of RAM
    uint16_t ram[ROAD_RAM_SIZE / 2];
    uint16_t ramBuff[ROAD_RAM_SIZE / 2];

#ifdef DREAMCAST_PVR_RENDERER
    pvrroad::RunTable road_runs;
#endif

    void decode_road(const uint8_t*);
    void render_background_lores(uint16_t*);
    void render_foreground_lores(uint16_t*);
    void render_background_hires(uint16_t*);
    void render_foreground_hires(uint16_t*);
};

extern HWRoad hwroad;