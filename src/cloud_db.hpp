// cloud_db.hpp — Embedded Binary DB API
// vgmM5 / Layer812
//
// DB files are now embedded into the firmware flash (.rodata) via platformio.ini
#pragma once

#if defined(IS_CARDPUTER)

#include <Arduino.h>

// ============================================================
// Embedded DB Symbols (from platformio.ini board_build.embed_txtfiles)
// ============================================================
extern const uint8_t _binary_src_albums_bin_start[];
extern const uint8_t _binary_src_albums_bin_end[];

extern const uint8_t _binary_src_tracks_bin_start[];
extern const uint8_t _binary_src_tracks_bin_end[];

extern const uint8_t _binary_src_strings_bin_start[];
extern const uint8_t _binary_src_strings_bin_end[];

extern const uint8_t _binary_src_companies_bin_start[];
extern const uint8_t _binary_src_companies_bin_end[];

extern const uint8_t _binary_src_systems_bin_start[];
extern const uint8_t _binary_src_systems_bin_end[];

extern const uint8_t _binary_src_composers_bin_start[];
extern const uint8_t _binary_src_composers_bin_end[];

// ============================================================
// Struct Definitions
// ============================================================
#pragma pack(push, 1)

struct AlbumRecord {
    uint32_t chips_mask;
    uint16_t sys_id;
    uint16_t comp_id;
    uint16_t composer_id;
    uint32_t title_offset;
    uint32_t thumb_offset;
    uint32_t path_offset;
};

struct TrackRecord {
    uint16_t album_id;
    uint16_t duration_sec;
    uint32_t title_offset;
    uint32_t url_offset;
};

struct MasterRecord {
    uint16_t id;
    uint32_t name_offset;
};

#pragma pack(pop)

// ============================================================
// Chips Mask
// ============================================================
#define CHIP_YM2151     (1 << 0)
#define CHIP_YM2612     (1 << 1)
#define CHIP_SN76489    (1 << 2)
#define CHIP_C140       (1 << 3)
#define CHIP_C352       (1 << 4)
#define CHIP_AY38910    (1 << 5)
#define CHIP_YM2203     (1 << 6)
#define CHIP_OKIM6295   (1 << 7)
#define CHIP_HUC6280    (1 << 8)
#define CHIP_NES        (1 << 9)
#define CHIP_GB         (1 << 10)

#define VGMM5_SUPPORTED_MASK (CHIP_YM2151 | CHIP_YM2612 | CHIP_SN76489 | CHIP_C140 | CHIP_C352 | CHIP_AY38910 | CHIP_YM2203 | CHIP_OKIM6295 | CHIP_HUC6280)

// ============================================================
// State
// ============================================================
static uint32_t s_track_count   = 0;
static uint32_t s_album_count   = 0;
static uint32_t s_company_count = 0;
static uint32_t s_composer_count = 0;
static uint32_t s_system_count  = 0;
static bool     s_db_ready      = false;

// ============================================================
// Initialization
// ============================================================
bool cloud_db_init() {
    s_album_count = (_binary_src_albums_bin_end - _binary_src_albums_bin_start) / sizeof(AlbumRecord);
    s_track_count = (_binary_src_tracks_bin_end - _binary_src_tracks_bin_start) / sizeof(TrackRecord);
    s_company_count = (_binary_src_companies_bin_end - _binary_src_companies_bin_start) / sizeof(MasterRecord);
    s_composer_count = (_binary_src_composers_bin_end - _binary_src_composers_bin_start) / sizeof(MasterRecord);
    s_system_count = (_binary_src_systems_bin_end - _binary_src_systems_bin_start) / sizeof(MasterRecord);

    s_db_ready = true;
    Serial.printf("[CloudDB] Flash DB Ready. Albums: %u, Tracks: %u, Companies: %u, Systems: %u\n",
        (unsigned)s_album_count, (unsigned)s_track_count,
        (unsigned)s_company_count, (unsigned)s_system_count);
    return true;
}

bool cloud_db_is_ready()    { return s_db_ready; }
uint32_t cloud_db_track_count()  { return s_track_count; }
uint32_t cloud_db_album_count()  { return s_album_count; }

// ============================================================
// Accessors
// ============================================================
String cloud_db_get_string(uint32_t offset) {
    if (offset >= (uint32_t)(_binary_src_strings_bin_end - _binary_src_strings_bin_start)) return "";
    return String((const char*)(_binary_src_strings_bin_start + offset));
}

AlbumRecord cloud_db_get_album(uint16_t album_id) {
    if (!s_db_ready || album_id >= s_album_count) return {};
    const AlbumRecord* recs = (const AlbumRecord*)_binary_src_albums_bin_start;
    return recs[album_id];
}

TrackRecord cloud_db_get_track(uint32_t track_id) {
    if (!s_db_ready || track_id >= s_track_count) return {};
    const TrackRecord* recs = (const TrackRecord*)_binary_src_tracks_bin_start;
    return recs[track_id];
}

String cloud_db_get_track_path(uint32_t track_id) {
    TrackRecord trk = cloud_db_get_track(track_id);
    return cloud_db_get_string(trk.url_offset);
}

