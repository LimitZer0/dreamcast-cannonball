/***************************************************************************
    Online leaderboard: the high score table as a QR code.

    The QR holds a link to the leaderboard website with the player's own
    entries of a high score table, encrypted and signed with the site's key.
    Address and key come from a QRLeaderboard pass on a memory card
    (tools/qrleaderboard). The website decrypts the link, asks for a user
    name and stores the scores. See web/leaderboard/.

    Packet (then base32, A-Z2-7 without padding, as the "d" parameter):
      version (1) = 1 | nonce (8) | ciphertext | MAC (10)
    Plaintext:
      table (1) | flags (1) | count (1) | count x entry (11)
      table: 0-7, bit 0 Japanese tracks, bit 1 continuous, bit 2 modified
      flags: bit 0 score scaling on
      entry: score (u32 LE, decimal points) | 3 initials (tile codes: 'A'-'Z',
             '[' = '.', ' ') | route (0-30, 0xFF unknown) |
             time (u24 LE, hundredths as shown in the game, 0 = not finished)
      route: 0 = stage 1; stage s (0-4) positions 2^s-1 ... 2^(s+1)-2, and the
             bits of (route - 2^s + 1) from the top are the forks (1 = right)
    Crypto (SHA-256 based):
      Kenc = HMAC(key, "enc"), Kmac = HMAC(key, "mac")
      ciphertext = plaintext XOR (HMAC(Kenc, nonce | u32le 0) | HMAC(..., 1) ...)
      MAC = first 10 bytes of HMAC(Kmac, version | nonce | ciphertext)
***************************************************************************/

#pragma once

#include <stdint.h>
#include <string>

namespace leaderboard
{
    // Look for a QRLeaderboard pass (file QRLEADERBRD) on the memory cards:
    // it holds the site address and key. Called on entering the menu.
    bool load_pass();

    // A pass was found: offer SUBMIT SCORES / the high score QR code
    bool enabled();

    // Link for the current high score table (empty if nothing to send)
    std::string build_link();

    // Encodes a link as a QR code. Returns the size in modules (0 = failed);
    // modules[y * size + x] is 1 for dark.
    const int MAX_SIZE = 177;
    int make_qr(const std::string& link, uint8_t* modules);

    // High score screen: call every frame while the table is shown.
    // table_ready = the table is final (no initials being entered).
    // Shows the hint; accelerate + brake together toggle the QR code.
    // Returns true while the QR code is up: hold the screen's timer then.
    bool tick_hiscore(bool table_ready);

    // Hide the QR code and forget the table (new high score screen)
    void reset();

    // For the renderer: the QR code to draw over the screen, or NULL
    const uint8_t* overlay(int* size);
    unsigned overlay_serial();    // changes whenever a new QR code is made

    // Link for one of the 8 tables (its scores must be in ohiscore.scores)
    std::string build_link_for(int table);

    // Menu SUBMIT SCORES: QR codes for every saved table with our own
    // scores. menu_open returns how many (0 = nothing to send); the QR shows
    // until menu_close. menu_step(+1/-1) switches table.
    int  menu_open();
    void menu_step(int dir);
    void menu_close();
    int  menu_tables();
    std::string menu_table_name();
}
