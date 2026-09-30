#pragma once

#include "stdint.hpp"

class RomLoader;

class hwtiles
{
public:
    enum
    {
        LEFT,
        RIGHT,
        CENTRE,
    };

    uint8_t text_ram[0x1000]; // Text RAM
    // Per text row pixel shift (Dreamcast renderer), so a line with an odd
    // number of characters can be centred on the screen. Reset with the text.
    int8_t  text_row_xoff[32] = {0};
    uint8_t tile_ram[0x10000]; // Tile RAM

    hwtiles(void);
    ~hwtiles(void);

    void init(uint8_t* src_tiles, const bool hires);
    void patch_tiles(RomLoader* patch);
    void restore_tiles();
    // Menu text: '(' and ')' drawn into tiles 0x28/0x29 (HUD graphics the
    // menu never shows) while on, the original tiles back while off
    void set_menu_glyphs(bool on);
    void set_x_clamp(const uint16_t);
    void update_tile_values();
    void render_tile_layer(uint16_t*, uint8_t, uint8_t);
    void render_text_layer(uint16_t*, uint8_t);
    void render_all_tiles(uint16_t*);

#ifdef DREAMCAST_PVR_RENDERER
    // Read-only access for the Dreamcast PVR renderer (dreamcast/pvr_render.cpp)
    const uint32_t* pvr_tiles() const  { return tiles; }
    uint8_t pvr_text_bank() const      { return tile_banks[0]; }
    uint16_t pvr_width() const         { return s16_width_noscale; }
    // Incremented whenever the converted tile graphics change
    uint32_t tiles_version;
    // Lines (one byte each, S16_HEIGHT) that the road will cover completely;
    // tile rows entirely inside them are not drawn. NULL = draw everything.
    const uint8_t* skip_lines = nullptr;
#endif

private:
    int16_t x_clamp;
    
    // S16 Width, ignoring widescreen related scaling.
    uint16_t s16_width_noscale;

    static const int TILES_LENGTH = 0x10000;
    uint32_t tiles[TILES_LENGTH];        // Converted tiles
    uint32_t tiles_backup[TILES_LENGTH]; // Converted tiles (backup without patch)

    uint16_t page[4];
    uint16_t scroll_x[4];
    uint16_t scroll_y[4];

    uint8_t tile_banks[2];

    static const uint16_t NUM_TILES = 0x2000; // Length of graphic rom / 24
    static const uint16_t TILEMAP_COLOUR_OFFSET = 0x1c00;
    
    void (hwtiles::*render8x8_tile_mask)(
        uint16_t *buf,
        uint16_t nTileNumber, 
        uint16_t StartX, 
        uint16_t StartY, 
        uint16_t nTilePalette, 
        uint16_t nColourDepth, 
        uint16_t nMaskColour, 
        uint16_t nPaletteOffset); 
        
    void (hwtiles::*render8x8_tile_mask_clip)(
        uint16_t *buf,
        uint16_t nTileNumber, 
        int16_t StartX, 
        int16_t StartY, 
        uint16_t nTilePalette, 
        uint16_t nColourDepth, 
        uint16_t nMaskColour, 
        uint16_t nPaletteOffset); 
        
    void render8x8_tile_mask_lores(
        uint16_t *buf,
        uint16_t nTileNumber, 
        uint16_t StartX, 
        uint16_t StartY, 
        uint16_t nTilePalette, 
        uint16_t nColourDepth, 
        uint16_t nMaskColour, 
        uint16_t nPaletteOffset); 

    void render8x8_tile_mask_clip_lores(
        uint16_t *buf,
        uint16_t nTileNumber, 
        int16_t StartX, 
        int16_t StartY, 
        uint16_t nTilePalette, 
        uint16_t nColourDepth, 
        uint16_t nMaskColour, 
        uint16_t nPaletteOffset);
        
    void render8x8_tile_mask_hires(
        uint16_t *buf,
        uint16_t nTileNumber, 
        uint16_t StartX, 
        uint16_t StartY, 
        uint16_t nTilePalette, 
        uint16_t nColourDepth, 
        uint16_t nMaskColour, 
        uint16_t nPaletteOffset); 
        
    void render8x8_tile_mask_clip_hires(
        uint16_t *buf,
        uint16_t nTileNumber, 
        int16_t StartX, 
        int16_t StartY, 
        uint16_t nTilePalette, 
        uint16_t nColourDepth, 
        uint16_t nMaskColour, 
        uint16_t nPaletteOffset);
        
    inline void set_pixel_x4(uint16_t *buf, uint32_t data);
};