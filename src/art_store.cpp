// art_store.cpp — see art_store.h. Format is produced by tools/bake_art.py.
#include "art_store.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "art";

#define ART_MAGIC 0x5452414Bu   // 'KART' little-endian

struct ArtHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
};

static const uint8_t*   s_base    = nullptr;
static const ArtHeader* s_hdr     = nullptr;
static const ArtEntry*  s_entries = nullptr;
static size_t           s_size    = 0;

bool art_init(void) {
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                           (esp_partition_subtype_t)0x40, "art");
    if (!part) {
        ESP_LOGE(TAG, "no 'art' partition in the table - dials and icons will be missing");
        return false;
    }
    const void* map = nullptr;
    esp_partition_mmap_handle_t h;
    esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &map, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mmap failed: %s", esp_err_to_name(err));
        return false;
    }
    const ArtHeader* hdr = (const ArtHeader*)map;
    if (hdr->magic != ART_MAGIC) {
        ESP_LOGE(TAG, "art partition not flashed (magic 0x%08lx) - run tools/bake_art.py and flash build/art.bin at 0x%08lx",
                 (unsigned long)hdr->magic, (unsigned long)part->address);
        return false;
    }
    if (hdr->count > 256) {
        ESP_LOGE(TAG, "art header count %lu implausible, ignoring partition", (unsigned long)hdr->count);
        return false;
    }
    s_base    = (const uint8_t*)map;
    s_hdr     = hdr;
    s_entries = (const ArtEntry*)(s_base + sizeof(ArtHeader));
    s_size    = part->size;
    ESP_LOGI(TAG, "art partition mapped: %lu entries, v%lu", (unsigned long)hdr->count, (unsigned long)hdr->version);
    for (uint32_t i = 0; i < hdr->count; ++i)
        ESP_LOGI(TAG, "  %-15s %ux%u fmt=%u %lu B", s_entries[i].name, s_entries[i].w, s_entries[i].h,
                 s_entries[i].format, (unsigned long)s_entries[i].size);
    return true;
}

const ArtEntry* art_find(const char* name) {
    if (!s_hdr) return nullptr;
    for (uint32_t i = 0; i < s_hdr->count; ++i) {
        if (strncmp(s_entries[i].name, name, sizeof(s_entries[i].name)) == 0) {
            const ArtEntry* e = &s_entries[i];
            if ((size_t)e->offset + e->size > s_size) return nullptr;   // truncated bake
            return e;
        }
    }
    return nullptr;
}

const uint8_t* art_data(const ArtEntry* e) {
    return (e && s_base) ? s_base + e->offset : nullptr;
}

bool art_image(const char* name, lv_image_dsc_t* out) {
    const ArtEntry* e = art_find(name);
    if (!e) return false;
    memset(out, 0, sizeof(*out));
    out->header.magic  = LV_IMAGE_HEADER_MAGIC;
    out->header.cf     = (e->format == ART_RGB565A8) ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
    out->header.w      = e->w;
    out->header.h      = e->h;
    out->header.stride = e->w * 2;
    out->data_size     = e->size;
    out->data          = art_data(e);
    return true;
}
