#pragma once

#include "stdint.hpp"

class video;

class hwsprites
{
public:
    hwsprites();
    ~hwsprites();
    void init(const uint8_t*);
    void reset();
    void set_x_clip(bool);
    void swap();
    uint8_t read(const uint16_t adr);
    void write(const uint16_t adr, const uint16_t data);
    void render(const uint8_t);

#if defined(DREAMCAST_PVR_RENDERER) || defined(SCORE_SIM)
    // Read-only access for the Dreamcast PVR renderer (dreamcast/pvr_render.cpp)
    const uint16_t* pvr_list() const   { return ramBuff; }
    const uint16_t* sim_ram() const    { return ram; }      // host sprite dumps
    const uint32_t* pvr_data() const   { return sprites; }
    int pvr_list_words() const         { return SPRITE_RAM_SIZE; }
    int pvr_num_banks() const          { return SPRITES_LENGTH / 0x10000; }
    uint16_t pvr_clip_x1() const       { return x1; }
    uint16_t pvr_clip_x2() const       { return x2; }
#endif

private:
    // Clip values.
    uint16_t x1, x2;

    // 128 sprites, 16 bytes each (0x400)
    static const uint16_t SPRITE_RAM_SIZE = 128 * 8;
    static const uint32_t SPRITES_LENGTH = 0x100000 >> 2;
    static const uint16_t COLOR_BASE = 0x800;

    uint32_t sprites[SPRITES_LENGTH]; // Converted sprites
    
    // Two halves of RAM
    uint16_t ram[SPRITE_RAM_SIZE];
    uint16_t ramBuff[SPRITE_RAM_SIZE];
};

