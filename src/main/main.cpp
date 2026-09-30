/***************************************************************************
    Cannonball Main Entry Point.
    
    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <cstdio>
#include <cstring>
#include <iostream>

// SDL Library
#include <SDL.h>

#ifdef __DREAMCAST__
#include <arch/arch.h>
#include <dc/maple/controller.h>
#include <kos/dbglog.h>
#include <kos/thread.h>
#include <stdint.h>
#endif

// SDL Specific Code
#include "sdl2/timer.hpp"
#include "sdl2/input.hpp"

#include "video.hpp"
#ifdef DREAMCAST_CUSTOM_MUSIC
#include "dreamcast/custom_music.hpp"
#endif
#if defined(DREAMCAST_PVR_SELFTEST) || defined(DREAMCAST_PVR_COMPARE) || defined(DREAMCAST_PVR_RENDERER)
#include "dreamcast/pvr_render.hpp"
#endif
#ifdef __DREAMCAST__
#include "dreamcast/vmu_lcd.hpp"
#include <arch/timer.h>
#endif

#include "romloader.hpp"
#include "trackloader.hpp"
#include "stdint.hpp"
#include "main.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"
#include "frontend/menu.hpp"

#include "engine/oinputs.hpp"
#include "engine/ooutputs.hpp"
#include "engine/omusic.hpp"
#include "engine/oroad.hpp"
#include "engine/ostats.hpp"
#include "engine/osprites.hpp"
#include "engine/otraffic.hpp"
#include "engine/oinitengine.hpp"
#include "engine/ohud.hpp"

// Direct X Haptic Support.
// Fine to include on non-windows builds as dummy functions used.
#include "directx/ffeedback.hpp"

// ------------------------------------------------------------------------------------------------
// Initialize Shared Variables
// ------------------------------------------------------------------------------------------------
using namespace cannonball;

int    cannonball::state       = STATE_BOOT;
double cannonball::frame_ms    = 0;
int    cannonball::frame       = 0;
bool   cannonball::tick_frame  = true;
int    cannonball::fps_counter = 0;
char   cannonball::perf_line[48] = "";

// ------------------------------------------------------------------------------------------------
// Main Variables and Pointers
// ------------------------------------------------------------------------------------------------
Audio cannonball::audio;
Menu* menu;
bool pause_engine;

#ifdef __DREAMCAST__
#define DC_TRACE(...) dbglog(DBG_INFO, __VA_ARGS__)
#define DC_STARTUP_TRACE(...) do {} while (0)
#define DC_INPUT_TRACE(...) do {} while (0)
#define DC_PERF_INTERVAL_MS 2000
#else
#define DC_TRACE(...) do {} while (0)
#define DC_STARTUP_TRACE(...) do {} while (0)
#define DC_INPUT_TRACE(...) do {} while (0)
#endif

// ------------------------------------------------------------------------------------------------

static void quit_func(int code)
{
    audio.stop_audio();
    input.close_joy();
    forcefeedback::close();
    delete menu;
    SDL_Quit();
    exit(code);
}

static void process_events(void)
{
    SDL_Event event;

    // Grab all events from the queue.
    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
            case SDL_KEYDOWN:
                // Handle key presses.
                if (event.key.keysym.sym == SDLK_ESCAPE)
#ifdef __DREAMCAST__
                    arch_exit();
#else
                    state = STATE_QUIT;
#endif
                else
                    input.handle_key_down(&event.key.keysym);
                break;

            case SDL_KEYUP:
                input.handle_key_up(&event.key.keysym);
                break;

            case SDL_JOYAXISMOTION:
                input.handle_joy_axis(&event.jaxis);
                break;

            case SDL_JOYBUTTONDOWN:
                input.handle_joy_down(&event.jbutton);
                break;

            case SDL_JOYBUTTONUP:
                input.handle_joy_up(&event.jbutton);
                break;

            case SDL_CONTROLLERAXISMOTION:
                input.handle_controller_axis(&event.caxis);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                input.handle_controller_down(&event.cbutton);
                break;

            case SDL_CONTROLLERBUTTONUP:
                input.handle_controller_up(&event.cbutton);
                break;

            case SDL_JOYHATMOTION:
                input.handle_joy_hat(&event.jhat);
                break;

            case SDL_JOYDEVICEADDED:
                DC_INPUT_TRACE("cannonball: SDL_JOYDEVICEADDED which=%ld\n", (long)event.jdevice.which);
                input.open_joy();
                break;

            case SDL_JOYDEVICEREMOVED:
                DC_INPUT_TRACE("cannonball: SDL_JOYDEVICEREMOVED which=%ld\n", (long)event.jdevice.which);
                input.close_joy();
                break;

            case SDL_CONTROLLERDEVICEADDED:
                DC_INPUT_TRACE("cannonball: SDL_CONTROLLERDEVICEADDED which=%ld\n", (long)event.cdevice.which);
                input.open_joy();
                break;

            case SDL_CONTROLLERDEVICEREMOVED:
                DC_INPUT_TRACE("cannonball: SDL_CONTROLLERDEVICEREMOVED which=%ld\n", (long)event.cdevice.which);
                input.close_joy();
                break;

            case SDL_QUIT:
                // Handle quit requests (like Ctrl-c).
                state = STATE_QUIT;
                break;
        }
    }
}

#ifdef SCORE_SIM
// Headless score measurement (host only): the AI plays full games as fast as
// the CPU allows, with settings from environment variables, and one RESULT
// line is printed per finished game. See tools/score_sim.sh.
#include <cstdlib>
static int sim_games_left = 1;
static int sim_env(const char* name, int def)
{
    const char* v = getenv(name);
    return v ? atoi(v) : def;
}
static void sim_apply_env()
{
    config.engine.dip_time        = sim_env("CB_TIME", 1);
    const int traffic             = sim_env("CB_TRAFFIC", 1);
    config.engine.disable_traffic = traffic < 0;
    config.engine.dip_traffic     = traffic < 0 ? 1 : traffic;
    config.engine.jap             = sim_env("CB_JAP", 0);
    config.engine.grippy_tyres    = sim_env("CB_GRIP", 0) != 0;
    config.engine.bumper          = sim_env("CB_BUMPER", 0) != 0;
    config.engine.freeze_timer    = false;
    config.engine.randomgen       = 0;
    config.engine.score_scaling   = sim_env("CB_SCALE", 0);
    config.engine.fix_timer       = sim_env("CB_FIXTIMER", config.engine.fix_timer);
    sim_games_left                = sim_env("CB_GAMES", 10);
    srand(sim_env("CB_SEED", 1));
}
static uint32_t sim_bcd(uint32_t b)
{
    uint32_t v = 0, m = 1;
    for (int i = 0; i < 8; i++, b >>= 4, m *= 10) v += (b & 0xF) * m;
    return v;
}
// CB_SPRDUMP=<frame>[,<frame>...]: write the sprite list and palette RAM at
// those frames (spr_<frame>.bin) and the sprite ROM data once (sprdata.bin),
// for offline sprite scaling experiments (tools/sprite_scale)
static void sim_sprite_dump(int frame)
{
    static const char* list = getenv("CB_SPRDUMP");
    if (!list) return;
    bool hit = false;
    for (const char* p = list; *p; )
    {
        if (atoi(p) == frame) hit = true;
        while (*p && *p != ',') p++;
        if (*p) p++;
    }
    if (!hit) return;
    static bool data_done = false;
    if (!data_done)
    {
        FILE* f = fopen("sprdata.bin", "wb");
        fwrite(video.sprite_layer->pvr_data(), 4, video.sprite_layer->pvr_num_banks() * 0x10000, f);
        fclose(f);
        data_done = true;
    }
    char name[64];
    snprintf(name, sizeof(name), "spr_%d.bin", frame);
    FILE* f = fopen(name, "wb");
    fwrite(video.sprite_layer->sim_ram(), 2, video.sprite_layer->pvr_list_words(), f);
    for (uint32_t a = 0; a < 0x2000; a += 2)
    {
        const uint16_t v = video.read_pal16(a);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
    printf("SPRDUMP %d gs=%d split=%d rds=%d route=%d tsplit=%d stage=%d pos=%d\n", frame, (int)outrun.game_state,
           (int)oinitengine.road_remove_split, (int)oinitengine.rd_split_state, (int)oinitengine.route_selected,
           (int)otraffic.traffic_split, (int)ostats.cur_stage, (int)(oroad.road_pos >> 16));
    for (int i = OSprites::SPRITE_TRAFF1; i <= OSprites::SPRITE_TRAFF8; i++)
    {
        const oentry& t = osprites.jump_table[i];
        if (t.control & OSprites::ENABLE)
            printf("  T%d ctl=%02x z=%d x=%d y=%d fn=%d\n", i - OSprites::SPRITE_TRAFF1, t.control, (int)(t.z >> 16), t.x, t.y, t.function_holder);
    }
}

// CB_TRAFFICDUMP=1: print the traffic sprite frame table (every type, angle
// frame, incline and size) for tools/sprite_scale
static void sim_traffic_dump()
{
    static bool done = false;
    if (done || !getenv("CB_TRAFFICDUMP")) return;
    done = true;
    for (int type = 0; type < 0xA0; type += 8)
    {
        const int kind = roms.rom0p->read8(outrun.adr.traffic_props + type + 7);
        const int pal  = roms.rom0p->read8(outrun.adr.traffic_props + type + 4);
        for (int frame = 1; frame <= 3; frame++)
            for (int incline = 0; incline <= 0x10; incline += 0x10)
            {
                const uint32_t addr = roms.rom0p->read32(outrun.adr.traffic_data + (kind << 5) + (frame << 2) + incline);
                for (int k = 0; k < 5; k++)
                {
                    const uint32_t r = addr + k * 10;
                    printf("TRAFFIC type=%d kind=%d pal=%d frame=%d incline=%d size=%d addr=%06x",
                           type / 8, kind, pal, frame, incline, k, addr);
                    for (int b = 0; b < 10; b++) printf(" %02x", roms.rom0p->read8(r + b));
                    printf("\n");
                }
            }
    }
}

// CB_FERRARIDUMP=1: print the Ferrari's frame records (turn x incline, and
// the skid frames) for tools/sprite_scale
static void sim_ferrari_dump()
{
    static bool done = false;
    if (done || !getenv("CB_FERRARIDUMP")) return;
    done = true;
    for (int set = 0; set < 2; set++)
    {
        const uint32_t base = set ? outrun.adr.sprite_skid_frames : outrun.adr.sprite_ferrari_frames;
        const int n = set ? 12 : 9;
        for (int i = 0; i < n; i++)
        {
            const uint32_t e = base + i * 8;
            const uint32_t fa = roms.rom0p->read32(e);
            printf("FERRARI set=%d idx=%d frame=%06x passy=%d xoff=%d rec=", set, i, fa,
                   (int16_t)roms.rom0p->read16(e + 4), (int16_t)roms.rom0p->read16(e + 6));
            for (int b = 0; b < 10; b++) printf(" %02x", roms.rom0p->read8(fa + b));
            printf("\n");
        }
    }
}

// CB_TILEUSE=1: at exit, list the text/tile codes 0x00-0xFF the game never
// put on screen (candidates for extra font glyphs)
static uint8_t sim_tile_used[0x200];
static void sim_tile_use()
{
    static const bool on = getenv("CB_TILEUSE") != NULL;
    if (!on) return;
    const uint8_t* t = video.tile_layer->text_ram;
    for (int i = 0; i < 0x800; i++)
        sim_tile_used[((t[i * 2] << 8) | t[i * 2 + 1]) & 0x1ff] = 1;
    const uint8_t* m = video.tile_layer->tile_ram;
    for (int i = 0; i < 0x8000; i++)
        sim_tile_used[((m[i * 2] << 8) | m[i * 2 + 1]) & 0x1ff] = 1;
}
static void sim_tile_report()
{
    if (!getenv("CB_TILEUSE")) return;
    printf("TILEUNUSED");
    for (int c = 0; c < 0x100; c++) if (!sim_tile_used[c]) printf(" %02x", c);
    printf("\n");
}

static void sim_check(int frame)
{
    sim_tile_use();
    sim_ferrari_dump();
    sim_traffic_dump();
    sim_sprite_dump(frame);
    static int prev = -1;
    static int start_frame = 0;
    const int gs = outrun.game_state;
    if (gs == GS_INGAME && prev == GS_START3)
        start_frame = frame;
    if (gs == GS_INIT_MAP && prev != GS_INIT_MAP)
    {
        printf("RESULT score=%u stage=%d completed=%d route=%02x frames=%d\n",
               sim_bcd(ostats.score), ostats.cur_stage + 1, ostats.game_completed ? 1 : 0,
               ostats.route_info, frame - start_frame);
        fflush(stdout);
        if (--sim_games_left <= 0)
        {
            sim_tile_report();
            state = STATE_QUIT;
        }
    }
    prev = gs;
}
#endif

#ifdef DREAMCAST_DEBUG_MUSIC
extern "C" volatile unsigned int wav_debug_reads, wav_debug_bytes, wav_debug_short;
#endif

#ifdef RENDER_AUDIO
// ------------------------------------------------------------------------------------------------
// Offline audio rendering (host build only, see tools/render_audio.sh)
//
// Runs the game's own sound code (the ported Z80 program driving the YM2151
// and SegaPCM emulators) with no game attached, and writes the result as a
// 16-bit stereo WAV:
//   music:  CB_RENDER=music CB_CMD=0x85 -> plays until the sequencer state
//           repeats, giving an exact loop. A 'smpl' chunk marks the loop.
//   effect: CB_RENDER=sfx   CB_CMD=0x86 -> plays until the effect has ended.
// The sound code runs 125 times a second, so the sample rate is chosen to be
// a multiple of 125 (default 32000 Hz = 256 samples per tick) and loop points
// land on exact samples.
// ------------------------------------------------------------------------------------------------
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include "engine/audio/osound.hpp"
extern OSound osound;

static void wav_put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF); }
static void wav_put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
static void wav_tag(std::vector<uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }

static bool write_wav(const char* fn, const std::vector<int16_t>& smp, int rate, int channels, long loop_start, long loop_end)
{
    std::vector<uint8_t> f;
    const uint32_t data_bytes = (uint32_t)smp.size() * 2;
    const bool looped = loop_start >= 0;
    wav_tag(f, "RIFF"); wav_put32(f, 0); wav_tag(f, "WAVE");
    wav_tag(f, "fmt "); wav_put32(f, 16); wav_put16(f, 1); wav_put16(f, channels);
    wav_put32(f, rate); wav_put32(f, rate * 2 * channels); wav_put16(f, 2 * channels); wav_put16(f, 16);
    wav_tag(f, "data"); wav_put32(f, data_bytes);
    for (int16_t x : smp) wav_put16(f, (uint16_t)x);
    if (looped)
    {
        // Standard sampler chunk with one forward loop (frames, end inclusive)
        wav_tag(f, "smpl"); wav_put32(f, 36 + 24);
        wav_put32(f, 0); wav_put32(f, 0); wav_put32(f, 1000000000u / rate); wav_put32(f, 60);
        wav_put32(f, 0); wav_put32(f, 0); wav_put32(f, 0); wav_put32(f, 1); wav_put32(f, 0);
        wav_put32(f, 0); wav_put32(f, 0); wav_put32(f, (uint32_t)loop_start); wav_put32(f, (uint32_t)loop_end - 1);
        wav_put32(f, 0); wav_put32(f, 0);
    }
    const uint32_t riff = (uint32_t)f.size() - 8;
    for (int i = 0; i < 4; i++) f[4 + i] = (riff >> (8 * i)) & 0xFF;
    FILE* fp = fopen(fn, "wb");
    if (!fp) return false;
    fwrite(f.data(), 1, f.size(), fp);
    fclose(fp);
    return true;
}

static int render_audio_main()
{
    const char* mode = getenv("CB_RENDER");
    const char* out  = getenv("CB_OUT");
    const char* cmds = getenv("CB_CMD");
    if (!mode || !out || !cmds) { fprintf(stderr, "CB_RENDER, CB_CMD and CB_OUT required\n"); return 1; }
    const int cmd  = (int)strtol(cmds, NULL, 0);
    const int rate = getenv("CB_RATE") ? atoi(getenv("CB_RATE")) : 32000;
    const bool music = strcmp(mode, "music") == 0;
    const bool engine = strcmp(mode, "engine") == 0;   // CB_CMD = engine pitch
    if (rate % 125) { fprintf(stderr, "CB_RATE must be a multiple of 125\n"); return 1; }

    config.fps        = 125;        // one sound tick per rendered chunk
    config.sound.rate = rate;
    const int spt = rate / 125;     // stereo frames per tick

    outrun.game_state = GS_MUSIC;   // (music is suppressed in attract mode)
    osoundint.init();
    osoundint.has_booted = true;

    // Let the sound program settle, then start the music / effect
    for (int i = 0; i < 25; i++) { osoundint.tick(); osoundint.pcm->stream_update(); osoundint.ym->stream_update(); }
    if (engine)
    {
        osoundint.engine_data[sound::ENGINE_PITCH_H] = (cmd >> 8) & 0xFF;
        osoundint.engine_data[sound::ENGINE_PITCH_L] = cmd & 0xFF;
        osoundint.engine_data[sound::ENGINE_VOL]     = 0x3F;
    }
    else
        osoundint.queue_sound((uint8_t)cmd);

    std::vector<int16_t> smp;
    std::map<uint32_t, long> seen;
    long loop_start = -1, loop_end = -1, quiet = 0, last_loud = 0;
    const long max_ticks = 125L * 60 * 10;    // 10 minutes

    for (long t = 0; t < max_ticks; t++)
    {
        osoundint.tick();
        osoundint.pcm->stream_update();
        osoundint.ym->stream_update();
        const int16_t* p = osoundint.pcm->get_buffer();
        const int16_t* y = osoundint.ym->get_buffer();
        const int n = osoundint.pcm->buffer_size;
        int peak = 0;
        for (int i = 0; i < n; i++)
        {
            int32_t m = p[i] + y[i];            // same mix as Audio::tick()
            if (m > INT16_MAX) m = INT16_MAX; else if (m < INT16_MIN) m = INT16_MIN;
            smp.push_back((int16_t)m);
            if (abs(m) > peak) peak = abs(m);
        }

        if (engine)
        {
            if (t >= 125 * 3) break;    // 3 seconds
            continue;
        }
        if (music && getenv("CB_LOOPLOG"))
        {
            static uint32_t prev[32] = {0};
            for (int c = 1; c < 15; c++)
                if (osound.loop_forever_count[c] != prev[c])
                {
                    fprintf(stderr, "ch%2d loop#%u at %.3fs\n", c, osound.loop_forever_count[c], t / 125.0);
                    prev[c] = osound.loop_forever_count[c];
                }
        }
        if (music && getenv("CB_CHANLOG") && (t % 1250) == 0)
        {
            char act[32]; int k = 0;
            for (uint16_t ch = channel::YM1; ch < channel::PCM_FX1; ch += 0x20) act[k++] = osound.channel_enabled(ch) ? '1' : '.';
            act[k] = 0;
            fprintf(stderr, "t=%4lds channels %s\n", t / 125, act);
        }
        if (music)
        {
            // The sequencer state first repeats at t (seen before at t1): the
            // music from t on is the loop. The loop's first pass still carries
            // release tails of the notes before it, so render one more pass
            // and use that as the loop: from then on playback is exact.
            static long stop_at = -1;
            if (stop_at < 0)
            {
                const uint32_t h = osound.music_state_hash();
                auto it = seen.find(h);
                if (it != seen.end() && t > 125)
                {
                    loop_start = t * spt;
                    stop_at    = t + (t - it->second);
                    if (getenv("CB_EXTRA")) stop_at += atol(getenv("CB_EXTRA"));
                }
                else
                    seen[h] = t;
            }
            if (stop_at >= 0 && t + 1 >= stop_at)
            {
                loop_end = (t + 1) * spt;
                if (getenv("CB_EXTRA")) loop_end -= atol(getenv("CB_EXTRA")) * spt;
                break;
            }
        }
        else
        {
            if (peak > 64) last_loud = t;
            bool any = false;
            for (uint16_t ch = channel::YM1; ch <= channel::YM8; ch += 0x20) any |= osound.channel_enabled(ch);
            any |= osound.channel_enabled(channel::YM_FX1) || osound.channel_enabled(channel::YM_FX2);
            quiet = any ? 0 : quiet + 1;
            if (quiet > 25 && t - last_loud > 25) break;   // ended and silent for 0.2s
        }
    }

    if (!music && !engine)
        smp.resize((size_t)(last_loud + 13) * spt * 2); // keep 0.1s after the last sound

    if (music && loop_start < 0) { fprintf(stderr, "no loop found\n"); return 2; }

    if (music && loop_end > 0 && !getenv("CB_EXTRA")) smp.resize((size_t)loop_end * 2);

    // A "loop" of pure silence means the tune ends (Last Wave): no loop
    if (music)
    {
        int peak = 0;
        for (size_t i = (size_t)loop_start * 2; i < smp.size(); i++) if (abs(smp[i]) > peak) peak = abs(smp[i]);
        if (peak < 64)
        {
            size_t end = (size_t)loop_start * 2;
            while (end > 2 && abs(smp[end - 1]) < 64 && abs(smp[end - 2]) < 64) end -= 2;
            smp.resize(std::min(smp.size(), end + (size_t)rate / 4 * 2));   // keep 0.25s of tail
            loop_start = loop_end = -1;
        }
    }

    // Effects: store mono if both channels are identical
    int channels = 2;
    if (!music && !engine)
    {
        bool same = true;
        for (size_t i = 0; i + 1 < smp.size() && same; i += 2) same = smp[i] == smp[i + 1];
        if (same)
        {
            for (size_t i = 0; i < smp.size() / 2; i++) smp[i] = smp[i * 2];
            smp.resize(smp.size() / 2);
            channels = 1;
        }
    }

    if (!write_wav(out, smp, rate, channels, loop_start, loop_end)) { fprintf(stderr, "cannot write %s\n", out); return 3; }
    // Loop start (in sample frames) for the disc builder, which re-encodes the WAV
    std::string loopfn = std::string(out) + ".loop";
    remove(loopfn.c_str());
    if (loop_start >= 0)
    {
        FILE* lf = fopen(loopfn.c_str(), "w");
        if (lf) { fprintf(lf, "%ld\n", loop_start); fclose(lf); }
    }
    printf("%s: cmd 0x%02X, %.2fs, %s", out, cmd, smp.size() / (double)channels / rate, channels == 1 ? "mono" : "stereo");
    if (loop_start >= 0) printf(", loop %.3fs-%.3fs", loop_start / (double)rate, loop_end / (double)rate);
    printf("\n");
    return 0;
}
#endif


#ifdef __DREAMCAST__
// Game pause (START): "PAUSED" on text row 13, whose contents are put back
// on unpausing; sound effects and the engine go silent (streamed music plays on)
static void set_paused(bool on)
{
    static uint8_t saved[128];
    static int8_t  saved_xoff;
    const int ROW = 13;
    if (on == pause_engine)
        return;
    const uint32_t row = ohud.translate(0, ROW);
    if (on)
    {
        for (int i = 0; i < 128; i++) saved[i] = video.read_text8(row + i);
        saved_xoff = video.tile_layer->text_row_xoff[ROW];
        ohud.blit_text_centre(ROW, "PAUSED", OHud::GREEN);
        audio.set_game_paused(true);
        input.set_rumble(false, 0);
    }
    else
    {
        for (int i = 0; i < 128; i++) video.write_text8(row + i, saved[i]);
        video.tile_layer->text_row_xoff[ROW] = saved_xoff;
        audio.set_game_paused(false);
    }
    pause_engine = on;
}
#endif

static void tick()
{
    frame++;
    static int last_state = -1;
    if (last_state != state)
    {
        DC_TRACE("cannonball: state %d -> %d frame=%d\n", last_state, state, frame);
        last_state = state;
    }

    // Non standard FPS: Determine whether to tick certain logic for the current frame.
    if (config.fps == 60)
        tick_frame = frame & 1;
    else if (config.fps == 120)
        tick_frame = (frame & 3) == 1;

#ifdef __DREAMCAST__
    // Remote-exit hook for unattended hardware testing: kos-tool's -m maps
    // /pc/ to a real host directory (the same passthrough Config::load()
    // already uses for /pc/config.xml). Touching build-dc/cd/exit_now on
    // the host ends a running session without physical controller input --
    // no host-to-target dc-load command channel exists otherwise. Checked
    // every 30 frames, not every frame, to keep this off the hot path.
    if ((frame % 30) == 0)
    {
        FILE* dc_exit_sentinel = fopen("/pc/exit_now", "rb");
        if (dc_exit_sentinel)
        {
            fclose(dc_exit_sentinel);
            DC_TRACE("cannonball: /pc/exit_now detected, calling arch_exit()\n");
            arch_exit();
        }
    }
#ifdef DREAMCAST_EXIT_ON_STAGE_ADVANCE
    // Auto-exit once the current stage's road split has actually been
    // played through (ostats.cur_stage advances) -- for capturing exactly
    // one stage/fork per run instead of an arbitrary time budget. Latches
    // the baseline stage on the first STATE_GAME frame seen.
    if (state == STATE_GAME)
    {
        static bool dc_stage_baseline_set = false;
        static int8_t dc_stage_baseline = 0;
        static int dc_advances_seen = 0;
        if (!dc_stage_baseline_set)
        {
            dc_stage_baseline = ostats.cur_stage;
            dc_stage_baseline_set = true;
        }
        else if (ostats.cur_stage != dc_stage_baseline)
        {
            dc_advances_seen++;
            DC_TRACE("cannonball: stage advanced %d -> %d, stage_lookup_off=%d, advance %d/%d\n",
                     dc_stage_baseline, ostats.cur_stage, oroad.stage_lookup_off,
                     dc_advances_seen, DREAMCAST_EXIT_AFTER_N_ADVANCES);
            dc_stage_baseline = ostats.cur_stage;
            if (dc_advances_seen >= DREAMCAST_EXIT_AFTER_N_ADVANCES)
            {
                DC_TRACE("cannonball: target advance count reached, calling arch_exit()\n");
                arch_exit();
            }
        }
    }
#endif
#ifdef DREAMCAST_EXIT_ON_GAME_COMPLETE
    // Stage 5's ending never touches ostats.cur_stage (it goes through
    // GS_INIT_BONUS/GS_BONUS/hiscore instead of another road split), so
    // DREAMCAST_EXIT_ON_STAGE_ADVANCE can't catch it. ostats.game_completed
    // is set the instant the bonus sequence starts and only clears back to
    // 0 in the *next* OInitEngine::init() (i.e. once the whole bonus +
    // hiscore cycle has finished and GS_REINIT/GS_INIT has run) -- so a
    // 1 -> 0 edge on that flag means a full ending cycle just completed.
    if (state == STATE_GAME)
    {
        static uint8_t dc_prev_game_completed = 0;
        if (dc_prev_game_completed && !ostats.game_completed)
        {
            DC_TRACE("cannonball: ending cycle completed, calling arch_exit()\n");
            arch_exit();
        }
        dc_prev_game_completed = ostats.game_completed;
    }
#endif
#endif

    process_events();

    if (tick_frame)
    {
        oinputs.tick();           // Do Controls
        oinputs.do_gear();        // Digital Gear
    }
     
#ifdef __DREAMCAST__
    // '(' and ')' for the menu text exist only while the menu is up
    video.tile_layer->set_menu_glyphs(state == STATE_MENU);
#endif

    switch (state)
    {
        case STATE_GAME:
        {
            if (tick_frame)
            {
                if (input.has_pressed(Input::TIMER)) outrun.freeze_timer = !outrun.freeze_timer;
#ifdef __DREAMCAST__
                // START pauses while driving: "PAUSED" in the middle of the
                // screen, sound and music stopped
                if (input.has_pressed(Input::START) &&
                    outrun.game_state >= GS_START1 && outrun.game_state <= GS_INGAME)
                    set_paused(!pause_engine);
                if (input.has_pressed(Input::MENU))
                {
                    set_paused(false);
                    state = STATE_INIT_MENU;
                }
#else
                if (input.has_pressed(Input::PAUSE)) pause_engine = !pause_engine;
                if (input.has_pressed(Input::MENU))  state = STATE_INIT_MENU;
#endif
            }

#ifdef DREAMCAST_DEBUG_PAUSE
            {
                // Test builds: pause for 5 s after 12 s of driving, twice
                static int n = 0;
                if (tick_frame && outrun.game_state == GS_INGAME)
                {
                    ++n;
                    if (n == 720 || n == 1400) set_paused(true);
                    if (n == 1020 || n == 1700) set_paused(false);
                    if (n == 720 || n == 1020) printf("PAUSEDBG %d paused=%d\n", n, pause_engine);
                }
            }
#endif
            if (!pause_engine || input.has_pressed(Input::STEP))
            {
                outrun.tick(tick_frame);
#ifdef SCORE_SIM
                sim_check(frame);
#endif
                if (tick_frame) input.frame_done();
                osoundint.tick();
            }
            else
            {                
                if (tick_frame) input.frame_done();
            }
        }
        break;

        case STATE_INIT_GAME:
            if (config.engine.jap && !roms.load_japanese_roms())
            {
                DC_TRACE("cannonball: STATE_INIT_GAME japanese rom load failed\n");
                state = STATE_QUIT;
            }
            else
            {
                tick_frame = true;
                pause_engine = false;
                outrun.init();
                state = STATE_GAME;
            }
            break;

        case STATE_MENU:
            menu->tick();
            input.frame_done();
            osoundint.tick();
            break;

        case STATE_INIT_MENU:
            oinputs.init();
            outrun.outputs->init();
            menu->init();
            state = STATE_MENU;
            break;
    }

    // Map OutRun outputs to CannonBall devices (SmartyPi Interface / Controller Rumble)
    outrun.outputs->writeDigitalToConsole();
    if (tick_frame)
    {
         input.set_rumble(!pause_engine && outrun.outputs->is_set(OOutputs::D_MOTOR), config.controls.rumble);
    }
}

#ifdef SCORE_SIM
// Leaderboard test: CB_LBTEST=<url> prints the QR link for a table with
// CB_LBN (default 3) of our own entries and writes the QR as a PBM (CB_LBQR)
#include "frontend/leaderboard.hpp"
#include "engine/oaddresses.hpp"
#include "engine/ohiscore.hpp"
static int leaderboard_test_main()
{
    ostats.init(false);
    ohiscore.init_def_scores();
    leaderboard::load_pass();
    const int n = getenv("CB_LBN") ? atoi(getenv("CB_LBN")) : 3;
    for (int i = 0; i < n && i < OHiScore::NO_SCORES; i++)
    {
        score_entry& e = ohiscore.scores[i];
        uint32_t dec = 29876540 - (uint32_t)i * 1234560, bcd = 0;  // to BCD
        for (int d = 0; d < 8; d++, dec /= 10) bcd |= (dec % 10) << (4 * d);
        e.score    = bcd;
        e.initial1 = 'N'; e.initial2 = 'A' + i; e.initial3 = i == 1 ? '[' : 'T';
        const int node = i == 2 ? 3 : 15 + (i * 5) % 16;      // entry 2: out of time on stage 3
        e.maptiles = roms.rom0.read32(TILES_MINIMAP + (node << 2));
        e.time     = i == 2 ? 0 : (uint16_t)(4 * 3600 + ((20 + i) << 6) + 13);
    }
    const std::string link = leaderboard::build_link();
    printf("%s\n", link.c_str());
    static uint8_t m[leaderboard::MAX_SIZE * leaderboard::MAX_SIZE];
    const int size = leaderboard::make_qr(link, m);
    fprintf(stderr, "QR %dx%d, link %u chars\n", size, size, (unsigned)link.size());
    if (getenv("CB_LBQR") && size)
    {
        FILE* f = fopen(getenv("CB_LBQR"), "w");
        const int q = 4, t = size + 2 * q;
        fprintf(f, "P1\n%d %d\n", t, t);
        for (int y = 0; y < t; y++, fputc('\n', f))
            for (int x = 0; x < t; x++)
            {
                const int mx = x - q, my = y - q;
                fputs(mx >= 0 && my >= 0 && mx < size && my < size && m[my * size + mx] ? "1 " : "0 ", f);
            }
        fclose(f);
    }
    return size ? 0 : 1;
}
#endif

static void main_loop()
{
    // FPS Counter (If Enabled)
    Timer fps_count;
    int frame = 0;
    fps_count.start();

    // General Frame Timing
    bool vsync = config.video.vsync == 1 && video.supports_vsync();
    Timer frame_time;
    int t;                              // Actual timing of tick in ms as measured by SDL (ms)
    double deltatime  = 0;              // Time we want an entire frame to take (ms)
    int deltaintegral = 0;              // Integer version of above

#ifdef __DREAMCAST__
    uint32_t perf_last = SDL_GetTicks();
    int perf_frames = 0;
    uint32_t perf_tick = 0;
    uint32_t perf_prepare = 0;
    uint32_t perf_render = 0;
    uint32_t perf_audio = 0;
    uint32_t perf_total = 0;
    // On-screen timing readout (FPS COUNTER: DETAIL), microseconds
    uint64_t ro_last = timer_us_gettime64();
    uint32_t ro_tick = 0, ro_prep = 0, ro_render = 0, ro_audio = 0, ro_n = 0;
#endif

    while (state != STATE_QUIT)
    {
        frame_time.start();
#ifdef __DREAMCAST__
        const uint32_t perf_frame_start = SDL_GetTicks();
#endif
        // Tick Engine
#ifdef __DREAMCAST__
        uint32_t perf_start = SDL_GetTicks();
        uint64_t ro_t = timer_us_gettime64();
#endif
        tick();
#ifdef __DREAMCAST__
        vmu_lcd::tick();
        perf_tick += SDL_GetTicks() - perf_start;
        { const uint64_t n = timer_us_gettime64(); ro_tick += (uint32_t)(n - ro_t); ro_t = n; }
#endif

        // Draw SDL Video
#ifdef __DREAMCAST__
        perf_start = SDL_GetTicks();
#endif
#ifdef SCORE_SIM
        continue;
#endif
        video.prepare_frame();
#ifdef __DREAMCAST__
        perf_prepare += SDL_GetTicks() - perf_start;
        perf_start = SDL_GetTicks();
        { const uint64_t n = timer_us_gettime64(); ro_prep += (uint32_t)(n - ro_t); ro_t = n; }
#endif
        video.render_frame();
#ifdef __DREAMCAST__
        perf_render += SDL_GetTicks() - perf_start;
        { const uint64_t n = timer_us_gettime64(); ro_render += (uint32_t)(n - ro_t); ro_t = n; }
#endif
#ifdef DREAMCAST_PVR_COMPARE
        pvr_compare_after_frame();
#endif

        // Fill SDL Audio Buffer For Callback
#ifdef __DREAMCAST__
        perf_start = SDL_GetTicks();
#endif
        audio.tick();
#ifdef __DREAMCAST__
        perf_audio += SDL_GetTicks() - perf_start;
        ro_audio += (uint32_t)(timer_us_gettime64() - ro_t);
        ro_n++;
        if (timer_us_gettime64() - ro_last >= 1000000 && ro_n)
        {
            // Average ms per frame: G game logic, B background and road
            // (CPU), S sprite images prepared, D drawing sent to the GPU,
            // W waiting for the GPU to finish the previous frame, A sound
            uint32_t r[5] = { 0, 0, 0, 0, 0 };
#ifdef DREAMCAST_PVR_RENDERER
            pvr_render_take_timing(r);
#endif
            const uint32_t rf = r[4] ? r[4] : 1;
            const float v[6] = { ro_tick / 1000.0f / ro_n, ro_prep / 1000.0f / ro_n,
                                 r[1] / 1000.0f / rf, (r[2] > r[1] ? r[2] - r[1] : 0) / 1000.0f / rf,
                                 r[3] / 1000.0f / rf, ro_audio / 1000.0f / ro_n };
            const char lbl[6] = { 'G', 'B', 'S', 'D', 'W', 'A' };
            char* o = perf_line;
            for (int i = 0; i < 6; i++)
            {
                if (v[i] < 9.95f) o += sprintf(o, "%c%d.%d ", lbl[i], (int)(v[i] + 0.05f), (int)((v[i] + 0.05f) * 10) % 10);
                else              o += sprintf(o, "%c%-3d ", lbl[i], (int)(v[i] + 0.5f));
            }
            ro_last = timer_us_gettime64();
            ro_tick = ro_prep = ro_render = ro_audio = ro_n = 0;
        }
#endif
        
        // Calculate Timings. Cap Frame Rate. Note this might be trumped by V-Sync
        if (!vsync)
        {
            deltatime += (frame_ms * audio.adjust_speed());
            deltaintegral = (int)deltatime;
            t = frame_time.get_ticks();
            
            if (t < deltatime)
                SDL_Delay((Uint32)(deltatime - t));

            deltatime -= deltaintegral;
        }

#ifdef __DREAMCAST__
        perf_total += SDL_GetTicks() - perf_frame_start;
        perf_frames++;
        const uint32_t perf_now = SDL_GetTicks();
        if (perf_now - perf_last >= DC_PERF_INTERVAL_MS)
        {
            const uint32_t perf_elapsed = perf_now - perf_last;
            const uint32_t perf_fps = (perf_frames * 1000) / perf_elapsed;
            #ifdef DREAMCAST_DEBUG_MUSIC
            {
                DC_TRACE("cannonball: wav reads=%u bytes=%u short=%u fm_chip=%d t=%lu\n", wav_debug_reads, wav_debug_bytes, wav_debug_short, custommusic::fm_chip_needed() ? 1 : 0, (unsigned long)SDL_GetTicks());
            }
#endif
            DC_TRACE("cannonball: perf fps=%lu avg_ms total=%lu tick=%lu prep=%lu render=%lu audio=%lu state=%d target_fps=%d\n",
                     perf_fps,
                     (unsigned long)(perf_total / perf_frames),
                     (unsigned long)(perf_tick / perf_frames),
                     (unsigned long)(perf_prepare / perf_frames),
                     (unsigned long)(perf_render / perf_frames),
                     (unsigned long)(perf_audio / perf_frames),
                     state,
                     config.fps);
            perf_last = perf_now;
            perf_frames = 0;
            perf_tick = 0;
            perf_prepare = 0;
            perf_render = 0;
            perf_audio = 0;
            perf_total = 0;
        }
#endif

        if (config.video.fps_count)
        {
            frame++;
            // One second has elapsed
            if (fps_count.get_ticks() >= 1000)
            {
                fps_counter = frame;
                frame       = 0;
                fps_count.start();
            }
        }
    }

    quit_func(0);
}

// Very (very) simple command line parser.
// Returns true if everything is ok to proceed with launching th engine.
static bool parse_command_line(int argc, char* argv[])
{
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-cfgfile") == 0 && i+1 < argc)
        {
            config.set_config_file(argv[i+1]);
        }
        else if (strcmp(argv[i], "-file") == 0 && i+1 < argc)
        {
            if (!trackloader.set_layout_track(argv[i+1]))
                return false;
        }
        else if (strcmp(argv[i], "-help") == 0)
        {
            std::cout << "Command Line Options:\n\n" <<
                         "-cfgfile: Location and name of config.xml\n" <<
                         "-file   : LayOut Editor track data to load\n" << std::endl;
            return false;
        }
    }
    return true;
}


static int cannonball_main(int argc, char* argv[])
{
#ifdef DREAMCAST_PVR_SELFTEST
    // Renderer self-test build: no ROMs, no game. See dreamcast/pvr_selftest.cpp
    return pvr_selftest_main();
#endif
    DC_STARTUP_TRACE("cannonball: app thread start argc=%d\n", argc);
    // Parse command line arguments (config file location, LayOut data) 
    bool ok = parse_command_line(argc, argv);
    DC_STARTUP_TRACE("cannonball: parse_command_line -> %d\n", ok);

    if (ok)
    {
        DC_STARTUP_TRACE("cannonball: config.load begin\n");
        config.load(); // Load config.XML file
#ifdef RENDER_AUDIO
        if (getenv("CB_FIX")) config.sound.fix_samples = atoi(getenv("CB_FIX"));
#endif
#ifdef SCORE_SIM
        sim_apply_env();
#endif
        DC_STARTUP_TRACE("cannonball: config.load done rom=%s res=%s save=%s\n",
                 config.data.rom_path.c_str(), config.data.res_path.c_str(), config.data.save_path.c_str());
        DC_STARTUP_TRACE("cannonball: roms.load_revb_roms begin\n");
        ok = roms.load_revb_roms(config.sound.fix_samples);
        DC_STARTUP_TRACE("cannonball: roms.load_revb_roms -> %d\n", ok);
    }
    if (!ok)
    {
        DC_TRACE("cannonball: startup failed before SDL init\n");
        return 1;
    }
#ifdef RENDER_AUDIO
    return render_audio_main();
#endif
#ifdef SCORE_SIM
    if (getenv("CB_LBTEST")) return leaderboard_test_main();
#endif
#if defined(__DREAMCAST__) && !defined(DREAMCAST_PVR_RENDERER)
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "Dreamcast PVR");
    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    // SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
#endif
    // Load gamecontrollerdb.txt mappings
    DC_STARTUP_TRACE("cannonball: SDL mappings begin\n");
    if (SDL_GameControllerAddMappingsFromFile((config.data.res_path + "gamecontrollerdb.txt").c_str()) == -1)
        std::cout << "Unable to load controller mapping" << std::endl;
    DC_STARTUP_TRACE("cannonball: SDL mappings done\n");
    // Initialize timer and video systems
    DC_STARTUP_TRACE("cannonball: SDL_Init begin\n");
#ifdef DREAMCAST_PVR_RENDERER
    // Video is driven directly through KallistiOS' PVR API (dreamcast/pvr_render.cpp),
    // so SDL's video subsystem (which would also initialise the PVR) stays off.
    const Uint32 sdl_flags = SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC;
#else
    const Uint32 sdl_flags = SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC;
#endif
    if (SDL_Init(sdl_flags) == -1)
    {
        std::cerr << "SDL Initialization Failed: " << SDL_GetError() << std::endl;
        DC_TRACE("cannonball: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    DC_STARTUP_TRACE("cannonball: SDL_Init done\n");

    // Load patched widescreen tilemaps
    DC_STARTUP_TRACE("cannonball: load_widescreen_map begin\n");
    if (!omusic.load_widescreen_map(config.data.res_path))
        std::cout << "Unable to load widescreen tilemaps" << std::endl;
    DC_STARTUP_TRACE("cannonball: load_widescreen_map done\n");

    // Initialize SDL Video
    DC_STARTUP_TRACE("cannonball: video.init begin\n");
    config.set_fps(config.video.fps);
    if (!video.init(&roms, &config.video))
    {
        DC_TRACE("cannonball: video.init failed\n");
        quit_func(1);
    }
    DC_STARTUP_TRACE("cannonball: video.init done\n");

    // Initialize SDL Audio
    DC_STARTUP_TRACE("cannonball: audio.init begin\n");
#ifdef DREAMCAST_CUSTOM_MUSIC
    custommusic::init();
#endif
    audio.init();
    DC_STARTUP_TRACE("cannonball: audio.init done\n");

    state = config.menu.enabled ? STATE_INIT_MENU : STATE_INIT_GAME;

    // Initalize SDL Controls
    DC_STARTUP_TRACE("cannonball: input.init begin\n");
    input.init(config.controls.pad_id,
               config.controls.keyconfig, config.controls.padconfig, 
               config.controls.analog,    config.controls.axis, config.controls.invert, config.controls.asettings);
    DC_STARTUP_TRACE("cannonball: input.init done\n");

    if (config.controls.haptic) 
        config.controls.haptic = forcefeedback::init(config.controls.max_force, config.controls.min_force, config.controls.force_duration);
        
    // Populate menus
    DC_STARTUP_TRACE("cannonball: menu populate begin\n");
    menu = new Menu();
    menu->populate();
    DC_STARTUP_TRACE("cannonball: main_loop begin state=%d\n", state);
    main_loop();  // Loop until we quit the app

    // Never Reached
    return 0;
}

#ifdef __DREAMCAST__
struct dreamcast_main_args_t
{
    int argc;
    char** argv;
};

static void* dreamcast_main_thread(void* param)
{
    dreamcast_main_args_t* args = static_cast<dreamcast_main_args_t*>(param);
    return reinterpret_cast<void*>(static_cast<intptr_t>(cannonball_main(args->argc, args->argv)));
}

int main(int argc, char* argv[])
{
    cont_btn_callback(0,
        CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
        (cont_btn_callback_t)arch_exit);

    dreamcast_main_args_t args = { argc, argv };
    kthread_attr_t attr = {};
    void* thread_result = NULL;

    attr.stack_size = 2 * 1024 * 1024;
    attr.prio = PRIO_DEFAULT;
    attr.label = "cannonball";

    kthread_t* thread = thd_create_ex(&attr, dreamcast_main_thread, &args);
    if (!thread)
    {
        dbglog(DBG_ERROR, "cannonball: failed to create app thread\n");
        return 1;
    }

    thd_join(thread, &thread_result);
    return static_cast<int>(reinterpret_cast<intptr_t>(thread_result));
}
#else
int main(int argc, char* argv[])
{
    return cannonball_main(argc, argv);
}
#endif
