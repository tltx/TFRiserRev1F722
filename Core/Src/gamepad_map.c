#include "gamepad_map.h"
#include "stm32f7xx_hal.h"

extern RTC_HandleTypeDef hrtc;

/*
 * RTC backup-register layout (DR0 is reserved for mouse sensitivity):
 *   DR1 = pad1 entries 0..3   (1 byte each)
 *   DR2 = pad1 entries 4..7
 *   DR3 = pad1 entries 8..9   (in low 16 bits)
 *   DR4 = pad2 entries 0..3
 *   DR5 = pad2 entries 4..7
 *   DR6 = pad2 entries 8..9
 *   DR7 = magic word ('GPM1') indicating valid contents
 */
#define GMAP_BKP_MAGIC      0x47504D31U   /* 'GPM1' */

gamepad_map_t gamepad1_map;
gamepad_map_t gamepad2_map;

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

static uint32_t pack4(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void unpack4(uint32_t v, uint8_t *p)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void gamepad_map_load(void)
{
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR7) != GMAP_BKP_MAGIC) {
        gamepad1_map = default_map;
        gamepad2_map = default_map;
        return;
    }

    unpack4(HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1), &gamepad1_map.src[0]);
    unpack4(HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2), &gamepad1_map.src[4]);
    uint32_t v3 = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3);
    gamepad1_map.src[8] = (uint8_t)(v3 & 0xFF);
    gamepad1_map.src[9] = (uint8_t)((v3 >> 8) & 0xFF);

    unpack4(HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR4), &gamepad2_map.src[0]);
    unpack4(HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR5), &gamepad2_map.src[4]);
    uint32_t v6 = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR6);
    gamepad2_map.src[8] = (uint8_t)(v6 & 0xFF);
    gamepad2_map.src[9] = (uint8_t)((v6 >> 8) & 0xFF);
}

void gamepad_map_save(void)
{
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, pack4(&gamepad1_map.src[0]));
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, pack4(&gamepad1_map.src[4]));
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3,
        (uint32_t)gamepad1_map.src[8] |
        ((uint32_t)gamepad1_map.src[9] << 8));

    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR4, pack4(&gamepad2_map.src[0]));
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR5, pack4(&gamepad2_map.src[4]));
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR6,
        (uint32_t)gamepad2_map.src[8] |
        ((uint32_t)gamepad2_map.src[9] << 8));

    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR7, GMAP_BKP_MAGIC);
}
