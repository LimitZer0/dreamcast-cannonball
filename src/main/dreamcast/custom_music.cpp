/***************************************************************************
    Dreamcast: music and FM sound effects streamed by the AICA.
    See custom_music.hpp.
***************************************************************************/

#include <string>
#include <cstring>
#include <cstdlib>
#include <kos/fs.h>
#include <kos/dbglog.h>
#include <arch/timer.h>
#include <dc/sound/stream.h>
#include <dc/sound/sfxmgr.h>

#include "dreamcast/custom_music.hpp"
#include "dreamcast/libwav/sndwav.h"
#include "frontend/config.hpp"
#include "engine/audio/commands.hpp"

namespace custommusic
{
    namespace
    {
        const int TRACKS = 4;
        const char* TITLES[TRACKS] =
        {
            "MAGICAL SOUND SHOWER", "PASSING BREEZE", "SPLASH WAVE", "LAST WAVE"
        };
        const char* EXTS[] = { ".wav", ".WAV", ".adpcm", ".ADPCM", ".raw", ".RAW" };

        // The FM chip's sound effects, recorded to /cd/sfx/<command>.wav
        const int FX_COUNT = 6;
        const uint8_t FX_CMDS[FX_COUNT] =
        {
            sound::COIN_IN, sound::YM_CHECKPOINT, sound::SIGNAL1,
            sound::SIGNAL2, sound::BEEP1, sound::BEEP2
        };

        std::string files[TRACKS];      // your music (/cd/music/)
        std::string orig[TRACKS];       // recorded original music (/cd/music/orig/)
        long orig_loop[TRACKS];         // loop start in sample frames, -1 = plays once
        std::string fx_files[FX_COUNT];
        sfxhnd_t fx[FX_COUNT];
        bool fx_loaded   = false;
        bool audio_up    = false;       // the AICA/sound system is running

        bool lib_ready   = false;
        bool suspended   = false;
        bool paused      = false;
        int  playing     = -1;          // track index, -1 = none
        bool playing_orig = false;
        wav_stream_hnd_t hnd = SND_STREAM_INVALID;

        // The FM chip is still needed while it plays music, or for a moment
        // after it was sent an effect we have no recording of
        bool     chip_music   = false;
        uint64_t chip_fx_until = 0;

        const int VOLUME_CUSTOM = 210;  // your music: a little under the chips
        const int VOLUME_ORIG   = 255;  // recordings are mixed exactly like the
                                        // emulated chips, which SDL streams at 255
        const int VOLUME_FX     = 255;

        bool exists(const std::string& path)
        {
            file_t f = fs_open(path.c_str(), O_RDONLY);
            if (f == FILEHND_INVALID) return false;
            fs_close(f);
            return true;
        }

        long read_loop(const std::string& path)
        {
            file_t f = fs_open(path.c_str(), O_RDONLY);
            if (f == FILEHND_INVALID) return -1;
            char buf[32] = {0};
            fs_read(f, buf, sizeof(buf) - 1);
            fs_close(f);
            return atol(buf);
        }

        int track_for(uint8_t cmd)
        {
            switch (cmd)
            {
                case sound::MUSIC_MAGICAL:  return 0;
                case sound::MUSIC_BREEZE:   return 1;
                case sound::MUSIC_SPLASH:   return 2;
                case sound::MUSIC_LASTWAVE: return 3;
            }
            return -1;
        }

        int fx_for(uint8_t cmd)
        {
            for (int i = 0; i < FX_COUNT; i++)
                if (FX_CMDS[i] == cmd) return i;
            return -1;
        }

        bool is_fm_command(uint8_t cmd)
        {
            return fx_for(cmd) >= 0 || cmd == sound::UFO || cmd == sound::MUSIC_CUSTOM;
        }

        void destroy_stream()
        {
            if (hnd != SND_STREAM_INVALID)
            {
                wav_stop(hnd);
                wav_destroy(hnd);
                hnd = SND_STREAM_INVALID;
            }
        }

