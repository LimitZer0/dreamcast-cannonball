/***************************************************************************
    SDL2 Hardware Surface Video Rendering.  
    
    Known Bugs:
    - Software scanlines not implemented because we do hardware post-scaling
      using the SDL_RenderCopy() rects from the original bitmap, so would not
      look good at all because they would be ruined by magnifying.

    Copyright Manuel Alfayate and Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

#include "renderbase.hpp"

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
#ifdef __DREAMCAST__
    // SDL_RENDERER_PRESENTVSYNC is already requested for the Dreamcast PVR
    // renderer (see init()); without this override, RenderBase's default
    // (false) makes main_loop() also run its own redundant SDL_Delay-based
    // frame pacing on every single frame regardless of config.video.vsync,
    // which is the likely trigger for a sustained-runtime crash in the
    // SDL_Delay -> thd_sleep -> genwait_wait call chain (Data address
    // error after ~9-13 minutes of continuous play, hardware-confirmed).
    bool supports_vsync() { return true; }
#endif
    bool start_frame();
    bool finalize_frame();
    void draw_frame(uint16_t* pixels);
    void convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1);

private:
    // SDL2 window
    SDL_Window *window;

    // SDL2 renderer
    SDL_Renderer *renderer;

    // SDL2 texture
    SDL_Texture *texture;

#ifdef __DREAMCAST__
    SDL_Surface *window_surface;
#endif

    // SDL2 blitting rects for hw scaling 
    // ratio correction using SDL_RenderCopy()
    SDL_Rect src_rect;
    SDL_Rect dst_rect;
};
