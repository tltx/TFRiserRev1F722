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

/*
 * Per-device profile.  Mappings are keyed by VID:PID so the same physical
 * pad gets the same button layout regardless of which Riser USB jack it's
 * in.  An "empty" slot has vid==0 (real devices always have a non-zero VID).
 */
#define GAMEPAD_PROFILE_SLOTS 4

typedef struct {
    uint16_t       vid;
    uint16_t       pid;
    gamepad_map_t  map;
} gamepad_profile_t;

/* Active in-RAM mappings used by the runtime button decoder. */
extern gamepad_map_t gamepad1_map;
extern gamepad_map_t gamepad2_map;

/* Currently-active pad identity per port; 0/0 means "no pad" or
 * "pad uses the in-RAM default and has no persistent profile". */
extern uint16_t gamepad1_vid, gamepad1_pid;
extern uint16_t gamepad2_vid, gamepad2_pid;

/* Profile storage (mirror of RTC backup).  Slot 0 is MRU. */
extern gamepad_profile_t gamepad_profiles[GAMEPAD_PROFILE_SLOTS];

/*
 * Load profiles from RTC backup at boot.  If magic doesn't match, reset
 * to empty slots and load gamepad1/2_map with the built-in default.
 */
void gamepad_map_load(void);

/*
 * Activate a pad on the given Amiga slot (port 1 = HS, port 2 = FS).
 * Looks up VID:PID in profile storage; if found, copies that profile's
 * mapping into the active map.  If not found, copies the built-in
 * default mapping -- no profile is created until the user actually
 * edits a slot via gamepad_map_save_active().
 *
 * vid==0 / pid==0 means "no pad detected" -- the active map is left
 * untouched and the per-port vid/pid trackers are cleared.
 */
void gamepad_map_activate(uint8_t port, uint16_t vid, uint16_t pid);

/*
 * Persist the currently-active map for the given port back to RTC
 * backup.  If the port's pad already has a profile, that slot is
 * updated.  If not (the pad was using the in-RAM default), a new
 * profile slot is allocated -- MRU at slot 0, others shift down,
 * oldest (slot 3) falls off.
 *
 * No-op if the port has no active pad (vid==0).  Called by the EXTI
 * write handler whenever a mapping byte ($20..$33) is modified.
 */
void gamepad_map_save_active(uint8_t port);

static inline uint16_t gp_combine(uint8_t data, uint8_t extra)
{
    return (uint16_t)data | ((uint16_t)extra << 8);
}

static inline uint8_t gp_btn(uint16_t w, uint8_t src)
{
    return (src < 16u) ? (uint8_t)((w >> src) & 1u) : 0u;
}

#endif
