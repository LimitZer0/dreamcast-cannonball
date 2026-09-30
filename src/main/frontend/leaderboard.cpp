/***************************************************************************
    Online leaderboard: the high score table as a QR code.
    See leaderboard.hpp for the format.
***************************************************************************/

#include <cstring>
#include <ctime>

#include "frontend/leaderboard.hpp"
#include "frontend/config.hpp"
#include "frontend/qrcodegen.h"
#include "frontend/timeattack.hpp"
#include "engine/outrun.hpp"
#include "engine/ohiscore.hpp"
#include "engine/ostats.hpp"
#include "engine/oaddresses.hpp"
#include "roms.hpp"
#include "engine/ohud.hpp"
#include "sdl2/input.hpp"
#ifdef __DREAMCAST__
#include "frontend/vmu.hpp"
#endif

#ifdef __DREAMCAST__
#include <arch/timer.h>
#include <kos/fs.h>
#include <kos/dbglog.h>
#endif
#include <cstdio>
#include <cstdlib>

namespace leaderboard
{
namespace
{
    // ---------------------------------------------------------------------
    // SHA-256 and HMAC-SHA256
    // ---------------------------------------------------------------------
    struct Sha256
    {
        uint32_t h[8];
        uint8_t  buf[64];
        uint64_t len;
        int      fill;
    };

    const uint32_t K256[64] =
    {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };

    inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void sha_block(Sha256& s, const uint8_t* p)
    {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; i++)
        {
            const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3], e = s.h[4], f = s.h[5], g = s.h[6], h = s.h[7];
        for (int i = 0; i < 64; i++)
        {
            const uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
            const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        s.h[0] += a; s.h[1] += b; s.h[2] += c; s.h[3] += d;
        s.h[4] += e; s.h[5] += f; s.h[6] += g; s.h[7] += h;
    }

    void sha_init(Sha256& s)
    {
        static const uint32_t H0[8] =
        {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };
        memcpy(s.h, H0, sizeof(H0));
        s.len = 0;
        s.fill = 0;
    }

    void sha_update(Sha256& s, const uint8_t* p, size_t n)
    {
        s.len += n;
        while (n--)
        {
            s.buf[s.fill++] = *p++;
            if (s.fill == 64) { sha_block(s, s.buf); s.fill = 0; }
        }
    }

