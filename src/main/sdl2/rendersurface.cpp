/***************************************************************************
    SDL2 Hardware Surface Video Rendering.  
    
    Known Bugs:
    - Missing Scanlines

    Copyright Manuel Alfayate, Chris White.
    See license.txt for more details.
***************************************************************************/

#include <iostream>
#include <cstring>

#include "rendersurface.hpp"
#include "frontend/config.hpp"

#ifdef __DREAMCAST__
#include <kos/dbglog.h>
#endif

#ifdef __DREAMCAST__
#define DC_RENDER_TRACE(...) dbglog(DBG_INFO, __VA_ARGS__)
#define DC_RENDER_PERF_INTERVAL_MS 5000
#else
#define DC_RENDER_TRACE(...) do {} while (0)
#endif

#ifdef __DREAMCAST__
static int find_pvr_renderer()
{
    SDL_RendererInfo info;
    for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i)
    {
        if (SDL_GetRenderDriverInfo(i, &info) == 0 &&
            info.name && std::strcmp(info.name, "Dreamcast PVR") == 0)
            return i;
    }
    return -1;
}
#endif

Render::Render(void)
{
#ifdef __DREAMCAST__
    window_surface = NULL;
#endif
}

Render::~Render(void)
{
}

bool Render::init(int src_width, int src_height,
                    int scale,
                    int video_mode,
                    int scanlines)
{
    this->src_width  = src_width;
    this->src_height = src_height;
    this->scale      = scale;
    this->video_mode = video_mode;
    this->scanlines  = scanlines;

    // Setup SDL Screen size
    if (!RenderBase::sdl_screen_size())
        return false;

#ifdef __DREAMCAST__
    video_mode = video_settings_t::MODE_STRETCH;
    this->video_mode = video_mode;
#endif

    int flags = SDL_WINDOW_SHOWN;

    // In SDL2, we calculate the output dimensions, but then in draw_frame() we won't do any scaling: SDL2
    // will do that for us, using the rects passed to SDL_RenderCopy().
    // scn_* -> physical screen dimensions OR window dimensions. On FULLSCREEN MODE it has the physical screen
    //		dimensions and in windowed mode it has the window dimensions.
    // src_* -> real, internal, frame dimensions. Will ALWAYS be 320 or 398 x 224. NEVER CHANGES. 
    // corrected_scn_width_* -> output screen size for scaling.
    // In windowed mode it's the size of the window. 
   
    // --------------------------------------------------------------------------------------------
    // Full Screen Mode
    // --------------------------------------------------------------------------------------------
    if (video_mode == video_settings_t::MODE_FULL || video_mode == video_settings_t::MODE_STRETCH)
    {
	    flags |= (SDL_WINDOW_FULLSCREEN); // Set SDL flag

	    // Fullscreen window size: SDL2 ignores w and h in SDL_CreateWindow() if FULLSCREEN flag
	    // is enable, which is fine, so the window will be fullscreen of the physical videomode
	    // size, but then, if we want to preserve ratio, we need dst_width bigger than src_width.	
	    scn_width  = orig_width;
        scn_height = orig_height;

	    src_rect.w = src_width;
	    src_rect.h = src_height;
	    src_rect.x = 0;
	    src_rect.y = 0;
        
        if (video_mode == video_settings_t::MODE_FULL)
        {
            uint32_t w = (scn_width << 16) / src_width;
            uint32_t h = (scn_height << 16) / src_height;
            dst_rect.w = (src_width * std::min(w, h)) >> 16;
            dst_rect.h = (src_height * std::min(w, h)) >> 16;

            screen_xoff = scn_width - dst_rect.w;
            if (screen_xoff)
                screen_xoff = (screen_xoff / 2);
            
            screen_yoff = scn_height - dst_rect.h;
            if (screen_yoff)
                screen_yoff = screen_yoff / 2;

            dst_rect.x = screen_xoff;
            dst_rect.y = screen_yoff;
        }
        else
        {
            dst_rect.x = 0;
            dst_rect.y = 0;
            dst_rect.w = scn_width;
            dst_rect.h = scn_height;
        }


        SDL_ShowCursor(false);
     }
   
    // --------------------------------------------------------------------------------------------
    // Windowed Mode
    // --------------------------------------------------------------------------------------------
    else
    {
        this->video_mode = video_settings_t::MODE_WINDOW;
       
        scn_width  = src_width  * scale;
        scn_height = src_height * scale;

	    src_rect.w = src_width;
	    src_rect.h = src_height;
	    src_rect.x = 0;
	    src_rect.y = 0;
	    dst_rect.w = scn_width;
	    dst_rect.h = scn_height;
        dst_rect.x = 0;
        dst_rect.y = 0;

#ifdef __DREAMCAST__
        SDL_ShowCursor(false);
#else
        SDL_ShowCursor(true);
#endif
    }

#ifndef __DREAMCAST__
    const int bpp = 32;
    const uint32_t rmask = 0;
    const uint32_t gmask = 0;
    const uint32_t bmask = 0;
    const uint32_t amask = 0;
#endif

    // Frees (Deletes) existing surface
    if (surface)
    {
        SDL_FreeSurface(surface);
        surface = NULL;
    }

#ifdef __DREAMCAST__
    surface = SDL_CreateRGBSurfaceWithFormat(0,
                                             src_width,
                                             src_height < 240 ? 240 : src_height,
                                             16,
                                             SDL_PIXELFORMAT_RGB565);
#else
    surface = SDL_CreateRGBSurface(0,
                                   src_width,
                                   src_height,
                                   bpp,
                                   rmask,
                                   gmask,
                                   bmask,
                                   amask);
#endif

    if (!surface)
    {
        std::cerr << "Surface creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, config.video.filtering ? "linear" : "nearest");
#ifdef __DREAMCAST__

    DC_RENDER_TRACE("cannonball: Dreamcast render layout src=%dx%d screen=%dx%d dst=%d,%d %dx%d mode=%d\n",
                    src_width, src_height, scn_width, scn_height,
                    dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h, video_mode);
#endif
    window = SDL_CreateWindow(
        "Cannonball", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, scn_width, scn_height,
        flags);
    if (!window)
    {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        DC_RENDER_TRACE("cannonball: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

#ifdef __DREAMCAST__
    SDL_ShowCursor(false);
    window_surface = NULL;
    const int renderer_driver = find_pvr_renderer();
    if (renderer_driver < 0)
    {
        std::cerr << "Dreamcast PVR renderer not found" << std::endl;
        DC_RENDER_TRACE("cannonball: Dreamcast PVR renderer not found\n");
        SDL_DestroyWindow(window);
        window = NULL;
        return false;
    }
    renderer = SDL_CreateRenderer(window, renderer_driver,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        DC_RENDER_TRACE("cannonball: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        window = NULL;
        return false;
    }
    {
        SDL_RendererInfo renderer_info = {};
        const bool renderer_info_ok = SDL_GetRendererInfo(renderer, &renderer_info) == 0;
        if (renderer_info_ok)
        {
            DC_RENDER_TRACE("cannonball: renderer=%p name=%s flags=0x%lx\n",
                            (void*)renderer,
                            renderer_info.name,
                            (unsigned long)renderer_info.flags);
        }
        else
        {
            DC_RENDER_TRACE("cannonball: renderer=%p SDL_GetRendererInfo failed: %s\n",
                            (void*)renderer,
                            SDL_GetError());
        }
        if (!renderer_info_ok ||
            !renderer_info.name ||
            std::strcmp(renderer_info.name, "Dreamcast PVR") != 0)
        {
            DC_RENDER_TRACE("cannonball: unexpected Dreamcast renderer: %s\n",
                            renderer_info.name ? renderer_info.name : "(null)");
            std::cerr << "Unexpected Dreamcast renderer: "
                      << (renderer_info.name ? renderer_info.name : "(null)") << std::endl;
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            renderer = NULL;
            window = NULL;
            return false;
        }
    }
DC_RENDER_TRACE("cannonball: creating texture %dx%d surface=%dx%d src=%dx%d dst=%dx%d\n",
                surface->w, surface->h,
                surface->w, surface->h,
                src_width, src_height,
                dst_rect.w, dst_rect.h);
                int tw, th, access;
Uint32 fmt;

    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STREAMING,
                                surface->w,
                                surface->h);
    if (!texture)
    {
        std::cerr << "Texture creation failed: " << SDL_GetError() << std::endl;
        DC_RENDER_TRACE("cannonball: SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        renderer = NULL;
        window = NULL;
        return false;
    }
    if (SDL_QueryTexture(texture, &fmt, &access, &tw, &th) == 0)
    {
        DC_RENDER_TRACE("cannonball: created texture=%p actual=%dx%d format=%s access=%d renderer=%p\n",
                        (void*)texture,
                        tw,
                        th,
                        SDL_GetPixelFormatName(fmt),
                        access,
                        (void*)renderer);
    }
    else
    {
        DC_RENDER_TRACE("cannonball: created texture=%p SDL_QueryTexture failed: %s\n",
                        (void*)texture,
                        SDL_GetError());
    }

    DC_RENDER_TRACE("cannonball: Dreamcast texture framebuffer surface=%dx%d pitch=%d format=%s\n",
                    surface->w,
                    surface->h,
                    surface->pitch,
                    SDL_GetPixelFormatName(surface->format->format));
#else
    renderer = SDL_CreateRenderer(window, 1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        window = NULL;
        return false;
    }
#endif

#ifndef __DREAMCAST__
    int texture_width = src_width;
    int texture_height = src_height;

    texture = SDL_CreateTexture(renderer,
                               SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING,
                               texture_width, texture_height);
    if (!texture)
    {
        std::cerr << "Texture creation failed: " << SDL_GetError() << std::endl;
        DC_RENDER_TRACE("cannonball: SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        renderer = NULL;
        window = NULL;
        return false;
    }

    // Convert the SDL pixel surface to 32 bit.
    // This is potentially a larger surface area than the internal pixel array.
    screen_pixels = (uint32_t*)surface->pixels;
#endif
    
    // SDL Pixel Format Information
    Rshift = surface->format->Rshift;
    Gshift = surface->format->Gshift;
    Bshift = surface->format->Bshift;
    Rmask  = surface->format->Rmask;
    Gmask  = surface->format->Gmask;
    Bmask  = surface->format->Bmask;

    return true;
}

void Render::disable()
{
#ifdef __DREAMCAST__
    window_surface = NULL;
#endif
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

bool Render::start_frame()
{
    return true;
}

bool Render::finalize_frame()
{
#ifdef __DREAMCAST__
    static uint32_t perf_last = SDL_GetTicks();
    static int perf_frames = 0;
    static uint32_t perf_update = 0;

    uint32_t perf_start = SDL_GetTicks();
    SDL_UpdateTexture(texture, NULL, surface->pixels, surface->pitch);
    SDL_RenderClear(renderer);
    SDL_Rect dst = { 0, 0, 640, 480 };
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
    perf_update += SDL_GetTicks() - perf_start;
#else
    SDL_UpdateTexture(texture, NULL, screen_pixels, src_width * sizeof (Uint32));

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, &src_rect, &dst_rect);

    // Very basic scanlines
    if (scanlines && scale != 1)
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, ((scanlines - 1) << 8) / 100);
        SDL_Rect r;
        r.x = dst_rect.x;
        r.w = dst_rect.w;
        r.h = scale >> 1;
        r.y = scale >> 1;

        for (; r.y < dst_rect.h; r.y += scale)
            SDL_RenderDrawRect(renderer, &r);
    }

    SDL_RenderPresent(renderer);
#endif
#ifdef __DREAMCAST__
    perf_frames++;
    const uint32_t perf_now = SDL_GetTicks();
    if (perf_now - perf_last >= DC_RENDER_PERF_INTERVAL_MS)
    {
        const uint32_t perf_elapsed = perf_now - perf_last;
        const uint32_t perf_fps = (perf_frames * 1000) / perf_elapsed;
        DC_RENDER_TRACE("cannonball: renderperf fps=%lu update=%lu\n",
                        perf_fps,
                        (unsigned long)(perf_update / perf_frames));
        perf_last = perf_now;
        perf_frames = 0;
        perf_update = 0;
    }
#endif
    return true;
}

void Render::draw_frame(uint16_t* pixels)
{
#ifdef __DREAMCAST__
    static uint32_t perf_last = SDL_GetTicks();
    static int perf_frames = 0;
    static uint32_t perf_draw = 0;
    uint32_t perf_start = SDL_GetTicks();
    uint8_t* dst_row = (uint8_t*)surface->pixels;
    const int dst_pitch = surface->pitch;

    // Lookup ARGB1555 values; SDL_RenderCopy scales this texture to the 640x480 output.
    for (int y = 0; y < src_height; y++)
    {
        uint16_t* dst = (uint16_t*)dst_row;
        for (int x = 0; x < src_width; x++)
            *dst++ = (uint16_t)rgb[*(pixels++)];
        dst_row += dst_pitch;
    }
    if (src_height < surface->h)
    {
        SDL_memset((uint8_t*)surface->pixels + (src_height * dst_pitch),
                   0,
                   (size_t)(surface->h - src_height) * dst_pitch);
    }
    perf_draw += SDL_GetTicks() - perf_start;
    perf_frames++;
    const uint32_t perf_now = SDL_GetTicks();
    if (perf_now - perf_last >= DC_RENDER_PERF_INTERVAL_MS)
    {
        const uint32_t perf_elapsed = perf_now - perf_last;
        const uint32_t perf_fps = (perf_frames * 1000) / perf_elapsed;
        DC_RENDER_TRACE("cannonball: drawperf fps=%lu draw=%lu\n",
                        perf_fps,
                        (unsigned long)(perf_draw / perf_frames));
        perf_last = perf_now;
        perf_frames = 0;
        perf_draw = 0;
    }
#else
    uint32_t* spix = screen_pixels;

    // Lookup real RGB value from rgb array for backbuffer
    for (int i = 0; i < (src_width * src_height); i++)
        *(spix++) = rgb[*(pixels++)];
#endif
}

void Render::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
#ifdef __DREAMCAST__
    adr >>= 1;

    const uint16_t r = (r1 & 0x1F);
    const uint16_t g = ((g1 & 0x1F) << 1) | ((g1 & 0x10) >> 4);
    const uint16_t b = (b1 & 0x1F);

    rgb[adr] = (r << 11) | (g << 5) | b;

    const uint32_t sr = ((r1 * shadow_multi) / 31) >> 3;
    const uint32_t sg = ((g1 * shadow_multi) / 31) >> 2;
    const uint32_t sb = ((b1 * shadow_multi) / 31) >> 3;

    rgb[adr + S16_PALETTE_ENTRIES] =
        ((sr & 0x1F) << 11) |
        ((sg & 0x3F) << 5) |
        (sb & 0x1F);
#else
    RenderBase::convert_palette(adr, r1, g1, b1);
#endif
}
