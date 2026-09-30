/*
 * QRLeaderboard setup disc (Dreamcast, KallistiOS).
 *
 * Writes a QRLeaderboard pass (file QRLEADERBRD) to a memory card. Games that
 * support QRLeaderboard find it there and offer score submission (a QR code
 * that opens the leaderboard site). The pass data comes from PASS.BIN on this
 * disc, which qrleaderboard.html fills in. The pass (as saved to the card):
 *   "QRLB" | version 1 | url length | url | 32-byte key | CRC-32 (IEEE)
 * On the disc it is scrambled so the key isn't sitting in the open:
 *   "QRLX" | 4-byte nonce | 504 bytes of pass XOR a keystream (see unscramble)
 * An unfilled disc has "QRLB", version 0 there. The key is never shown.
 *
 * Background: a waving chequered flag, drawn in RAM and copied to a
 * second framebuffer each frame.
 */
#include <kos.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dc/biosfont.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/vmu_pkg.h>
#include <dc/vmufs.h>
#include <dc/fmath.h>
#include <dc/sq.h>
#include "qr_icon.h"

#define W 640
#define H 480
static uint8_t pass[512];
static int     pass_len = 0;
static char    pass_url[256];

// Text colours (RGB565: the screen is 16-bit)
static const uint32_t COL_TEXT = 0xFFFF, COL_DIM = 0x94B6, COL_OK = 0x5712,
                      COL_ERR = 0xFB10, COL_HI = 0xFE88;

static uint32_t crc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}

// ---------------------------------------------------------------------------
// Waving chequered flag
// ---------------------------------------------------------------------------
static uint16_t screen[W * H] __attribute__((aligned(32)));   // drawn here, then copied to VRAM

#define CELL_SHIFT 5                    // 32-pixel squares
#define SHADES 16
static uint16_t flag_pal[2][SHADES];    // [dark/light square][fold shading]
static int16_t  col_off[W];             // this frame's vertical wave per column
static uint8_t  col_shade[W];
static uint8_t  col_sq[W];              // column's square (0/1 parity)

static uint16_t rgb(int r, int g, int b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void flag_init(void)
{
    for (int s = 0; s < SHADES; s++)
    {
        const int k = 60 + s * 40 / (SHADES - 1);        // 60%..100% light
        flag_pal[0][s] = rgb(24 * k / 100, 24 * k / 100, 32 * k / 100);
        flag_pal[1][s] = rgb(170 * k / 100, 172 * k / 100, 184 * k / 100);
    }
}

static void flag_draw(int t)
{
    // The wave runs left to right and grows away from the (off-screen) pole
    for (int x = 0; x < W; x++)
    {
        const float ph = x * 0.018f - t * 0.06f;
        const float amp = 6.0f + 22.0f * x / W;
        const float sx = x + 4.0f * fsin(ph * 0.5f);      // a little sideways sway
        col_off[x]   = (int16_t)(amp * fsin(ph));
        col_shade[x] = (uint8_t)((fcos(ph) * 0.5f + 0.5f) * (SHADES - 1) + 0.5f);
        col_sq[x]    = ((int)(sx + 64) >> CELL_SHIFT) & 1;
    }
    uint16_t* p = screen;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            const int v = y + 64 - col_off[x];
            *p++ = flag_pal[((v >> CELL_SHIFT) ^ col_sq[x]) & 1][col_shade[x]];
        }
}

// Darken a box to 3/8 so the text reads over the flag
static void shade_box(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; y++)
    {
        uint16_t* p = screen + y * W;
        for (int x = x0; x < x1; x++) p[x] = ((p[x] >> 2) & 0x39E7) + ((p[x] >> 3) & 0x18E3);
    }
}

static void present(void)
{
    vid_waitvbl();
    sq_cpy(vram_s, screen, sizeof(screen));
}

static void text(int x, int y, uint32_t col, const char* s)
{
    bfont_set_foreground_color(col);
    bfont_draw_str(screen + y * W + x, W, 0, s);
}

