#include "gamepad_map.h"
#include "stm32f7xx_hal.h"
#include <string.h>

extern RTC_HandleTypeDef hrtc;

/*
 * RTC backup-register layout (32 registers, 4 bytes each = 128 bytes on
 * paper, but the STM32F722 hardware only exposes 20 -- DR0..DR19):
 *
 *   DR0          mouse sensitivity (1 byte, managed elsewhere)
 *   DR1          magic word 'GPM2' -- bumped from 'GPM1' to invalidate
 *                the old per-port layout.
 *   DR2..DR5     profile 0  (4 registers, 14 bytes used, 2 bytes pad)
 *   DR6..DR9     profile 1
 *   DR10..DR13   profile 2
 *   DR14..DR17   profile 3
 *   DR18..DR19   spare
 *
 * Per-profile packing (4 registers):
 *   W0  =  vid | (pid << 16)
 *   W1  =  src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24)
 *   W2  =  src[4] | (src[5] << 8) | (src[6] << 16) | (src[7] << 24)
 *   W3  =  src[8] | (src[9] << 8) | (pad << 16) | (pad << 24)
 *
 * Slot 0 is MRU.  Lookup is linear; save shifts older slots down.
 */
#define GMAP_BKP_MAGIC      0x47504D32U   /* 'GPM2' */
#define BKP_MAGIC_REG       RTC_BKP_DR1
#define BKP_PROFILE_BASE    2U            /* DR2 */
#define BKP_REGS_PER_PROF   4U

gamepad_map_t gamepad1_map;
gamepad_map_t gamepad2_map;

uint16_t gamepad1_vid, gamepad1_pid;
uint16_t gamepad2_vid, gamepad2_pid;

gamepad_profile_t gamepad_profiles[GAMEPAD_PROFILE_SLOTS];

static const gamepad_map_t default_map = {
    .src = {
        [GMAP_FIRE1]       = 6,    /* data bit 6  (USB button 2) */
        [GMAP_FIRE2]       = 5,    /* data bit 5  (USB button 1) */
        [GMAP_FIRE3]       = 4,    /* data bit 4  (USB button 0) */
        [GMAP_CD32_PLAY]   = 13,   /* extraBtn 5  (Start)        */
        [GMAP_CD32_RW]     = 8,    /* extraBtn 0  (L1)           */
        [GMAP_CD32_FF]     = 9,    /* extraBtn 1  (R1)           */
        [GMAP_CD32_GREEN]  = 7,    /* data bit 7  (USB button 3) */
        [GMAP_CD32_YELLOW] = 4,    /* data bit 4  (USB button 0) */
        [GMAP_CD32_RED]    = 6,    /* data bit 6  (USB button 2) */
        [GMAP_CD32_BLUE]   = 5,    /* data bit 5  (USB button 1) */
    }
};

static void profile_pack(const gamepad_profile_t *p, uint32_t out[BKP_REGS_PER_PROF])
{
    out[0] = (uint32_t)p->vid | ((uint32_t)p->pid << 16);
    out[1] = (uint32_t)p->map.src[0]
           | ((uint32_t)p->map.src[1] << 8)
           | ((uint32_t)p->map.src[2] << 16)
           | ((uint32_t)p->map.src[3] << 24);
    out[2] = (uint32_t)p->map.src[4]
           | ((uint32_t)p->map.src[5] << 8)
           | ((uint32_t)p->map.src[6] << 16)
           | ((uint32_t)p->map.src[7] << 24);
    out[3] = (uint32_t)p->map.src[8]
           | ((uint32_t)p->map.src[9] << 8);
}

static void profile_unpack(const uint32_t in[BKP_REGS_PER_PROF], gamepad_profile_t *p)
{
    p->vid       = (uint16_t)(in[0] & 0xFFFFU);
    p->pid       = (uint16_t)((in[0] >> 16) & 0xFFFFU);
    p->map.src[0] = (uint8_t)(in[1] & 0xFF);
    p->map.src[1] = (uint8_t)((in[1] >> 8) & 0xFF);
    p->map.src[2] = (uint8_t)((in[1] >> 16) & 0xFF);
    p->map.src[3] = (uint8_t)((in[1] >> 24) & 0xFF);
    p->map.src[4] = (uint8_t)(in[2] & 0xFF);
    p->map.src[5] = (uint8_t)((in[2] >> 8) & 0xFF);
    p->map.src[6] = (uint8_t)((in[2] >> 16) & 0xFF);
    p->map.src[7] = (uint8_t)((in[2] >> 24) & 0xFF);
    p->map.src[8] = (uint8_t)(in[3] & 0xFF);
    p->map.src[9] = (uint8_t)((in[3] >> 8) & 0xFF);
}

static void profile_write_bkp(uint8_t slot, const gamepad_profile_t *p)
{
    uint32_t words[BKP_REGS_PER_PROF];
    profile_pack(p, words);
    uint32_t base = BKP_PROFILE_BASE + (uint32_t)slot * BKP_REGS_PER_PROF;
    for (uint32_t i = 0; i < BKP_REGS_PER_PROF; i++) {
        HAL_RTCEx_BKUPWrite(&hrtc, base + i, words[i]);
    }
}