String cloud_db_get_full_url(uint32_t track_id) {
    TrackRecord trk = cloud_db_get_track(track_id);
    AlbumRecord alb = cloud_db_get_album(trk.album_id);
    String path = cloud_db_get_string(alb.path_offset);
    String file = cloud_db_get_string(trk.url_offset);
    if (path.length() == 0 || file.length() == 0) return "";
    String full = path + "/" + file;
    full.replace(" ", "%20");
    return "https://vgmrips.net/packs/vgm/" + full + ".vgz";
}

String cloud_db_get_thumb_url(uint32_t thumb_offset) {
    String path = cloud_db_get_string(thumb_offset);
    if (path.length() == 0) return "";
    path.replace(" ", "%20");
    path.replace("https://vgmrips.netlarge/", "https://vgmrips.net/files/");
    path.replace("https://vgmrips.net/large/", "https://vgmrips.net/files/");
    
    String url = path;
    if (!path.startsWith("http")) {
        url = "vgmrips.net/files/" + path;
    } else {
        url.replace("https://", "");
        url.replace("http://", "");
    }
    return "https://wsrv.nl/?url=" + url + "&w=64&h=64&output=jpg";
}

uint32_t calculateHash(const String& str) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < str.length(); i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619u;
    }
    return hash;
}

String cloud_db_get_album_title(uint16_t album_id) {
    AlbumRecord rec = cloud_db_get_album(album_id);
    return cloud_db_get_string(rec.title_offset);
}

String cloud_db_get_track_title(uint32_t track_id) {
    TrackRecord rec = cloud_db_get_track(track_id);
    return cloud_db_get_string(rec.title_offset);
}

String cloud_db_format_duration(uint16_t duration_sec) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u:%02u", duration_sec / 60, duration_sec % 60);
    return String(buf);
}

uint32_t cloud_db_company_count() { return s_company_count; }
uint32_t cloud_db_composer_count() { return s_composer_count; }
uint32_t cloud_db_system_count()  { return s_system_count; }

MasterRecord cloud_db_get_company(uint16_t index) {
    if (!s_db_ready || index >= s_company_count) return {};
    const MasterRecord* recs = (const MasterRecord*)_binary_src_companies_bin_start;
    return recs[index];
}

MasterRecord cloud_db_get_system(uint16_t index) {
    if (!s_db_ready || index >= s_system_count) return {};
    const MasterRecord* recs = (const MasterRecord*)_binary_src_systems_bin_start;
    return recs[index];
}

String cloud_db_get_company_name(uint16_t index) {
    return cloud_db_get_string(cloud_db_get_company(index).name_offset);
}

String cloud_db_get_system_name(uint16_t index) {
    return cloud_db_get_string(cloud_db_get_system(index).name_offset);
}

MasterRecord cloud_db_get_composer(uint16_t index) {
    if (!s_db_ready || index >= s_composer_count) return {};
    const MasterRecord* recs = (const MasterRecord*)_binary_src_composers_bin_start;
    return recs[index];
}

String cloud_db_get_composer_name(uint16_t index) {
    return cloud_db_get_string(cloud_db_get_composer(index).name_offset);
}

// ============================================================
// Filters
// ============================================================
uint16_t cloud_db_get_albums_all_supported(uint16_t* out_buf, uint16_t max) {
    uint16_t count = 0;
    const AlbumRecord* recs = (const AlbumRecord*)_binary_src_albums_bin_start;
    for (uint16_t i = 0; i < s_album_count && count < max; i++) {
        if ((recs[i].chips_mask & VGMM5_SUPPORTED_MASK) != 0) {
            out_buf[count++] = i;
        }
    }
    return count;
}

uint16_t cloud_db_get_albums_by_chip(uint32_t chip_mask, uint16_t* out_buf, uint16_t max) {
    uint16_t count = 0;
    const AlbumRecord* recs = (const AlbumRecord*)_binary_src_albums_bin_start;
    for (uint16_t i = 0; i < s_album_count && count < max; i++) {
        if (recs[i].chips_mask & chip_mask) {
            out_buf[count++] = i;
        }
    }
    return count;
}

uint16_t cloud_db_get_albums_by_composer(uint16_t composer_id, uint16_t* out_buf, uint16_t max) {
    uint16_t count = 0;
    const AlbumRecord* recs = (const AlbumRecord*)_binary_src_albums_bin_start;
    for (uint16_t i = 0; i < s_album_count && count < max; i++) {
        if (recs[i].composer_id == composer_id) {
            out_buf[count++] = i;
        }
    }
    return count;
}

uint16_t cloud_db_get_albums_by_company(uint16_t company_id, uint16_t* out_buf, uint16_t max) {
    uint16_t count = 0;
    const AlbumRecord* recs = (const AlbumRecord*)_binary_src_albums_bin_start;
    for (uint16_t i = 0; i < s_album_count && count < max; i++) {
        if (recs[i].comp_id == company_id) {
            out_buf[count++] = i;
        }
    }
    return count;
}

uint16_t cloud_db_get_tracks_by_album(uint16_t album_id, uint32_t* out_buf, uint16_t max) {
    uint16_t count = 0;
    const TrackRecord* recs = (const TrackRecord*)_binary_src_tracks_bin_start;
    bool found = false;
    for (uint32_t i = 0; i < s_track_count && count < max; i++) {
        if (recs[i].album_id == album_id) {
            found = true;
            out_buf[count++] = i;
        } else if (found) {
            break;
        }
    }
    return count;
}

#endif // IS_CARDPUTER