// Long addresses: wrap at 48 characters per line
static int text_wrapped(int x, int y, uint32_t col, const char* s)
{
    char line[49];
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i += 48)
    {
        size_t k = n - i < 48 ? n - i : 48;
        memcpy(line, s + i, k); line[k] = 0;
        text(x, y, col, line);
        y += 26;
    }
    return y;
}

// PASS.BIN is scrambled on the disc: XOR with an xorshift32 keystream seeded
// from the nonce (the same as fillSetupDisc in qrleaderboard.src.html)
static void unscramble(const uint8_t* in, int n)
{
    uint32_t s = 0x9E3779B9u ^ (in[4] | (in[5] << 8) | (in[6] << 16) | ((uint32_t)in[7] << 24));
    if (!s) s = 1;
    memset(pass, 0, sizeof(pass));
    for (int i = 8; i < n && i - 8 < (int)sizeof(pass); i++)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        pass[i - 8] = in[i] ^ (uint8_t)(s >> 24);
    }
}

static int load_pass(void)
{
    uint8_t raw[512];
    file_t f = fs_open("/cd/PASS.BIN", O_RDONLY);
    if (f == FILEHND_INVALID) return -1;
    int n = fs_read(f, raw, sizeof(raw));
    fs_close(f);
    if (n >= 12 && !memcmp(raw, "QRLX", 4))
    {
        unscramble(raw, n);
        n -= 8;
    }
    else
        memcpy(pass, raw, n > 0 ? n : 0);
    if (n < 12 || memcmp(pass, "QRLB", 4) != 0) return -1;
    if (pass[4] != 1) return 0;                     // tool hasn't filled it in
    int len = pass[5];
    int body = 6 + len + 32;
    if (len == 0 || body + 4 > n) return -1;
    uint32_t want = pass[body] | (pass[body + 1] << 8) | (pass[body + 2] << 16) | ((uint32_t)pass[body + 3] << 24);
    if (crc32(pass, body) != want) return -1;
    memcpy(pass_url, pass + 6, len); pass_url[len] = 0;
    pass_len = body + 4;
    return 1;
}

static int write_pass(maple_device_t* dev)
{
    vmu_pkg_t pkg;
    memset(&pkg, 0, sizeof(pkg));
    strcpy(pkg.desc_short, "QRLeaderboard");
    strcpy(pkg.desc_long, "QRLeaderboard pass");
    strcpy(pkg.app_id, "QRLEADERBOARD");
    pkg.icon_cnt = 1;
    memcpy(pkg.icon_pal, QR_ICON_PAL, sizeof(pkg.icon_pal));
    pkg.icon_data = (uint8_t*)QR_ICON_DATA;
    pkg.eyecatch_type = VMUPKG_EC_NONE;
    pkg.data_len = pass_len;
    pkg.data = pass;

    uint8_t* out = NULL;
    int size = 0;
    if (vmu_pkg_build(&pkg, &out, &size) < 0 || !out) return -1;
    int padded = (size + 511) & ~511;
    uint8_t* buf = calloc(1, padded);
    if (!buf) { free(out); return -1; }
    memcpy(buf, out, size);
    free(out);
    int rv = vmufs_write(dev, "QRLEADERBRD", buf, padded, VMUFS_OVERWRITE);
    free(buf);
    return rv;
}

typedef struct { maple_device_t* dev; char name[4]; int free_blocks; int has_pass; } card_t;

static int find_cards(card_t* cards)
{
    int n = 0;
    for (int port = 0; port < 4; port++)
        for (int unit = 1; unit <= 2; unit++)
        {
            maple_device_t* dev = maple_enum_dev(port, unit);
            if (!dev || !(dev->info.functions & MAPLE_FUNC_MEMCARD)) continue;
            card_t* c = &cards[n++];
            c->dev = dev;
            snprintf(c->name, sizeof(c->name), "%c%d", 'A' + port, unit);
            c->free_blocks = vmufs_free_blocks(dev);
            vmu_dir_t* dir = NULL; int cnt = 0;
            c->has_pass = 0;
            if (vmufs_readdir(dev, &dir, &cnt) >= 0 && dir)
            {
                for (int i = 0; i < cnt; i++)
                    if (!strncmp(dir[i].filename, "QRLEADERBRD", 11)) c->has_pass = 1;
                free(dir);
            }
        }
    return n;
}

