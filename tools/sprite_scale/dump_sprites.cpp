// Decode the sprites of a host sprite dump (CB_SPRDUMP, see main.cpp) into
// raw images for offline scaling experiments.
//   dump_sprites sprdata.bin spr_<frame>.bin outdir
// Writes outdir/<key>.raw (w, h as uint16, then w*h pixel indices 0-15),
// outdir/<key>.pal (16 x RGB888) and prints one line per sprite:
//   key shadow pal hzoom vzoom xpos top height xdelta ydelta img_w rows
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <set>
#include <string>
#include "../../src/main/dreamcast/pvr_sprite_core.hpp"
using namespace pvrspr;

static std::vector<uint8_t> slurp(const char* f)
{
    std::vector<uint8_t> v;
    FILE* fp = fopen(f, "rb");
    if (!fp) { perror(f); exit(1); }
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) v.insert(v.end(), buf, buf + n);
    fclose(fp);
    return v;
}

static uint32_t rgb(uint16_t a)
{
    uint32_t r = (a & 0x000f) << 1, g = (a & 0x00f0) >> 3, b = (a & 0x0f00) >> 7;
    if (a & 0x1000) r |= 1;
    if (a & 0x2000) g |= 1;
    if (a & 0x4000) b |= 1;
    return ((r * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (b * 255 / 31);
}

int main(int argc, char** argv)
{
    if (argc < 4) return 1;
    std::vector<uint8_t> data = slurp(argv[1]);
    std::vector<uint8_t> dump = slurp(argv[2]);
    const uint16_t* ram = (const uint16_t*)dump.data();
    const uint16_t* pal = ram + 1024;
    const int banks = (int)(data.size() / 4 / 0x10000);
    std::set<uint32_t> done;
    for (int pri = 0; pri < 4; pri++)
    {
        SprCmd cmds[128];
        const int n = extract_sprites(ram, 1024, (uint8_t)(1 << pri), 0, banks, cmds, 128);
        for (int i = 0; i < n; i++)
        {
            const SprCmd& s = cmds[i];
            const uint32_t* bank = (const uint32_t*)data.data() + s.bank * 0x10000;
            const int rows = rows_needed(s);
            const int w = measure_sprite(bank, s.addr, s.pitch, s.flip, rows);
            std::vector<uint8_t> img((size_t)w * rows);
            decode_sprite(bank, s.addr, s.pitch, s.flip, rows, img.data(), w);
            const uint32_t key = image_key(s);
            printf("%08x %d %d %d %d %d %d %d %d %d %d %d\n", key, s.shadow, s.pal, s.hzoom, s.vzoom, s.xpos, s.top,
                   s.height, s.xdelta, s.ydelta, w, rows);
            char name[256];
            snprintf(name, sizeof name, "%s/%08x_%d.raw", argv[3], key, rows);
            FILE* f = fopen(name, "wb");
            uint16_t hdr[2] = { (uint16_t)w, (uint16_t)rows };
            fwrite(hdr, 2, 2, f); fwrite(img.data(), 1, img.size(), f); fclose(f);
            snprintf(name, sizeof name, "%s/pal_%d.pal", argv[3], s.pal);
            f = fopen(name, "wb");
            for (int c = 0; c < 16; c++)
            {
                const uint32_t v = rgb(pal[0x800 + s.pal * 16 + c]);
                uint8_t b3[3] = { (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
                fwrite(b3, 1, 3, f);
            }
            fclose(f);
        }
    }
    return 0;
}
