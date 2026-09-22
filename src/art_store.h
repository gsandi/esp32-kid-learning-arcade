// art_store.h — memory-mapped bitmap store in the `art` data partition.
//
// The 4 MB app slot cannot hold the clock dials (870 KB) plus launcher icons plus
// room to grow, so bitmaps live in their own flash partition, baked by
// tools/bake_art.py and flashed at 0x950000 (see partitions.csv, flash.sh). The
// firmware maps the whole partition once at boot and hands out const pointers; a
// memcpy from a mapped pointer is exactly what reading a .rodata array was.
//
// Missing partition, bad magic or unknown name all return NULL. Callers must draw
// something sensible without art and say so once in the log; a wrong flash never
// becomes a black panel.
#pragma once
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ArtFormat : uint8_t {
    ART_RGB565   = 0,   // w*h*2 bytes
    ART_RGB565A8 = 1,   // w*h*2 colour plane followed by w*h alpha plane (LVGL 9 planar)
};

typedef struct {
    char     name[16];
    uint32_t offset;
    uint16_t w, h;
    uint8_t  format;
    uint8_t  pad[3];
    uint32_t size;
} ArtEntry;

// Map the partition. Returns false (and logs) if it is absent or unreadable.
bool art_init(void);

// Look up a blob by name. Pointer is into mapped flash; valid for the life of the device.
const ArtEntry* art_find(const char* name);
const uint8_t*  art_data(const ArtEntry* e);

// Convenience: fill an lv_image_dsc_t that points straight at the mapped blob.
// Returns false if the entry is missing.
bool art_image(const char* name, lv_image_dsc_t* out);

#ifdef __cplusplus
}
#endif