int main(int argc, char** argv)
{
    vid_set_mode(DM_640x480, PM_RGB565);
    flag_init();
    const int state = load_pass();
    card_t cards[8];
    int ncards = 0, rescan = 0, t = 0;

    int sel = 0, last_buttons = 0;
    char msg[96] = "";
    uint32_t msg_col = COL_DIM;
#ifdef SETUP_AUTO
    int auto_frames = 0;
#endif

    for (;;)
    {
        // Memory cards: look again twice a second (reading a card's
        // directory is slow) and after saving
        if (rescan-- <= 0) { ncards = find_cards(cards); rescan = 30; }
        if (sel >= ncards) sel = ncards ? ncards - 1 : 0;

        flag_draw(t++);
        shade_box(24, 20, W - 24, H - 20);
        text(40, 30, COL_HI, "QRLEADERBOARD SETUP");
        text(40, 60, COL_DIM, "Online score submission for supported games");

        int y = 110;
        if (state < 0)
        {
            text(40, y, COL_ERR, "This disc's pass data is missing or damaged.");
            text(40, y + 30, COL_DIM, "Make it again with qrleaderboard.html.");
        }
        else if (state == 0)
        {
            text(40, y, COL_ERR, "This setup disc hasn't been filled in yet.");
            text(40, y + 30, COL_DIM, "Make one with qrleaderboard.html.");
        }
        else
        {
            text(40, y, COL_DIM, "Scores will be submitted to:");
            y = text_wrapped(40, y + 30, COL_TEXT, pass_url) + 20;

            if (!ncards)
                text(40, y, COL_ERR, "No memory card found. Insert one.");
            else
            {
                text(40, y, COL_DIM, "Memory card (up/down to choose):");
                y += 30;
                for (int i = 0; i < ncards; i++)
                {
                    char line[80];
                    snprintf(line, sizeof(line), "%s %s   %3d blocks free%s", i == sel ? ">" : " ",
                             cards[i].name, cards[i].free_blocks, cards[i].has_pass ? "   (has a pass)" : "");
                    text(40, y, i == sel ? COL_TEXT : COL_DIM, line);
                    y += 26;
                }
                text(40, y + 14, COL_HI, "Press A to save the pass (2 blocks)");
            }
        }
        if (msg[0]) text(40, 420, msg_col, msg);

        // Controls
        int buttons = 0;
        maple_device_t* pad = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if (pad)
        {
            cont_state_t* st = (cont_state_t*)maple_dev_status(pad);
            if (st) buttons = st->buttons;
        }
        const int pressed = buttons & ~last_buttons;
        last_buttons = buttons;
#ifdef SETUP_AUTO
        if (++auto_frames == 120) { }
        const int do_write = state == 1 && ncards && auto_frames == 120;
#else
        const int do_write = state == 1 && ncards && (pressed & CONT_A);
#endif
        if (pressed & CONT_DPAD_UP) sel = sel > 0 ? sel - 1 : 0;
        if (pressed & CONT_DPAD_DOWN && sel + 1 < ncards) sel++;
        if (do_write)
        {
            if (cards[sel].free_blocks < 2 && !cards[sel].has_pass)
            {
                snprintf(msg, sizeof(msg), "Not enough space on %s: 2 blocks needed.", cards[sel].name);
                msg_col = COL_ERR;
            }
            else if (write_pass(cards[sel].dev) < 0)
            {
                snprintf(msg, sizeof(msg), "Couldn't write to %s.", cards[sel].name);
                msg_col = COL_ERR;
            }
            else
            {
                snprintf(msg, sizeof(msg), "Saved to %s. You can switch off and play.", cards[sel].name);
                msg_col = COL_OK;
                dbglog(DBG_INFO, "setup: pass written to %s\n", cards[sel].name);
            }
            rescan = 0;
        }
        present();
    }
    return 0;
}