static void profile_read_bkp(uint8_t slot, gamepad_profile_t *p)
{
    uint32_t words[BKP_REGS_PER_PROF];
    uint32_t base = BKP_PROFILE_BASE + (uint32_t)slot * BKP_REGS_PER_PROF;
    for (uint32_t i = 0; i < BKP_REGS_PER_PROF; i++) {
        words[i] = HAL_RTCEx_BKUPRead(&hrtc, base + i);
    }
    profile_unpack(words, p);
}

static int profile_find_slot(uint16_t vid, uint16_t pid)
{
    if (vid == 0U) return -1;
    for (uint8_t i = 0; i < GAMEPAD_PROFILE_SLOTS; i++) {
        if (gamepad_profiles[i].vid == vid
            && gamepad_profiles[i].pid == pid) {
            return (int)i;
        }
    }
    return -1;
}

void gamepad_map_load(void)
{
    gamepad1_map = default_map;
    gamepad2_map = default_map;
    gamepad1_vid = 0U; gamepad1_pid = 0U;
    gamepad2_vid = 0U; gamepad2_pid = 0U;

    if (HAL_RTCEx_BKUPRead(&hrtc, BKP_MAGIC_REG) != GMAP_BKP_MAGIC) {
        /* Fresh / upgraded firmware: clear profile slots, persist
         * the new magic so subsequent boots take the fast path. */
        memset(gamepad_profiles, 0, sizeof(gamepad_profiles));
        for (uint8_t i = 0; i < GAMEPAD_PROFILE_SLOTS; i++) {
            profile_write_bkp(i, &gamepad_profiles[i]);
        }
        HAL_RTCEx_BKUPWrite(&hrtc, BKP_MAGIC_REG, GMAP_BKP_MAGIC);
        return;
    }

    for (uint8_t i = 0; i < GAMEPAD_PROFILE_SLOTS; i++) {
        profile_read_bkp(i, &gamepad_profiles[i]);
    }
}

void gamepad_map_activate(uint8_t port, uint16_t vid, uint16_t pid)
{
    gamepad_map_t *active = (port == 1U) ? &gamepad1_map : &gamepad2_map;
    uint16_t      *active_vid = (port == 1U) ? &gamepad1_vid : &gamepad2_vid;
    uint16_t      *active_pid = (port == 1U) ? &gamepad1_pid : &gamepad2_pid;

    if (vid == 0U) {
        /* Pad gone -- forget identity; leave the in-RAM map as it was
         * (user can still read/write $20..$33, just won't persist
         * anywhere meaningful until a pad is plugged in). */
        *active_vid = 0U;
        *active_pid = 0U;
        return;
    }

    *active_vid = vid;
    *active_pid = pid;

    int slot = profile_find_slot(vid, pid);
    if (slot >= 0) {
        *active = gamepad_profiles[slot].map;
    } else {
        *active = default_map;
    }
}

void gamepad_map_save_active(uint8_t port)
{
    const gamepad_map_t *active = (port == 1U) ? &gamepad1_map : &gamepad2_map;
    uint16_t vid = (port == 1U) ? gamepad1_vid : gamepad2_vid;
    uint16_t pid = (port == 1U) ? gamepad1_pid : gamepad2_pid;

    if (vid == 0U) {
        /* No active pad on this port -- the user is editing $20..$33
         * but there's nothing to attach the change to.  Drop it. */
        return;
    }

    int slot = profile_find_slot(vid, pid);
    if (slot >= 0) {
        /* Update existing profile in-place; promote to MRU if not already. */
        gamepad_profiles[slot].map = *active;
        if (slot > 0) {
            gamepad_profile_t tmp = gamepad_profiles[slot];
            for (int i = slot; i > 0; i--) {
                gamepad_profiles[i] = gamepad_profiles[i - 1];
            }
            gamepad_profiles[0] = tmp;
            /* Persist all slots that shifted (0..slot inclusive). */
            for (int i = 0; i <= slot; i++) {
                profile_write_bkp((uint8_t)i, &gamepad_profiles[i]);
            }
        } else {
            profile_write_bkp(0, &gamepad_profiles[0]);
        }
        return;
    }

    /* New profile -- shift slots down (oldest falls off) and insert at MRU. */
    for (int i = GAMEPAD_PROFILE_SLOTS - 1; i > 0; i--) {
        gamepad_profiles[i] = gamepad_profiles[i - 1];
    }
    gamepad_profiles[0].vid = vid;
    gamepad_profiles[0].pid = pid;
    gamepad_profiles[0].map = *active;
    for (uint8_t i = 0; i < GAMEPAD_PROFILE_SLOTS; i++) {
        profile_write_bkp(i, &gamepad_profiles[i]);
    }
}
