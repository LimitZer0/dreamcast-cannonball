/***************************************************************************
    Dreamcast VMU Save/Load Helpers.

    Dreamcast-only fixed binary archive for arcade hi-scores and menu
    settings. All multi-byte fields use byte-wise little-endian helpers to
    avoid unaligned SH-4 accesses.

    Copyright Troy Davis.
***************************************************************************/

#include "vmu.hpp"

#ifdef __DREAMCAST__

#include <cstring>
#include <cstdio>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/fs_vmu.h>
#include <dc/vmufs.h>
#include <dc/vmu_pkg.h>
#include <cstdlib>
#include <kos/dbglog.h>
#include <kos/fs.h>

#include "engine/ohiscore.hpp"
#include "frontend/config.hpp"

#include "frontend/vmu_icon.h"

namespace
{
    static const char VMU_ARCHIVE_MAGIC[] = "CBVM";

    // Version 2: separate high score tables (see vmu.hpp for the list index)
    static const uint32_t VMU_ARCHIVE_VERSION = 2;
    static const uint32_t VMU_ARCHIVE_LISTS = VMU_SCORE_LISTS;
    static const uint32_t VMU_ARCHIVE_SCORE_COUNT = 20;
    static const uint32_t VMU_ARCHIVE_SCORE_SIZE = 13;
    static const uint32_t VMU_ARCHIVE_CONFIG_SIZE = 160;
    static const uint32_t VMU_ARCHIVE_HEADER_SIZE = 28;
    static const uint32_t VMU_ARCHIVE_LIST_SIZE = 1 + VMU_ARCHIVE_SCORE_COUNT * VMU_ARCHIVE_SCORE_SIZE; // used flag + entries
    static const uint32_t VMU_ARCHIVE_SCORE_OFFSET = VMU_ARCHIVE_HEADER_SIZE;
    static const uint32_t VMU_ARCHIVE_CONFIG_OFFSET =
        VMU_ARCHIVE_SCORE_OFFSET + (VMU_ARCHIVE_LISTS * VMU_ARCHIVE_LIST_SIZE);
    static const uint32_t VMU_ARCHIVE_SIZE =
        VMU_ARCHIVE_CONFIG_OFFSET + VMU_ARCHIVE_CONFIG_SIZE;

    // Version 1 layout (one shared table), read once and converted
    static const uint32_t V1_SCORE_COUNT = 20;
    static const uint32_t V1_SCORE_SIZE = 22;
    static const uint32_t V1_HEADER_SIZE = 24;
    static const uint32_t V1_CONFIG_OFFSET = V1_HEADER_SIZE + V1_SCORE_COUNT * V1_SCORE_SIZE;
    static const uint32_t V1_SIZE = V1_CONFIG_OFFSET + VMU_ARCHIVE_CONFIG_SIZE;

    static const char VMU_ARCHIVE_FILE[] = "CANNON";

    // Default button layout version (config byte after rainbow_unlocked)
    static const uint8_t PAD_LAYOUT = 1;
    static int pad_defaults[15];
    static const char port_chars[] = { 'a', 'b', 'c', 'd' };

    static uint16_t read_le16(const uint8_t* p)
    {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }

    static int16_t read_i16(const uint8_t* p)
    {
        return (int16_t)read_le16(p);
    }

    static uint32_t read_le32(const uint8_t* p)
    {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    static void write_le16(uint8_t* p, uint16_t v)
    {
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)((v >> 8) & 0xFF);
    }

    static void write_i16(uint8_t* p, int v)
    {
        write_le16(p, (uint16_t)(int16_t)v);
    }