    void sha_final(Sha256& s, uint8_t out[32])
    {
        const uint64_t bits = s.len * 8;
        const uint8_t pad80 = 0x80, zero = 0;
        sha_update(s, &pad80, 1);
        while (s.fill != 56) sha_update(s, &zero, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
        sha_update(s, l, 8);
        for (int i = 0; i < 8; i++)
        {
            out[i * 4]     = (uint8_t)(s.h[i] >> 24);
            out[i * 4 + 1] = (uint8_t)(s.h[i] >> 16);
            out[i * 4 + 2] = (uint8_t)(s.h[i] >> 8);
            out[i * 4 + 3] = (uint8_t)s.h[i];
        }
    }

    // HMAC-SHA256 of two concatenated parts (either may be empty)
    void hmac(const uint8_t key[32], const uint8_t* a, size_t an, const uint8_t* b, size_t bn, uint8_t out[32])
    {
        uint8_t k[64];
        memset(k, 0, sizeof(k));
        memcpy(k, key, 32);
        uint8_t pad[64];
        Sha256 s;

        for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
        sha_init(s);
        sha_update(s, pad, 64);
        if (an) sha_update(s, a, an);
        if (bn) sha_update(s, b, bn);
        uint8_t inner[32];
        sha_final(s, inner);

        for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
        sha_init(s);
        sha_update(s, pad, 64);
        sha_update(s, inner, 32);
        sha_final(s, out);
    }

    // ---------------------------------------------------------------------
    // Packet
    // ---------------------------------------------------------------------
    const uint8_t VERSION   = 1;
    const int     NONCE_LEN = 8;
    const int     MAC_LEN   = 10;
    const int     ENTRY_LEN = 11;
    const int     TT_ENTRY_LEN = 7;     // time trial entry
    const int     TT_PAGE = 30;         // time trial entries per QR code

    int table_index()
    {
        const bool original = outrun.cannonball_mode == Outrun::MODE_ORIGINAL;
        return (config.engine.jap ? 1 : 0) | (original ? 0 : 2) | (OStats::assists_enabled() ? 4 : 0);
    }

    // The arcade's own entries are in every table; don't send those
    bool is_default(const score_entry& e)
    {
        uint32_t adr = DEFAULT_SCORES;
        for (int i = 0; i < OHiScore::NO_SCORES; i++)
        {
            const uint32_t score    = roms.rom0.read32(&adr);
            const uint32_t initials = roms.rom0.read32(&adr);
            const uint16_t time     = roms.rom0.read16(&adr);
            const uint32_t maptiles = roms.rom0.read32(&adr);
            if (e.score == score && e.time == time && e.maptiles == maptiles &&
                e.initial1 == ((initials >> 24) & 0xFF) &&
                e.initial2 == ((initials >> 16) & 0xFF) &&
                e.initial3 == ((initials >> 8) & 0xFF))
                return true;
        }
        return false;
    }

    // Route map tiles back to the route (see OHud::setup_mini_map)
    uint8_t route_of(uint32_t maptiles)
    {
        for (int n = 0; n <= 30; n++)
            if (roms.rom0.read32(TILES_MINIMAP + (n << 2)) == maptiles)
                return (uint8_t)n;
        return 0xFF;
    }

    uint32_t bcd_to_dec(uint32_t bcd)
    {
        uint32_t v = 0, m = 1;
        for (int i = 0; i < 8; i++, bcd >>= 4, m *= 10)
            v += (bcd & 0xF) * m;
        return v;
    }

    // The time as the table shows it (OHiScore::convert_lap_time), in 1/100 s
    uint32_t shown_time(uint16_t time)
    {
        if (time == 0) return 0;
        const uint32_t minutes = time / 3600;
        const uint32_t rest    = time % 3600;
        const uint32_t seconds = rest >> 6;
        const uint8_t  ms      = ostats.lap_ms[rest & 0x3F];
        return minutes * 6000 + seconds * 100 + (ms >> 4) * 10 + (ms & 0xF);
    }

    void make_nonce(uint8_t nonce[NONCE_LEN])
    {
        static uint32_t counter = 0;
#ifdef __DREAMCAST__
        const uint32_t t = (uint32_t)time(NULL);
        const uint32_t r = (uint32_t)timer_us_gettime64() ^ (++counter * 0x9E3779B9u);
#else
        const uint32_t t = (uint32_t)time(NULL);
        const uint32_t r = (uint32_t)clock() ^ (++counter * 0x9E3779B9u);
#endif
        for (int i = 0; i < 4; i++) { nonce[i] = (uint8_t)(t >> (8 * i)); nonce[4 + i] = (uint8_t)(r >> (8 * i)); }
    }

    std::string base32(const uint8_t* p, size_t n)
    {
        static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        std::string out;
        uint32_t acc = 0;
        int bits = 0;
        for (size_t i = 0; i < n; i++)
        {
            acc = (acc << 8) | p[i];
            bits += 8;
            while (bits >= 5) { out += A[(acc >> (bits - 5)) & 31]; bits -= 5; }
        }
        if (bits) out += A[(acc << (5 - bits)) & 31];
        return out;
    }
}

// ---------------------------------------------------------------------------
// QRLeaderboard pass: the site address and key live in a small file on a
// memory card (QRLEADERBRD), made by the QRLeaderboard tool or setup disc
// (tools/qrleaderboard). Without it there is no SUBMIT SCORES and no QR code.
//
// File data: "QRLB" | version 1 | url length | url | 32-byte key |
//            CRC-32 (IEEE) of everything before it
// ---------------------------------------------------------------------------
namespace
{
    bool    pass_ok = false;
    uint8_t pass_key[32];