        void load_fx()
        {
            if (fx_loaded) return;
            for (int i = 0; i < FX_COUNT; i++)
            {
                fx[i] = SFXHND_INVALID;
                if (!fx_files[i].empty())
                    fx[i] = snd_sfx_load(fx_files[i].c_str());
                if (!fx_files[i].empty())
                    dbglog(DBG_INFO, "cannonball: FM effect %s %s\n", fx_files[i].c_str(), fx[i] != SFXHND_INVALID ? "loaded" : "FAILED");
            }
            fx_loaded = true;
        }

        void unload_fx()
        {
            if (!fx_loaded) return;
            for (int i = 0; i < FX_COUNT; i++)
            {
                if (fx[i] != SFXHND_INVALID)
                    snd_sfx_unload(fx[i]);
                fx[i] = SFXHND_INVALID;
            }
            fx_loaded = false;
        }

#ifdef DREAMCAST_DEBUG_MUSIC
        void debug_filter(wav_stream_hnd_t, void*, int hz, int channels, void** buffer, int* samplecnt)
        {
            static int calls = 0, total = 0;
            total += *samplecnt;
            if ((++calls % 50) == 0)
                dbglog(DBG_INFO, "cannonball: music streamed %d samples (hz=%d ch=%d)\n", total, hz, channels);
        }
#endif

        bool start(int track)
        {
            if (!lib_ready)
            {
                if (!wav_init()) return false;
                lib_ready = true;
            }
            destroy_stream();
            const std::string& fn = playing_orig ? orig[track] : files[track];
            const bool loops = !playing_orig || orig_loop[track] >= 0;
            hnd = wav_create(fn.c_str(), loops ? 1 : 0);
            if (hnd == SND_STREAM_INVALID)
            {
                dbglog(DBG_INFO, "cannonball: music: cannot open %s\n", fn.c_str());
                return false;
            }
            if (playing_orig && orig_loop[track] > 0)
            {
                // Recordings are 4-bit stereo ADPCM: one byte per sample frame
                wav_set_loop_start(hnd, (uint32_t)orig_loop[track]);
            }
            wav_volume(hnd, playing_orig ? VOLUME_ORIG : VOLUME_CUSTOM);
#ifdef DREAMCAST_DEBUG_MUSIC
            wav_add_filter(hnd, debug_filter, NULL);
#endif
            wav_play(hnd);
            dbglog(DBG_INFO, "cannonball: music: playing %s\n", fn.c_str());
            return true;
        }
    }

    void init()
    {
        const std::string dir = "/cd/music/";
        for (int t = 0; t < TRACKS; t++)
        {
            files[t].clear();
            for (size_t e = 0; e < sizeof(EXTS) / sizeof(EXTS[0]) && files[t].empty(); e++)
            {
                const std::string by_number = dir + std::to_string(t + 1) + EXTS[e];
                const std::string by_title  = dir + TITLES[t] + EXTS[e];
                if (exists(by_number))      files[t] = by_number;
                else if (exists(by_title))  files[t] = by_title;
            }
            if (!files[t].empty())
                dbglog(DBG_INFO, "cannonball: custom music track %d: %s\n", t + 1, files[t].c_str());

            orig[t].clear();
            orig_loop[t] = -1;
            const std::string o = dir + "orig/" + std::to_string(t + 1) + ".wav";
            if (exists(o))
            {
                orig[t] = o;
                orig_loop[t] = read_loop(o + ".loop");
                dbglog(DBG_INFO, "cannonball: original music track %d: %s (loop %ld)\n", t + 1, o.c_str(), orig_loop[t]);
            }
        }

        for (int i = 0; i < FX_COUNT; i++)
        {
            char fn[32];
            snprintf(fn, sizeof(fn), "/cd/sfx/%02X.wav", FX_CMDS[i]);
            fx_files[i] = exists(fn) ? fn : "";
            fx[i] = SFXHND_INVALID;
        }

        // The audio device may already be running (it can start before this
        // scan); load the effects now in that case
        if (audio_up)
        {
            fx_loaded = false;
            load_fx();
        }
    }

