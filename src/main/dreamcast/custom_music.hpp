/***************************************************************************
    Dreamcast: music and FM sound effects streamed by the AICA.

    The Dreamcast's sound chip plays these by itself, so they cost the main
    CPU almost nothing, and the YM2151 FM chip no longer has to be emulated.

    Your own music, in /cd/music/ (put there when the disc image is built):
        1.wav  Magical Sound Shower
        2.wav  Passing Breeze
        3.wav  Splash Wave
        4.wav  Last Wave (high score music)
    Titles work too, e.g. "PASSING BREEZE.wav". WAV files can be 16-bit PCM
    or Yamaha ADPCM (4-bit, much smaller - what the disc builder produces).
    Chosen with SETTINGS > SOUND > MUSIC SOURCE = CUSTOM.

    The original music, recorded from the game's own sound code when the
    disc is built (tools/render_audio.sh): /cd/music/orig/1.wav-4.wav, with
    N.wav.loop holding the loop start in sample frames (no file = plays
    once). Used for ORIGINAL, and for any track you have no file for.

    The FM chip's sound effects, recorded the same way: /cd/sfx/<cmd>.wav
    (84 coin, 86 checkpoint, 94/95 start signals, 99/9B beeps).

    Anything without a recording falls back to the emulated chips.
***************************************************************************/

#pragma once

#include <stdint.h>

namespace custommusic
{
    // Find the music files on the disc. Call once at start-up.
    void init();

    // True if at least one custom track file was found
    bool available();

    // Called for every sound command the game queues. Returns true if the
    // command was handled here (so the chip must not play it).
    bool handle_command(uint8_t cmd);

    // False while nothing needs the emulated FM chip (music and FM effects
    // are all coming from recordings), so its synthesis can be skipped
    bool fm_chip_needed();

    // The emulated chips are playing the music (no recording for it)
    bool chip_music_active();

    // Stop any custom track (e.g. when switching back to original music)
    void stop();

    // Sound system restarts (SDL audio shuts all AICA streams down)
    void suspend();
    void resume();

    // Game pause
    void pause();
    void unpause();
}