    uint32_t crc32(const uint8_t* p, size_t n)
    {
        uint32_t c = 0xFFFFFFFFu;
        while (n--)
        {
            c ^= *p++;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
        return ~c;
    }

    // Finds and checks the pass data in a file's bytes (the VMU file starts
    // with its package header and icon)
    bool parse_pass(const uint8_t* d, size_t n, std::string& url, uint8_t key[32])
    {
        for (size_t i = 0; i + 4 <= n; i++)
        {
            if (memcmp(d + i, "QRLB", 4) != 0) continue;
            const uint8_t* p = d + i;
            const size_t left = n - i;
            if (left < 6 || p[4] != 1) continue;
            const size_t len = p[5];
            if (len == 0 || 6 + len + 32 + 4 > left) continue;
            const size_t body = 6 + len + 32;
            const uint32_t want = p[body] | (p[body + 1] << 8) | (p[body + 2] << 16) | ((uint32_t)p[body + 3] << 24);
            if (crc32(p, body) != want) continue;
            url.assign((const char*)p + 6, len);
            memcpy(key, p + 6 + len, 32);
            return true;
        }
        return false;
    }

    // Host builds (tests): address from CB_LBTEST, key (hex) from CB_LBKEY
    bool hex_key(const std::string& h, uint8_t key[32])
    {
        if (h.size() != 64) return false;
        for (int i = 0; i < 32; i++)
        {
            int v = 0;
            for (int n = 0; n < 2; n++)
            {
                const char c = h[i * 2 + n];
                const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                              c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (d < 0) return false;
                v = v * 16 + d;
            }
            key[i] = (uint8_t)v;
        }
        return true;
    }
}

bool load_pass()
{
    pass_ok = false;
    config.leaderboard_url.clear();
#ifdef __DREAMCAST__
    // Every memory card slot: controllers A-D, sockets 1 and 2
    static uint8_t buf[4096];
    for (int port = 0; port < 4 && !pass_ok; port++)
        for (int unit = 1; unit <= 2 && !pass_ok; unit++)
        {
            char path[32];
            snprintf(path, sizeof(path), "/vmu/%c%d/QRLEADERBRD", 'a' + port, unit);
            file_t f = fs_open(path, O_RDONLY);
            if (f == FILEHND_INVALID) continue;
            const ssize_t n = fs_read(f, buf, sizeof(buf));
            fs_close(f);
            std::string url;
            if (n > 0 && parse_pass(buf, (size_t)n, url, pass_key))
            {
                config.leaderboard_url = url;
                pass_ok = true;
                dbglog(DBG_INFO, "cannonball: QRLeaderboard pass found on %c%d\n", 'A' + port, unit);
            }
        }
#else
    if (getenv("CB_LBTEST") && getenv("CB_LBKEY") && hex_key(getenv("CB_LBKEY"), pass_key))
    {
        config.leaderboard_url = getenv("CB_LBTEST");
        pass_ok = true;
    }
#endif
    return pass_ok;
}

bool enabled()
{
    return pass_ok && !config.leaderboard_url.empty();
}

std::string build_link()
{
    return build_link_for(table_index());
}

// Encrypts and signs a plaintext and makes the link (see leaderboard.hpp)
static std::string seal(const uint8_t* plain, size_t plain_len)
{
    // Keys
    uint8_t kenc[32], kmac[32];
    hmac(pass_key, (const uint8_t*)"enc", 3, NULL, 0, kenc);
    hmac(pass_key, (const uint8_t*)"mac", 3, NULL, 0, kmac);

    // Packet
    uint8_t pkt[1 + NONCE_LEN + 256 + MAC_LEN];
    if (plain_len > 256) return "";
    pkt[0] = VERSION;
    uint8_t* nonce = pkt + 1;
    make_nonce(nonce);
    uint8_t* ct = pkt + 1 + NONCE_LEN;
    for (size_t off = 0, block = 0; off < plain_len; off += 32, block++)
    {
        uint8_t ctr[4] = { (uint8_t)block, (uint8_t)(block >> 8), (uint8_t)(block >> 16), (uint8_t)(block >> 24) };
        uint8_t ks[32];
        hmac(kenc, nonce, NONCE_LEN, ctr, 4, ks);
        for (size_t i = 0; i < 32 && off + i < plain_len; i++)
            ct[off + i] = plain[off + i] ^ ks[i];
    }
    uint8_t mac[32];
    hmac(kmac, pkt, 1 + NONCE_LEN + plain_len, NULL, 0, mac);
    memcpy(ct + plain_len, mac, MAC_LEN);
    const size_t pkt_len = 1 + NONCE_LEN + plain_len + MAC_LEN;

    const std::string& url = config.leaderboard_url;
    const bool has_query = url.find('?') != std::string::npos;
    return url + (has_query ? "&d=" : "?d=") + base32(pkt, pkt_len);
}

std::string build_link_for(int table)
{
    if (!enabled()) return "";
    if (!ostats.lap_ms) ostats.init(false);     // lap time table (menu, before any game)

    // Plaintext
    uint8_t plain[3 + OHiScore::NO_SCORES * ENTRY_LEN];
    int count = 0;
    uint8_t* e = plain + 3;
    for (int i = 0; i < OHiScore::NO_SCORES; i++)
    {
        const score_entry& s = ohiscore.scores[i];
        if (s.score == 0 || is_default(s)) continue;
        const uint32_t pts = bcd_to_dec(s.score);
        const uint32_t cs  = shown_time(s.time);
        e[0] = (uint8_t)pts; e[1] = (uint8_t)(pts >> 8); e[2] = (uint8_t)(pts >> 16); e[3] = (uint8_t)(pts >> 24);
        e[4] = s.initial1; e[5] = s.initial2; e[6] = s.initial3;
        e[7] = route_of(s.maptiles);
        e[8] = (uint8_t)cs; e[9] = (uint8_t)(cs >> 8); e[10] = (uint8_t)(cs >> 16);
        e += ENTRY_LEN;
        count++;
    }
    if (count == 0) return "";
    plain[0] = (uint8_t)table;
    plain[1] = config.engine.score_scaling ? 1 : 0;
    plain[2] = (uint8_t)count;
    const size_t plain_len = 3 + count * ENTRY_LEN;

    return seal(plain, plain_len);
}

int make_qr(const std::string& link, uint8_t* modules)
{
    // The address as bytes, the data (base32: all QR alphanumeric
    // characters) in the denser alphanumeric mode
    const size_t split = link.rfind("d=");
    if (split == std::string::npos) return 0;
    const std::string head = link.substr(0, split + 2);
    const std::string data = link.substr(split + 2);

    static uint8_t buf0[qrcodegen_BUFFER_LEN_MAX], buf1[qrcodegen_BUFFER_LEN_MAX];
    static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    if (head.size() > sizeof(buf0) || data.size() > sizeof(buf1) * 2) return 0;

    qrcodegen_Segment segs[2];
    segs[0] = qrcodegen_makeBytes((const uint8_t*)head.data(), head.size(), buf0);
    segs[1] = qrcodegen_makeAlphanumeric(data.c_str(), buf1);
    if (!qrcodegen_encodeSegmentsAdvanced(segs, 2, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, 25,
                                          qrcodegen_Mask_AUTO, true, tmp, qr))
        return 0;

    const int size = qrcodegen_getSize(qr);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            modules[y * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
    return size;
}


// Menu SUBMIT SCORES state (functions at the end)
namespace
{
    struct MenuQR { std::string name; std::string link; };
    MenuQR   menu_qrs[24];
    int      menu_count = 0;
    int      menu_pos   = -1;       // -1 = closed
    int      menu_size  = 0;
    uint8_t  menu_modules[MAX_SIZE * MAX_SIZE];
    unsigned menu_serial = 0x80000000u;

    void menu_make()
    {
        menu_size = make_qr(menu_qrs[menu_pos].link, menu_modules);
        menu_serial++;
    }
}

// ---------------------------------------------------------------------------
// High score screen
// ---------------------------------------------------------------------------
namespace
{
    enum { OFFER_NONE, OFFER_NOTHING, OFFER_READY };
    int     offer   = OFFER_NONE;   // table checked for entries to send
    bool    visible = false;
    int     qr_size = 0;
    unsigned qr_serial = 0;
    uint8_t qr_modules[MAX_SIZE * MAX_SIZE];

    // Bottom text row, clear of the table
    const uint16_t HINT_X = 13, HINT_Y = 26;
    bool combo_prev = true;         // wait for a fresh press

    void draw_hint()
    {
        ohud.blit_text_new(HINT_X, HINT_Y, visible ? "ACCEL+BRAKE  HIDE QR " : "ACCEL+BRAKE  SCORE QR", OHud::GREEN);
    }
}

bool tick_hiscore(bool table_ready)
{
    if (!enabled() || !table_ready)
        return false;

    if (offer == OFFER_NONE)
    {
        const std::string link = build_link();
        qr_size = link.empty() ? 0 : make_qr(link, qr_modules);
        qr_serial++;
        offer = qr_size ? OFFER_READY : OFFER_NOTHING;
        if (offer == OFFER_READY)
        {
            draw_hint();
            // Leave time to see the hint (the screen may be about to close)
            if (ostats.time_counter < 0x10)
                ostats.time_counter = 0x10;
        }
    }
    if (offer != OFFER_READY)
        return false;

    // Keep the hint on screen (other code may redraw that text row)
    draw_hint();

    // Accelerate and brake together toggle the QR code
    bool combo = input.accel_held() && input.brake_held();
#ifdef DREAMCAST_DEBUG_QR
    // Test builds: show the QR code straight away
    static unsigned shown_serial = 0;
    if (shown_serial != qr_serial) { shown_serial = qr_serial; visible = false; combo = true; combo_prev = false; }
#endif
    const bool toggle = combo && !combo_prev;
    combo_prev = combo;
    if (toggle)
    {
        visible = !visible;
        draw_hint();
    }
    return visible;
}

void reset()
{
    offer = OFFER_NONE;
    visible = false;
    combo_prev = true;
}

unsigned overlay_serial()
{
    return menu_pos >= 0 ? menu_serial : qr_serial;
}

const uint8_t* overlay(int* size)
{
    if (menu_pos >= 0 && menu_size)
    {
        *size = menu_size;
        return menu_modules;
    }
    if (!visible || !qr_size) return NULL;
    *size = qr_size;
    return qr_modules;
}


// ---------------------------------------------------------------------------
// Menu: SUBMIT SCORES
// ---------------------------------------------------------------------------
int menu_open()
{
    menu_count = 0;
    menu_pos = -1;
    reset();
    if (!enabled()) return 0;

    // Every saved high score table with scores of our own
    for (int t = 0; t < 8; t++)
    {
        ohiscore.init_def_scores();
#ifdef __DREAMCAST__
        if (!vmu_load_scores(t)) continue;
#else
        if (t != table_index()) continue;   // host builds: the table in memory
#endif
        const std::string link = build_link_for(t);
        if (!link.empty())
        {
            std::string n = (t & 1) ? "JAPAN" : "WORLD";
            n += (t & 2) ? " CONTINUOUS" : " ARCADE";
            if (t & 4) n += " MODIFIED";
            menu_qrs[menu_count].name = n;
            menu_qrs[menu_count].link = link;
            menu_count++;
        }
    }

    // Time trial times: one or more pages per track set / modified
    // Plaintext: table 0x80 | Japan (1) | modified (4) | flags 0 | count |
    //            count x (route 0-15 | 3 initials | time u24 LE, 1/100 s)
    timeattack::load();
    for (int t = 0; t < 4; t++)
    {
        const int jap = t & 1, mod = (t >> 1) & 1;
        uint8_t plain[3 + TT_PAGE * TT_ENTRY_LEN];
        int count = 0, pages = 0;
        for (int r = 0; r <= timeattack::ROUTES; r++)
        {
            const timeattack::Entry* e = r < timeattack::ROUTES ? timeattack::table(jap, mod, r) : NULL;
            for (int k = 0; e && k < timeattack::RANKS; k++)
            {
                if (!e[k].cs) continue;
                uint8_t* p = plain + 3 + count * TT_ENTRY_LEN;
                p[0] = (uint8_t)r;
                memcpy(p + 1, e[k].init, 3);
                p[4] = (uint8_t)e[k].cs; p[5] = (uint8_t)(e[k].cs >> 8); p[6] = (uint8_t)(e[k].cs >> 16);
                count++;
            }
            // Page full, or the end: seal it
            if ((count >= TT_PAGE - timeattack::RANKS || (!e && count)) && menu_count < 24)
            {
                plain[0] = (uint8_t)(0x80 | jap | (mod << 2));
                plain[1] = 0;
                plain[2] = (uint8_t)count;
                const std::string link = seal(plain, 3 + count * TT_ENTRY_LEN);
                if (!link.empty())
                {
                    char n[48];
                    snprintf(n, sizeof(n), "TIME TRIAL %s%s%s", jap ? "JAPAN" : "WORLD", mod ? " MODIFIED" : "",
                             ++pages > 1 ? " +" : "");
                    menu_qrs[menu_count].name = n;
                    menu_qrs[menu_count].link = link;
                    menu_count++;
                }
                count = 0;
            }
        }
    }
    // The game reloads the right table when it starts
    ohiscore.init_def_scores();

    if (menu_count)
    {
        menu_pos = 0;
        menu_make();
    }
    return menu_count;
}

void menu_step(int dir)
{
    if (menu_pos < 0 || menu_count < 2) return;
    menu_pos = (menu_pos + dir + menu_count) % menu_count;
    menu_make();
}

void menu_close()
{
    menu_pos = -1;
}

int menu_tables()
{
    return menu_count;
}

std::string menu_table_name()
{
    if (menu_pos < 0) return "";
    return menu_qrs[menu_pos].name;
}

}
