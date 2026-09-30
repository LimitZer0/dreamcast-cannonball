// Decode one sprite image by key (see pvrspr::image_key) with a given row
// count: decode_key sprdata.bin <key hex> <rows> out.raw
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include "../../src/main/dreamcast/pvr_sprite_core.hpp"
using namespace pvrspr;
int main(int argc, char** argv)
{
    if (argc < 5) return 1;
    FILE* f = fopen(argv[1], "rb");
    std::vector<uint32_t> data(0x40000);
    fread(data.data(), 4, data.size(), f); fclose(f);
    const uint32_t key = strtoul(argv[2], 0, 16);
    const uint16_t addr = key & 0xffff;
    const int bank = (key >> 16) & 0xf, flip = (key >> 20) & 1;
    int16_t pitch = (key >> 21) & 0x7ff; if (pitch & 0x400) pitch -= 0x800;
    const int rows = atoi(argv[3]);
    const uint32_t* b = data.data() + bank * 0x10000;
    const int w = measure_sprite(b, addr, pitch, flip, rows);
    std::vector<uint8_t> img((size_t)w * rows);
    decode_sprite(b, addr, pitch, flip, rows, img.data(), w);
    f = fopen(argv[4], "wb");
    uint16_t hdr[2] = { (uint16_t)w, (uint16_t)rows };
    fwrite(hdr, 2, 2, f); fwrite(img.data(), 1, img.size(), f); fclose(f);
    return 0;
}
