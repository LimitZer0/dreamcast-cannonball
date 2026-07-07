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
#include <kos/dbglog.h>
#include <kos/fs.h>

#include "engine/ohiscore.hpp"
#include "frontend/config.hpp"

namespace
{
    static const char VMU_ARCHIVE_MAGIC[] = "CBVM";
    static const uint32_t VMU_ARCHIVE_VERSION = 1;
    static const uint32_t VMU_ARCHIVE_SCORE_COUNT = 20;
    static const uint32_t VMU_ARCHIVE_SCORE_SIZE = 22;
    static const uint32_t VMU_ARCHIVE_CONFIG_SIZE = 160;
    static const uint32_t VMU_ARCHIVE_HEADER_SIZE = 24;
    static const uint32_t VMU_ARCHIVE_SCORE_OFFSET = VMU_ARCHIVE_HEADER_SIZE;
    static const uint32_t VMU_ARCHIVE_CONFIG_OFFSET =
        VMU_ARCHIVE_SCORE_OFFSET + (VMU_ARCHIVE_SCORE_COUNT * VMU_ARCHIVE_SCORE_SIZE);
    static const uint32_t VMU_ARCHIVE_SIZE =
        VMU_ARCHIVE_CONFIG_OFFSET + VMU_ARCHIVE_CONFIG_SIZE;
    static const char VMU_ARCHIVE_FILE[] = "CANNON";
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
        if (read_le32(data + 12) != VMU_ARCHIVE_SCORE_COUNT)
            return false;
        if (read_le32(data + 16) != VMU_ARCHIVE_SCORE_SIZE)
            return false;
        if (read_le32(data + 20) != VMU_ARCHIVE_CONFIG_SIZE)
            return false;
        return true;
    }

    static void pack_header(uint8_t* data)
    {
        memcpy(data, VMU_ARCHIVE_MAGIC, 4);
        write_le32(data + 4, VMU_ARCHIVE_VERSION);
        write_le32(data + 8, VMU_ARCHIVE_SIZE);
        write_le32(data + 12, VMU_ARCHIVE_SCORE_COUNT);
        write_le32(data + 16, VMU_ARCHIVE_SCORE_SIZE);
        write_le32(data + 20, VMU_ARCHIVE_CONFIG_SIZE);
    }

    static void build_vmu_pkg(vmu_pkg_t* pkg, const uint8_t* data)
    {
        memset(pkg, 0, sizeof(*pkg));
        strcpy(pkg->desc_short, "Cannonball");
        strcpy(pkg->desc_long, "Cannonball Save Data");
        strcpy(pkg->app_id, "CANNONBALL");
        pkg->icon_cnt = 0;
        pkg->icon_anim_speed = 0;
        pkg->eyecatch_type = VMUPKG_EC_NONE;
        pkg->data_len = VMU_ARCHIVE_SIZE;
        pkg->data = data;
    }

    static void pack_scores(uint8_t* data)
    {
        uint8_t* p = data + VMU_ARCHIVE_SCORE_OFFSET;
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

    static bool apply_scores(const uint8_t* data)
    {
        const uint8_t* p = data + VMU_ARCHIVE_SCORE_OFFSET;
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
    }

    static bool apply_config(const uint8_t* data)
    {
        const uint8_t* p = data + VMU_ARCHIVE_CONFIG_OFFSET;

        config.video.mode       = *p++;
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
        config.engine.freeplay        = *p++ != 0;
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

        return true;
    }

    static void init_archive(uint8_t* data)
    {
        memset(data, 0, VMU_ARCHIVE_SIZE);
        pack_header(data);
        pack_scores(data);
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

        const ssize_t n = fs_read(fd, data, VMU_ARCHIVE_SIZE);
        fs_close(fd);

        if (n != (ssize_t)VMU_ARCHIVE_SIZE || !check_header(data, VMU_ARCHIVE_SIZE))
        {
            dbglog(DBG_INFO, "[VMU] Read archive: FAIL (%zd of %u)\n",
                   n, (unsigned)VMU_ARCHIVE_SIZE);
            return false;
        }

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

        const char* vmu_path = build_vmu_path(port);
        dbglog(DBG_INFO, "[VMU] Unlink file %s\n", vmu_path);
        fs_unlink(vmu_path);

        dbglog(DBG_INFO, "[VMU] Open file %s\n", vmu_path);
        file_t fd = fs_open(vmu_path, O_WRONLY | O_CREAT);
        if (fd == FILEHND_INVALID)
        {
            dbglog(DBG_INFO, "[VMU] Open file %s: FAIL\n", vmu_path);
            return false;
        }

        dbglog(DBG_INFO, "[VMU] Open file %s: OK\n", vmu_path);
        const ssize_t n = fs_write(fd, data, VMU_ARCHIVE_SIZE);
        vmu_pkg_t pkg;
        build_vmu_pkg(&pkg, data);
        const int header_ok = fs_vmu_set_header(fd, &pkg);
        const int close_ok = fs_close(fd);

        if (n != (ssize_t)VMU_ARCHIVE_SIZE)
        {
            dbglog(DBG_INFO, "[VMU] Write archive: FAIL (%zd of %u)\n",
                   n, (unsigned)VMU_ARCHIVE_SIZE);
            return false;
        }

        if (header_ok != 0 || close_ok != 0)
        {
            dbglog(DBG_INFO, "[VMU] Write archive: FAIL (header=%d close=%d)\n",
                   header_ok, close_ok);
            return false;
        }

        return true;
    }
}

bool vmu_load_scores()
{
    uint8_t buf[VMU_ARCHIVE_SIZE];
    if (!read_archive(buf))
        return false;

    apply_scores(buf);
    dbglog(DBG_INFO, "[VMU] Loaded 20 score entries from CANNON (%u bytes)\n",
           (unsigned)VMU_ARCHIVE_SIZE);
    return true;
}

bool vmu_save_scores()
{
    uint8_t buf[VMU_ARCHIVE_SIZE];
    if (!read_archive(buf))
        init_archive(buf);
    else
        pack_scores(buf);

    if (!write_archive(buf))
        return false;

    dbglog(DBG_INFO, "[VMU] Saved 20 score entries to CANNON (%u bytes)\n",
           (unsigned)VMU_ARCHIVE_SIZE);
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

    vmu_set_buttons_enabled(1);

    const char* vmu_path = build_vmu_path(port);
    dbglog(DBG_INFO, "[VMU] Unlink file %s\n", vmu_path);
    return fs_unlink(vmu_path) == 0;
}

bool vmu_load_config()
{
    uint8_t buf[VMU_ARCHIVE_SIZE];
    if (!read_archive(buf))
        return false;

    apply_config(buf);
    dbglog(DBG_INFO, "[VMU] Loaded settings from CANNON (%u bytes)\n",
           (unsigned)VMU_ARCHIVE_SIZE);
    return true;
}

bool vmu_save_config()
{
    uint8_t buf[VMU_ARCHIVE_SIZE];
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
    return vmu_clear_scores();
}

#endif // __DREAMCAST__