    bool available()
    {
        for (int t = 0; t < TRACKS; t++)
            if (!files[t].empty()) return true;
        return false;
    }

    bool handle_command(uint8_t cmd)
    {
        // FM chip reset stops the music (0xFF is treated the same by the
        // sound CPU). Note sound::RESET (0x80) is NOT a stop: the sound code
        // ignores it, and the game sends it every time you pass a car.
        if (cmd == sound::FM_RESET || cmd == 0xFF)
        {
            stop();
            chip_music = false;
            return false;   // the chip still needs its reset
        }

        // FM sound effects: play the recording, but still pass the command on
        // to the sound program. Some effects also change its state (the last
        // start signal ends the start-line rev effect), and while the FM chip
        // isn't being synthesised its own copy of the effect is silent.
        const int f = fx_for(cmd);
        if (f >= 0 && fx_loaded && fx[f] != SFXHND_INVALID && !suspended && !chip_music)
        {
            if (config.sound.enabled)
            {
                const int ch = snd_sfx_play(fx[f], VOLUME_FX, 128);
#ifdef DREAMCAST_DEBUG_MUSIC
                dbglog(DBG_INFO, "cannonball: FM effect %02X from recording (channel %d)\n", cmd, ch);
#else
                (void)ch;
#endif
            }
            return false;
        }
        if (is_fm_command(cmd))
        {
            if (cmd == sound::MUSIC_CUSTOM) chip_music = true;
            chip_fx_until = timer_ms_gettime64() + 5000;
            return false;
        }

        const int track = track_for(cmd);
        if (track < 0)
            return false;

        // Music off: swallow the command (the FM chip stays idle too)
        if (config.sound.custom_music == 2)
        {
            stop();
            chip_music = false;
            return true;
        }

        // Your music if chosen and present, else the recorded original,
        // else the emulated chips
        bool use_orig;
        if (config.sound.custom_music == 1 && !files[track].empty())
            use_orig = false;
        else if (!orig[track].empty())
            use_orig = true;
        else
        {
            stop();
            chip_music = true;
            return false;
        }
        chip_music = false;

        // Music select screen re-requests the highlighted track; keep playing
        if (track == playing && use_orig == playing_orig && hnd != SND_STREAM_INVALID)
            return true;

        playing = track;
        playing_orig = use_orig;
        paused = false;
        if (!suspended)
            start(track);
        return true;
    }

    bool chip_music_active()
    {
        return chip_music;
    }

    bool fm_chip_needed()
    {
        return chip_music || timer_ms_gettime64() < chip_fx_until;
    }

    void stop()
    {
#ifdef DREAMCAST_DEBUG_MUSIC
        if (playing >= 0) dbglog(DBG_INFO, "cannonball: music stopped\n");
#endif
        playing = -1;
        paused = false;
        destroy_stream();
    }

    void suspend()
    {
        if (suspended) return;
        suspended = true;
        destroy_stream();
        unload_fx();
        audio_up = false;
        if (lib_ready)
        {
            wav_shutdown();
            lib_ready = false;
        }
    }

    void resume()
    {
        // Called whenever the audio device has (re)started
        suspended = false;
        audio_up = true;
        load_fx();
        if (playing >= 0 && !paused && hnd == SND_STREAM_INVALID)
            start(playing);
    }

    void pause()
    {
        if (hnd != SND_STREAM_INVALID && !paused)
        {
            wav_pause(hnd);
            paused = true;
        }
    }

    void unpause()
    {
        if (hnd != SND_STREAM_INVALID && paused)
        {
            wav_play(hnd);
            paused = false;
        }
    }
}