    static void write_le32(uint8_t* p, uint32_t v)
    {
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)((v >> 8) & 0xFF);
        p[2] = (uint8_t)((v >> 16) & 0xFF);
        p[3] = (uint8_t)((v >> 24) & 0xFF);
    }

    static int find_vmu_port()
    {
        for (int i = 0; i < 4; i++)
        {
            if (maple_enum_type(i, MAPLE_FUNC_MEMCARD))
                return i;
        }
        return -1;
    }

    static const char* build_vmu_path(int port)
    {
        static char path[64];
        const char* p = port_chars + port;
        snprintf(path, sizeof(path), "/vmu/%c1/%s", *p, VMU_ARCHIVE_FILE);
        return path;
    }

    static bool check_header(const uint8_t* data, size_t len)
    {
        if (len != VMU_ARCHIVE_SIZE)
            return false;
        if (memcmp(data, VMU_ARCHIVE_MAGIC, 4) != 0)
            return false;
        if (read_le32(data + 4) != VMU_ARCHIVE_VERSION)
            return false;
        if (read_le32(data + 8) != VMU_ARCHIVE_SIZE)
            return false;
        if (read_le32(data + 12) != VMU_ARCHIVE_LISTS)
            return false;
        if (read_le32(data + 16) != VMU_ARCHIVE_SCORE_COUNT)
            return false;
        if (read_le32(data + 20) != VMU_ARCHIVE_SCORE_SIZE)
            return false;
        if (read_le32(data + 24) != VMU_ARCHIVE_CONFIG_SIZE)
            return false;
        return true;
    }

    static bool check_header_v1(const uint8_t* data, size_t len)
    {
        return len == V1_SIZE &&
               memcmp(data, VMU_ARCHIVE_MAGIC, 4) == 0 &&
               read_le32(data + 4) == 1 &&
               read_le32(data + 8) == V1_SIZE &&
               read_le32(data + 12) == V1_SCORE_COUNT &&
               read_le32(data + 16) == V1_SCORE_SIZE &&
               read_le32(data + 20) == VMU_ARCHIVE_CONFIG_SIZE;
    }

    static void pack_header(uint8_t* data)
    {
        memcpy(data, VMU_ARCHIVE_MAGIC, 4);
        write_le32(data + 4, VMU_ARCHIVE_VERSION);
        write_le32(data + 8, VMU_ARCHIVE_SIZE);
        write_le32(data + 12, VMU_ARCHIVE_LISTS);
        write_le32(data + 16, VMU_ARCHIVE_SCORE_COUNT);
        write_le32(data + 20, VMU_ARCHIVE_SCORE_SIZE);
        write_le32(data + 24, VMU_ARCHIVE_CONFIG_SIZE);
    }

    static void build_vmu_pkg(vmu_pkg_t* pkg, const uint8_t* data)
    {
        memset(pkg, 0, sizeof(*pkg));
        strcpy(pkg->desc_short, "Cannonball");
        strcpy(pkg->desc_long, "Cannonball Save Data");
        strcpy(pkg->app_id, "CANNONBALL");
        pkg->icon_cnt = 1;
        pkg->icon_anim_speed = 0;
        memcpy(pkg->icon_pal, VMU_ICON_PAL, sizeof(pkg->icon_pal));
        pkg->icon_data = (uint8_t*)VMU_ICON_DATA;
        pkg->eyecatch_type = VMUPKG_EC_NONE;
        pkg->data_len = VMU_ARCHIVE_SIZE;
        pkg->data = data;
    }

    static uint8_t* list_ptr(uint8_t* data, int list)
    {
        return data + VMU_ARCHIVE_SCORE_OFFSET + list * VMU_ARCHIVE_LIST_SIZE;
    }

    static void pack_scores(uint8_t* data, int list)
    {
        uint8_t* p = list_ptr(data, list);
        *p++ = 1;   // list in use
        for (uint32_t i = 0; i < VMU_ARCHIVE_SCORE_COUNT; i++)
        {
            const score_entry* s = &ohiscore.scores[i];
            write_le32(p + 0, s->score);
            p[4] = s->initial1;
            p[5] = s->initial2;
            p[6] = s->initial3;
            write_le32(p + 7, s->maptiles);
            write_le16(p + 11, s->time);
            p += VMU_ARCHIVE_SCORE_SIZE;
        }
    }

    // Returns false if this list has never been saved (the in-memory table
    // then keeps the default arcade scores)
    static bool apply_scores(uint8_t* data, int list)
    {
        const uint8_t* p = list_ptr(data, list);
        if (*p++ != 1)
            return false;
        for (uint32_t i = 0; i < VMU_ARCHIVE_SCORE_COUNT; i++)
        {
            score_entry* s = &ohiscore.scores[i];
            s->score    = read_le32(p + 0);
            s->initial1 = p[4];
            s->initial2 = p[5];
            s->initial3 = p[6];
            s->maptiles = read_le32(p + 7);
            s->time     = read_le16(p + 11);
            p += VMU_ARCHIVE_SCORE_SIZE;
        }
        return true;
    }

    // Convert a version 1 save (one shared table) into version 2. Its scores
    // were unscaled and mixed all modes, so they become the World / Arcade /
    // standard table, the one most people played.
    static void convert_v1(const uint8_t* v1, uint8_t* data)
    {
        memset(data, 0, VMU_ARCHIVE_SIZE);
        pack_header(data);
        uint8_t* dst = list_ptr(data, 0);
        *dst++ = 1;
        const uint8_t* src = v1 + V1_HEADER_SIZE;
        for (uint32_t i = 0; i < V1_SCORE_COUNT; i++)
        {
            memcpy(dst, src, VMU_ARCHIVE_SCORE_SIZE);   // same first 13 bytes
            dst += VMU_ARCHIVE_SCORE_SIZE;
            src += V1_SCORE_SIZE;
        }
        memcpy(data + VMU_ARCHIVE_CONFIG_OFFSET, v1 + V1_CONFIG_OFFSET, VMU_ARCHIVE_CONFIG_SIZE);
    }

    static void pack_config(uint8_t* data)
    {
        uint8_t* cfg = data + VMU_ARCHIVE_CONFIG_OFFSET;
        memset(cfg, 0, VMU_ARCHIVE_CONFIG_SIZE);
        uint8_t* p = cfg;

        *p++ = (uint8_t)config.video.mode;
        *p++ = (uint8_t)config.video.scale;
        *p++ = (uint8_t)config.video.scanlines;
        *p++ = (uint8_t)config.video.fps;
        *p++ = (uint8_t)config.video.fps_count;
        *p++ = (uint8_t)config.video.widescreen;
        *p++ = (uint8_t)config.video.hires;
        *p++ = (uint8_t)config.video.filtering;
        *p++ = (uint8_t)config.video.vsync;
        *p++ = (uint8_t)config.video.shadow;

        *p++ = config.sound.enabled ? 1 : 0;
        write_le32(p, (uint32_t)config.sound.rate); p += 4;
        *p++ = (uint8_t)config.sound.advertise;
        *p++ = (uint8_t)config.sound.preview;
        *p++ = (uint8_t)config.sound.fix_samples;
        write_i16(p, config.sound.music_timer); p += 2;

        *p++ = (uint8_t)config.smartypi.enabled;
        *p++ = (uint8_t)config.smartypi.ouputs;
        *p++ = (uint8_t)config.smartypi.cabinet;

        write_i16(p, config.controls.gear); p += 2;
        write_i16(p, config.controls.steer_speed); p += 2;
        write_i16(p, config.controls.pedal_speed); p += 2;
        *p++ = (uint8_t)(config.controls.rumble * 4.0f + 0.5f);
        for (int i = 0; i < 12; i++) { write_i16(p, config.controls.keyconfig[i]); p += 2; }
        for (int i = 0; i < 15; i++) { write_i16(p, config.controls.padconfig[i]); p += 2; }
        write_i16(p, config.controls.pad_id); p += 2;
        *p++ = (uint8_t)config.controls.analog;
        for (int i = 0; i < 4; i++) { write_i16(p, config.controls.axis[i]); p += 2; }
        for (int i = 0; i < 3; i++) *p++ = config.controls.invert[i] ? 1 : 0;
        for (int i = 0; i < 2; i++) { write_i16(p, config.controls.asettings[i]); p += 2; }
        *p++ = (uint8_t)config.controls.haptic;
        write_i16(p, config.controls.max_force); p += 2;
        write_i16(p, config.controls.min_force); p += 2;
        write_i16(p, config.controls.force_duration); p += 2;

        *p++ = (uint8_t)config.engine.dip_time;
        *p++ = (uint8_t)config.engine.dip_traffic;
        *p++ = config.engine.freeplay ? 1 : 0;
        *p++ = config.engine.freeze_timer ? 1 : 0;
        *p++ = config.engine.disable_traffic ? 1 : 0;
        *p++ = (uint8_t)config.engine.jap;
        *p++ = (uint8_t)config.engine.prototype;
        *p++ = (uint8_t)config.engine.randomgen;
        *p++ = (uint8_t)config.engine.level_objects;
        *p++ = config.engine.fix_bugs ? 1 : 0;
        *p++ = config.engine.fix_bugs_backup ? 1 : 0;
        *p++ = config.engine.fix_timer ? 1 : 0;
        *p++ = config.engine.layout_debug ? 1 : 0;
        *p++ = config.engine.hiscore_delete ? 1 : 0;
        write_i16(p, config.engine.hiscore_timer); p += 2;
        *p++ = (uint8_t)config.engine.new_attract;
        *p++ = config.engine.grippy_tyres ? 1 : 0;
        *p++ = config.engine.offroad ? 1 : 0;
        *p++ = config.engine.bumper ? 1 : 0;
        *p++ = config.engine.turbo ? 1 : 0;
        *p++ = (uint8_t)config.engine.car_pal;

        *p++ = (uint8_t)config.ttrial.laps;
        *p++ = (uint8_t)config.ttrial.traffic;
        *p++ = (uint8_t)config.cont_traffic;
        *p++ = (uint8_t)config.sound.custom_music;   // added later: old saves read 0
        *p++ = (uint8_t)config.engine.speed_mph;     // added later: old saves read 0
        *p++ = config.engine.rainbow_unlocked ? 1 : 0; // added later: old saves read 0
        *p++ = PAD_LAYOUT;                              // added later: old saves read 0
        p++;                                            // reserved (a test build's sprite option)
        *p++ = (uint8_t)config.video.crt;               // added later: old saves read 0
        *p++ = (uint8_t)config.video.mirror_badge;      // added later: old saves read 0
        *p++ = config.video.vmu_anim ? 0 : 1;           // added later: stored as "off" so old saves read on
    }

    static bool apply_config(const uint8_t* data)
    {
        const uint8_t* p = data + VMU_ARCHIVE_CONFIG_OFFSET;

        config.video.mode       = *p++;
        if (config.video.mode == video_settings_t::MODE_STRETCH)   // FULL removed: pixel perfect
            config.video.mode = video_settings_t::MODE_FULL;
        config.video.scale      = *p++;
        config.video.scanlines  = *p++;
        config.video.fps        = *p++;
        config.video.fps_count  = *p++;
        config.video.widescreen = *p++;
        config.video.hires      = *p++;
        config.video.filtering  = *p++;
        config.video.vsync      = *p++;
        config.video.shadow     = *p++;

        config.sound.enabled     = *p++ != 0;
        config.sound.rate        = (int)read_le32(p); p += 4;
        config.sound.advertise   = *p++;
        config.sound.preview     = *p++;
        config.sound.fix_samples = *p++;
        config.sound.music_timer = read_i16(p); p += 2;

        config.smartypi.enabled = *p++;
        config.smartypi.ouputs  = *p++;
        config.smartypi.cabinet = *p++;

        config.controls.gear = read_i16(p); p += 2;
        config.controls.steer_speed = read_i16(p); p += 2;
        config.controls.pedal_speed = read_i16(p); p += 2;
        config.controls.rumble = (*p++) * 0.25f;
        for (int i = 0; i < 12; i++) { config.controls.keyconfig[i] = read_i16(p); p += 2; }
        for (int i = 0; i < 15; i++) { config.controls.padconfig[i] = read_i16(p); p += 2; }
        config.controls.pad_id = read_i16(p); p += 2;
        config.controls.analog = *p++;
        for (int i = 0; i < 4; i++) { config.controls.axis[i] = read_i16(p); p += 2; }
        for (int i = 0; i < 3; i++) config.controls.invert[i] = *p++ != 0;
        for (int i = 0; i < 2; i++) { config.controls.asettings[i] = read_i16(p); p += 2; }
        config.controls.haptic = *p++;
        config.controls.max_force = read_i16(p); p += 2;
        config.controls.min_force = read_i16(p); p += 2;
        config.controls.force_duration = read_i16(p); p += 2;

        config.engine.dip_time        = *p++;
        config.engine.dip_traffic     = *p++;
        p++;                                    // free play: always on (byte kept for the save layout)
        config.engine.freeplay        = true;
        config.engine.freeze_timer    = *p++ != 0;
        config.engine.disable_traffic = *p++ != 0;
        config.engine.jap             = *p++;
        config.engine.prototype       = *p++;
        config.engine.randomgen       = *p++;
        config.engine.level_objects   = *p++;
        config.engine.fix_bugs        = *p++ != 0;
        config.engine.fix_bugs_backup = *p++ != 0;
        config.engine.fix_timer       = *p++ != 0;
        config.engine.layout_debug    = *p++ != 0;
        config.engine.hiscore_delete  = *p++ != 0;
        config.engine.hiscore_timer   = read_i16(p); p += 2;
        config.engine.new_attract     = *p++;
        config.engine.grippy_tyres    = *p++ != 0;
        config.engine.offroad         = *p++ != 0;
        config.engine.bumper          = *p++ != 0;
        config.engine.turbo           = *p++ != 0;
        config.engine.car_pal         = *p++;

        config.ttrial.laps    = *p++;
        config.ttrial.traffic = *p++;
        config.cont_traffic   = *p++;
        config.sound.custom_music = *p++;
        config.engine.speed_mph   = *p++;
        config.engine.rainbow_unlocked = *p++ != 0;
        if (config.engine.car_pal == 8 && !config.engine.rainbow_unlocked)
            config.engine.car_pal = 0;

        // Saves from before the current default button layout: take the
        // new defaults (config.xml) for the buttons
        if (*p++ < PAD_LAYOUT)
            memcpy(config.controls.padconfig, pad_defaults, sizeof(pad_defaults));
        p++;                                            // reserved
        config.video.crt = *p++;
        if (config.video.crt > 2) config.video.crt = 0;
        config.video.mirror_badge = *p++ != 0;
        config.video.vmu_anim = *p++ == 0;

        return true;
    }

    static void init_archive(uint8_t* data)
    {
        memset(data, 0, VMU_ARCHIVE_SIZE);   // all score lists unused
        pack_header(data);
        pack_config(data);
    }

    static bool read_archive(uint8_t* data)
    {
        int port = find_vmu_port();
        if (port < 0)
        {
            dbglog(DBG_INFO, "[VMU] No VMU card detected; skipping read\n");
            return false;
        }

        vmu_set_buttons_enabled(1);

        const char* vmu_path = build_vmu_path(port);
        dbglog(DBG_INFO, "[VMU] Open file %s\n", vmu_path);
        file_t fd = fs_open(vmu_path, O_RDONLY);
        if (fd == FILEHND_INVALID)
        {
            dbglog(DBG_INFO, "[VMU] Open file %s: FAIL\n", vmu_path);
            return false;
        }

        // The file holds the VMU package header (description, icon) first,
        // then our data, padded to whole 512-byte blocks. Depending on the
        // KOS version the header may or may not be returned by fs_read, so
        // read the whole file and look for the archive magic.
        static uint8_t raw[VMU_ARCHIVE_SIZE + 2048];
        const ssize_t n = fs_read(fd, raw, sizeof(raw));
        fs_close(fd);

        ssize_t start = -1;
        for (ssize_t off = 0; off + 4 <= n && off <= 1024; off += 32)
        {
            if (memcmp(raw + off, VMU_ARCHIVE_MAGIC, 4) == 0) { start = off; break; }
        }
        const ssize_t avail = start >= 0 ? n - start : 0;

        if (start >= 0 && avail >= (ssize_t)V1_SIZE && check_header_v1(raw + start, V1_SIZE))
        {
            convert_v1(raw + start, data);
            dbglog(DBG_INFO, "[VMU] Converted version 1 save\n");
            return true;
        }

        if (start < 0 || avail < (ssize_t)VMU_ARCHIVE_SIZE || !check_header(raw + start, VMU_ARCHIVE_SIZE))
        {
            dbglog(DBG_INFO, "[VMU] Read archive: FAIL (%zd bytes, data at %zd)\n", n, start);
            return false;
        }
        memcpy(data, raw + start, VMU_ARCHIVE_SIZE);

        dbglog(DBG_INFO, "[VMU] Open file %s: OK\n", vmu_path);
        return true;
    }

    static bool write_archive(const uint8_t* data)
    {
        int port = find_vmu_port();
        if (port < 0)
        {
            dbglog(DBG_INFO, "[VMU] No VMU card detected; skipping write\n");
            return false;
        }

        vmu_set_buttons_enabled(1);

        // Build the package (header, icon, data) ourselves and write it with
        // vmufs: fs_vmu would pad the data to whole blocks before adding the
        // header and icon, costing an extra block (7 instead of 6)
        maple_device_t* dev = maple_enum_type(port, MAPLE_FUNC_MEMCARD);
        if (!dev)
            return false;
        vmu_pkg_t pkg;
        build_vmu_pkg(&pkg, data);
        uint8_t* pkg_data = NULL;
        int pkg_size = 0;
        if (vmu_pkg_build(&pkg, &pkg_data, &pkg_size) < 0 || !pkg_data)
        {
            dbglog(DBG_INFO, "[VMU] Write archive: FAIL (package)\n");
            return false;
        }
        const int padded = (pkg_size + 511) & ~511;
        uint8_t* buf = (uint8_t*)calloc(1, padded);   // vmufs reads whole blocks
        if (!buf)
        {
            free(pkg_data);
            return false;
        }
        memcpy(buf, pkg_data, pkg_size);
        free(pkg_data);
        const int rv = vmufs_write(dev, VMU_ARCHIVE_FILE, buf, padded, VMUFS_OVERWRITE);
        free(buf);
        if (rv < 0)
        {
            dbglog(DBG_INFO, "[VMU] Write archive: FAIL (vmufs %d)\n", rv);
            return false;
        }
        dbglog(DBG_INFO, "[VMU] Wrote %s (%d blocks)\n", VMU_ARCHIVE_FILE, padded / 512);
        return true;
    }
}

