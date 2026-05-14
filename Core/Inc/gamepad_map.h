#ifndef GAMEPAD_MAP_H
#define GAMEPAD_MAP_H

#include <stdint.h>

/*
 * Source bit indices into the combined 16-bit USB-button word
 *   w = ((extraBtn) << 8) | data
 *
 * data bits 0..3 are D-pad (RIGHT, LEFT, DOWN, UP) and are not remappable;
 * everything else (data bits 4..7, extraBtn bits 0..7) can be assigned
 * to any logical Amiga output.  Value 0xFF means "unmapped" (always 0).
 */

#define GAMEPAD_MAP_ENTRIES 10
#define GAMEPAD_MAP_UNMAPPED 0xFFu

enum {
    GMAP_FIRE1 = 0,
    GMAP_FIRE2,
    GMAP_FIRE3,
    GMAP_CD32_PLAY,
    GMAP_CD32_RW,
    GMAP_CD32_FF,
    GMAP_CD32_GREEN,
    GMAP_CD32_YELLOW,
    GMAP_CD32_RED,
    GMAP_CD32_BLUE,
};

typedef struct {
    uint8_t src[GAMEPAD_MAP_ENTRIES];
} gamepad_map_t;

extern gamepad_map_t gamepad1_map;
extern gamepad_map_t gamepad2_map;

void gamepad_map_load(void);
void gamepad_map_save(void);

static inline uint16_t gp_combine(uint8_t data, uint8_t extra)
{
    return (uint16_t)data | ((uint16_t)extra << 8);
}

static inline uint8_t gp_btn(uint16_t w, uint8_t src)
{
    return (src < 16u) ? (uint8_t)((w >> src) & 1u) : 0u;
}

#endif