bool vmu_load_scores(int list)
{
    static uint8_t buf[VMU_ARCHIVE_SIZE];
    if (list < 0 || list >= (int)VMU_ARCHIVE_LISTS || !read_archive(buf))
        return false;

    const bool used = apply_scores(buf, list);
    dbglog(DBG_INFO, "[VMU] Score table %d: %s\n", list, used ? "loaded" : "not saved yet");
    return used;
}

bool vmu_save_scores(int list)
{
    static uint8_t buf[VMU_ARCHIVE_SIZE];
    if (list < 0 || list >= (int)VMU_ARCHIVE_LISTS)
        return false;
    if (!read_archive(buf))
        init_archive(buf);
    pack_scores(buf, list);

    if (!write_archive(buf))
        return false;

    dbglog(DBG_INFO, "[VMU] Saved score table %d to CANNON (%u bytes)\n",
           list, (unsigned)VMU_ARCHIVE_SIZE);
    return true;
}

bool vmu_clear_scores()
{
    int port = find_vmu_port();
    if (port < 0)
    {
        dbglog(DBG_INFO, "[VMU] No VMU card detected; skipping clear\n");
        return false;
    }

    // Empty the score tables and keep the settings
    static uint8_t buf[VMU_ARCHIVE_SIZE];
    if (!read_archive(buf))
        return false;
    memset(buf + VMU_ARCHIVE_SCORE_OFFSET, 0, VMU_ARCHIVE_CONFIG_OFFSET - VMU_ARCHIVE_SCORE_OFFSET);
    dbglog(DBG_INFO, "[VMU] Clear score tables\n");
    return write_archive(buf);
}

bool vmu_load_config()
{
    // Called after config.xml is read: its buttons are the defaults
    memcpy(pad_defaults, config.controls.padconfig, sizeof(pad_defaults));

    static uint8_t buf[VMU_ARCHIVE_SIZE];
    if (!read_archive(buf))
        return false;

    apply_config(buf);
    dbglog(DBG_INFO, "[VMU] Loaded settings from CANNON (%u bytes)\n",
           (unsigned)VMU_ARCHIVE_SIZE);
    return true;
}

bool vmu_save_config()
{
    static uint8_t buf[VMU_ARCHIVE_SIZE];
    if (!read_archive(buf))
        init_archive(buf);
    else
        pack_config(buf);

    if (!write_archive(buf))
        return false;

    dbglog(DBG_INFO, "[VMU] Saved settings to CANNON (%u bytes)\n",
           (unsigned)VMU_ARCHIVE_SIZE);
    return true;
}

bool vmu_clear_config()
{
    // Settings back to config.xml; scores kept
    return vmu_save_config();
}

#endif // __DREAMCAST__
