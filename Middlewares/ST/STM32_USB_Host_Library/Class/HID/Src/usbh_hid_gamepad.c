/**
  ******************************************************************************
  * @file    usbh_hid_gamepad.c
  * @author  MCD Application Team
  * @brief   This file is the application layer for USB Host HID gamepad Handling.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      http://www.st.com/SLA0044
  *
  ******************************************************************************
  */

  /* BSPDependencies
  - "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
  - "stm32xxxxx_{eval}{discovery}_io.c"
  - "stm32xxxxx_{eval}{discovery}{adafruit}_lcd.c"
  - "stm32xxxxx_{eval}{discovery}_sdram.c"
  EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/

#include "usbh_hid_parser.h"
#include "usbh_hid_gamepad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/** @addtogroup USBH_LIB
  * @{
  */

/** @addtogroup USBH_CLASS
  * @{
  */

/** @addtogroup USBH_HID_CLASS
  * @{
  */

/** @defgroup USBH_HID_gamepad
  * @brief    This file includes HID Layer Handlers for USB Host HID class.
  * @{
  */

/** @defgroup USBH_HID_gamepad_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_gamepad_Private_Defines
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_gamepad_Private_Macros
  * @{
  */
/**
  * @}
  */

/** @defgroup USBH_HID_gamepad_Private_FunctionPrototypes
  * @{
  */
#define JOYSTICK_AXIS_MIN           0
#define JOYSTICK_AXIS_MID           127
#define JOYSTICK_AXIS_MAX           255
#define JOYSTICK_AXIS_TRIGGER_MIN   64
#define JOYSTICK_AXIS_TRIGGER_MAX   192

#define JOY_RIGHT       0x01
#define JOY_LEFT        0x02
#define JOY_DOWN        0x04
#define JOY_UP          0x08
#define JOY_BTN_SHIFT   4
#define JOY_BTN1        0x10
#define JOY_BTN2        0x20
#define JOY_BTN3        0x40
#define JOY_BTN4        0x80
#define JOY_MOVE        (JOY_RIGHT|JOY_LEFT|JOY_UP|JOY_DOWN)

static USBH_StatusTypeDef USBH_HID_GamepadDecode(USBH_HandleTypeDef *phost);

/**
  * @}
  */


/** @defgroup USBH_HID_gamepad_Private_Variables
  * @{
  */
/* One gamepad_info per USB host so two simultaneously-connected pads
 * (one on HS, one on FS) keep independent state.  Indexed by
 * phost->id (HOST_HS=0, HOST_FS=1).  Multi-interface gamepad devices
 * on the same host still share -- not a real concern in practice. */
HID_gamepad_Info_TypeDef    gamepad_info[2];

//static uint8_t gamepad_info;


static uint16_t collect_bits(uint8_t *p, uint16_t offset, uint8_t size, int is_signed) {
  // mask unused bits of first byte
  uint8_t mask = 0xff << (offset&7);
  uint8_t byte = offset/8;
  uint8_t bits = size;
  uint8_t shift = offset&7;

  uint16_t rval = (p[byte++] & mask) >> shift;
  mask = 0xff;
  shift = 8-shift;
  bits -= shift;

  // first byte already contained more bits than we need
  if(shift > size) {
    // mask unused bits
    rval &= (1<<size)-1;
  } else {
    // further bytes if required
    while(bits) {
      mask = (bits<8)?(0xff>>(8-bits)):0xff;
      rval += (p[byte++] & mask) << shift;
      shift += 8;
      bits -= (bits>8)?8:bits;
    }
  }

  if(is_signed) {
    // do sign expansion
    uint16_t sign_bit = 1<<(size-1);
    if(rval & sign_bit) {
      while(sign_bit) {
	rval |= sign_bit;
	sign_bit <<= 1;
      }

    }
  }

  return rval;
}


/* ------------------------------------------------------------------ *
 * Nintendo Switch Pro Controller (VID 057E / PID 2009) over USB.
 *
 * It enumerates as a HID device but streams no usable input until the
 * host runs a Nintendo-specific bring-up handshake on the interrupt
 * OUT endpoint.  usbh_hid.c forces this VID/PID onto the gamepad path
 * so USBH_HID_GamepadDecode() runs every poll; from there we drive the
 * handshake (here) and, once the pad emits its 0x30 "standard full"
 * reports, decode them into the Amiga layout further down.
 *
 * First cut, validated only by review -- iterate with `risergamepad raw`,
 * which shows the bytes the pad is actually sending.
 * ------------------------------------------------------------------ */
#define SWITCH_PRO_VID  0x057EU
#define SWITCH_PRO_PID  0x2009U

/* Free-running 1 ms tick for handshake pacing.  phost->Timer proved
 * unreliable here -- it stops advancing shortly after connect, which
 * froze the handshake at a random step; SysTick keeps running. */
uint32_t HAL_GetTick(void);

/* Per-host handshake state (index by phost->id & 1). */
static uint8_t  pro_step[2];     /* position in the bring-up sequence   */
static uint8_t  pro_done[2];     /* set once 0x30 reports are arriving  */
static uint8_t  pro_pkt[2];      /* rolling packet counter for 0x01 cmd */
static uint32_t pro_next[2];     /* phost->Timer at which to send next  */
static uint8_t  pro_out[2][12];  /* OUT buffer (must outlive transfer)  */

/* Five fixed bring-up commands, sent once each in order:
 *   80 01 status, 80 02 handshake, 80 03 baud, 80 02 handshake,
 *   80 04 force USB / disable the HID timeout.                         */
static const uint8_t pro_cmd80[5][2] = {
  {0x80, 0x01}, {0x80, 0x02}, {0x80, 0x03}, {0x80, 0x02}, {0x80, 0x04}
};

static void USBH_HID_ProReset(uint8_t id)
{
  id &= 1U;
  pro_step[id] = 0U;
  pro_done[id] = 0U;
  pro_pkt[id]  = 0U;
  pro_next[id] = 0U;
}

static void USBH_HID_ProHandshake(USBH_HandleTypeDef *phost,
                                  HID_HandleTypeDef *h)
{
  uint8_t id = phost->id & 1U;

  if (pro_done[id]) return;
  if (h->OutPipe == 0U) return;                          /* no OUT EP */

  /* CRITICAL: never re-arm the OUT channel while the previous transfer
   * is still in flight.  Submitting on a busy host channel corrupts its
   * state and HardFaults the OTG core (all USB input dies).  Wait for it
   * to return to DONE/IDLE -- this also gives the controller proper
   * one-command-at-a-time flow control. */
  {
    USBH_URBStateTypeDef ost = USBH_LL_GetURBState(phost, h->OutPipe);
    if (ost != USBH_URB_DONE && ost != USBH_URB_IDLE) return;
  }
  if ((int32_t)(HAL_GetTick() - pro_next[id]) < 0) return; /* min spacing */

  if (pro_step[id] < 5U) {
    pro_out[id][0] = pro_cmd80[pro_step[id]][0];
    pro_out[id][1] = pro_cmd80[pro_step[id]][1];
    USBH_InterruptSendData(phost, pro_out[id], 2U, h->OutPipe);
    pro_step[id]++;
    pro_next[id] = HAL_GetTick() + 40U;                  /* 40 ms apart */
  } else {
    /* Set input report mode to 0x30 (standard full @ 60 Hz), carried in
     * a rumble+subcommand (0x01) report.  Re-sent every 250 ms until the
     * decoder sees 0x30 reports and sets pro_done. */
    pro_out[id][0]  = 0x01U;             /* rumble + subcommand report  */
    pro_out[id][1]  = pro_pkt[id];       /* rolling packet number       */
    pro_out[id][2]  = 0x00U;             /* neutral rumble (left)       */
    pro_out[id][3]  = 0x01U;
    pro_out[id][4]  = 0x40U;
    pro_out[id][5]  = 0x40U;
    pro_out[id][6]  = 0x00U;             /* neutral rumble (right)      */
    pro_out[id][7]  = 0x01U;
    pro_out[id][8]  = 0x40U;
    pro_out[id][9]  = 0x40U;
    pro_out[id][10] = 0x03U;             /* subcommand: set report mode */
    pro_out[id][11] = 0x30U;             /* arg: standard full @ 60 Hz  */
    USBH_InterruptSendData(phost, pro_out[id], 12U, h->OutPipe);
    pro_pkt[id] = (uint8_t)((pro_pkt[id] + 1U) & 0x0FU);
    pro_next[id] = HAL_GetTick() + 250U;
  }
}


/**
  * @}
  */


/** @defgroup USBH_HID_gamepad_Private_Functions
  * @{
  */

/**
  * @brief  USBH_HID_gamepadInit
  *         The function init the HID gamepad.
  * @param  phost: Host handle
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_GamepadInit(USBH_HandleTypeDef *phost)
{
  HID_HandleTypeDef *HID_Handle =  (HID_HandleTypeDef *) phost->pActiveClass->pData[phost->device.current_interface];
  /* Reset this host's gamepad_info so a fresh connection starts with
   * an empty raw_report instead of inheriting the previous pad's last
   * button state.  Without this, learn-raw / raw on a freshly plugged
   * pad see stale data until the new pad sends its first report --
   * confusing because the user expects "just connected" to mean
   * "nothing recorded yet". */
  memset(&gamepad_info[phost->id & 1U], 0, sizeof(HID_gamepad_Info_TypeDef));
  USBH_HID_ProReset((uint8_t)phost->id);

  /* Prefer the endpoint's wMaxPacketSize (already stashed into
   * HID_Handle->length by usbh_hid.c:IFACE_READHID) over the descriptor
   * parser's computed report_size.  Many cheap gamepads have HID descriptors
   * the parser doesn't fully understand: it can yield 0 (decoder always
   * short-circuits and no reports reach the app) or a value smaller than
   * the actual report (button bytes get truncated).  wMaxPacketSize is the
   * largest packet the gamepad's endpoint can send and accommodates short
   * packets safely. */
  uint8_t reportSize = (uint8_t)HID_Handle->length;
  uint8_t parsedSize = HID_Handle->HID_Desc.RptDesc.report_size;
  if (reportSize == 0U) reportSize = parsedSize;
  if (reportSize == 0U) reportSize = 8U;   /* last-ditch sane default */





  HID_Handle->length = reportSize;


  HID_Handle->pData = (uint8_t*) malloc (reportSize *sizeof(uint8_t)); //(uint8_t*)(void *)
  /* Clamp the queue to the shared device.Data buffer.  A large-report
   * pad (e.g. Switch Pro: 64-byte reports) would otherwise size the FIFO
   * to HID_QUEUE_SIZE*64 = 640 > USBH_MAX_DATA_BUFFER (512); the ring then
   * writes past device.Data, corrupting memory and crashing the whole USB
   * host the moment reports start arriving. */
  uint16_t fifo_sz = (uint16_t)(HID_QUEUE_SIZE * reportSize);
  if (fifo_sz > USBH_MAX_DATA_BUFFER) {
    fifo_sz = (uint16_t)((USBH_MAX_DATA_BUFFER / reportSize) * reportSize);
  }
  USBH_HID_FifoInit(&HID_Handle->fifo, phost->device.Data, fifo_sz);

  return USBH_OK;
}

/**
  * @brief  USBH_HID_GetgamepadInfo
  *         The function return gamepad information.
  * @param  phost: Host handle
  * @retval gamepad information
  */
HID_gamepad_Info_TypeDef *USBH_HID_GetGamepadInfo(USBH_HandleTypeDef *phost)
{
	/* Best-effort refresh from the FIFO; on failure (no new data this tick,
	 * or short FIFO read) we keep the previously-decoded state.  Always
	 * return the per-host gamepad_info so the caller can rely on the
	 * pointer being stable while the gamepad is connected -- otherwise
	 * callers see gamepad1/gamepad2 transiently NULL between USB reports,
	 * which makes the live-state and raw-report registers permanently
	 * read as zero. */
	(void)USBH_HID_GamepadDecode(phost);
	return &gamepad_info[phost->id & 1U];
}



/**
  * @brief  USBH_HID_gamepadDecode
  *         The function decode gamepad data.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_GamepadDecode(USBH_HandleTypeDef *phost)
{
	HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData[phost->device.current_interface];
	HID_gamepad_Info_TypeDef *info = &gamepad_info[phost->id & 1U];
	uint8_t *gamepad_report_data = HID_Handle->pData;

	  if(HID_Handle->length == 0U)
	  {
	    return USBH_FAIL;
	  }

	  uint16_t bytes_read = USBH_HID_FifoRead(&HID_Handle->fifo, gamepad_report_data, HID_Handle->length);

	  /* Snapshot the raw report bytes BEFORE the descriptor-aware parser
	   * runs, even if the FIFO returned a short read.  Many cheap gamepads
	   * send variable-length reports; refusing to handle short reads means
	   * the raw view stays at zero forever. */
	  if (bytes_read > 0) {
		uint16_t copy_len = bytes_read;
		if (copy_len > sizeof(info->raw_report))
			copy_len = sizeof(info->raw_report);
		for (uint16_t i = 0; i < copy_len; i++)
			info->raw_report[i] = gamepad_report_data[i];
		for (uint16_t i = copy_len; i < sizeof(info->raw_report); i++)
			info->raw_report[i] = 0;
		info->raw_report_len = (uint8_t)copy_len;
	  }

	  if (bytes_read == HID_Handle->length)
	    {

		uint8_t jmap = 0;
		uint8_t btn = 0;
		uint8_t btn_extra = 0;
		int16_t a[2];
		uint8_t i;

		hid_report_t conf = HID_Handle->HID_Desc.RptDesc;

		// skip report id if present
		uint8_t *p = gamepad_report_data+(conf.report_id?1:0);


		//process axis
		// two axes ...
				for(i=0;i<2;i++) {
					// if logical minimum is > logical maximum then logical minimum
					// is signed. This means that the value itself is also signed
					int is_signed = conf.joystick_mouse.axis[i].logical.min >
					conf.joystick_mouse.axis[i].logical.max;
					a[i] = collect_bits(p, conf.joystick_mouse.axis[i].offset,
								conf.joystick_mouse.axis[i].size, is_signed);
				}

		//process 4 first buttons
		for(i=0;i<4;i++)
			if(p[conf.joystick_mouse.button[i].byte_offset] &
			 conf.joystick_mouse.button[i].bitmask) btn |= (1<<i);

		// ... and the eight extra buttons
		for(i=4;i<12;i++)
			if(p[conf.joystick_mouse.button[i].byte_offset] &
			 conf.joystick_mouse.button[i].bitmask) btn_extra |= (1<<(i-4));



	for(i=0;i<2;i++) {

		int hrange = (conf.joystick_mouse.axis[i].logical.max - abs(conf.joystick_mouse.axis[i].logical.min)) / 2;
		int dead = hrange/63;

		if (a[i] < conf.joystick_mouse.axis[i].logical.min) a[i] = conf.joystick_mouse.axis[i].logical.min;
		else if (a[i] > conf.joystick_mouse.axis[i].logical.max) a[i] = conf.joystick_mouse.axis[i].logical.max;

		a[i] = a[i] - (abs(conf.joystick_mouse.axis[i].logical.min) + conf.joystick_mouse.axis[i].logical.max) / 2;

		hrange -= dead;
		if (a[i] < -dead) a[i] += dead;
		else if (a[i] > dead) a[i] -= dead;
		else a[i] = 0;

		a[i] = (a[i] * 127) / hrange;

		if (a[i] < -127) a[i] = -127;
		else if (a[i] > 127) a[i] = 127;

		a[i]=a[i]+127; // mist wants a value in the range [0..255]
	}

				if(a[0] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_LEFT;
				if(a[0] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_RIGHT;
				if(a[1] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_UP;
				if(a[1] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_DOWN;
				jmap |= btn << JOY_BTN_SHIFT;      // add buttons

				info->gamepad_data = jmap;
				info->gamepad_extraBtn = btn_extra;
	    }

	  /* Per-device translators for gamepads whose HID descriptor the parser
	   * can't interpret (or that don't even serve a descriptor).  Each one
	   * reads the raw report bytes captured above and overwrites
	   * gamepad_data / gamepad_extraBtn with the standard layout the rest
	   * of the firmware expects:
	   *   gamepad_data bits 0..3 = D-pad (R, L, D, U)
	   *   gamepad_data bits 4..7 = USB buttons 0..3 (b0..b3)
	   *   gamepad_extraBtn       = USB buttons 4..11 (b4..b11)              */
	  {
	    uint16_t vid = phost->device.DevDesc.idVendor;
	    uint16_t pid = phost->device.DevDesc.idProduct;

	    if (vid == 0x289B && pid == 0x0080
	        && info->raw_report_len >= 15) {
	      /* raphnet WUSBMote v2.2 with SNES adapter.  Report bytes:
	       *   byte 13 = SNES buttons bitmap (bit 0..7).  In the SNES
	       *             firmware the WUSBMote ships with, interface 0
	       *             collapses some SNES buttons onto the same bit
	       *             (e.g. A, Select and X all toggle bit 4); only
	       *             5 of the 8 bits are distinct.
	       *   byte 14 = D-pad: bit 4 = UP, bit 5 = LEFT, bit 6 = RIGHT,
	       *             bit 7 = DOWN (the last is inferred -- our test
	       *             didn't capture it cleanly).
	       *   bytes 9, 10, 11, 12, 15 also change with shoulder presses
	       *     but the same information is already in byte 13. */
	      uint8_t btns = info->raw_report[13];
	      uint8_t dpad = info->raw_report[14];

	      /* D-pad bit-to-JOY_* mapping is empirically rotated from what
	       * you'd expect: setting JOY_LEFT in gamepad_data results in
	       * the Amiga seeing RIGHT, JOY_RIGHT -> DOWN, JOY_DOWN -> LEFT,
	       * JOY_UP -> UP.  Reverse the rotation so the physical
	       * direction matches what the Amiga sees. */
	      uint8_t d = 0;
	      if (dpad & 0x10U) d |= JOY_UP;     /* physical UP    -> Amiga UP    */
	      if (dpad & 0x20U) d |= JOY_DOWN;   /* physical LEFT  -> Amiga LEFT  */
	      if (dpad & 0x40U) d |= JOY_LEFT;   /* physical RIGHT -> Amiga RIGHT */
	      if (dpad & 0x80U) d |= JOY_RIGHT;  /* physical DOWN  -> Amiga DOWN  */
	      /* Pass byte 13 bits 0..3 through to USB buttons 0..3 and
	       * bits 4..7 to USB buttons 4..7 -- the existing gamepad-map
	       * tool (`risergamepad <pad> <slot> b<N>`) can then bind each
	       * bit to whichever CD32 slot the user prefers. */
	      d |= (btns & 0x0FU) << JOY_BTN_SHIFT;
	      info->gamepad_data = d;
	      info->gamepad_extraBtn = (btns >> 4) & 0x0FU;
	    }
	    else if (vid == 0x6666 && pid == 0x0667
	        && info->raw_report_len >= 3) {
	      /* WiseGroup SmartJoy PSX -> USB.  3-byte report:
	       *   byte 0 = buttons: X=b0 A=b1 B=b2 Y=b3 Sel=b4 Start=b5 L=b6 R=b7
	       *   byte 1 = X axis  (0x00 = left, 0x80 = center, 0xFF = right)
	       *   byte 2 = Y axis  (0x00 = up,   0x80 = center, 0xFF = down)  */
	      uint8_t btns = info->raw_report[0];
	      uint8_t xax  = info->raw_report[1];
	      uint8_t yax  = info->raw_report[2];

	      uint8_t d = 0;
	      if (xax < 0x40U) d |= JOY_LEFT;
	      if (xax > 0xC0U) d |= JOY_RIGHT;
	      if (yax < 0x40U) d |= JOY_UP;
	      if (yax > 0xC0U) d |= JOY_DOWN;
	      /* Move PSX X/A/B/Y into the data byte's "USB buttons 0..3" slot */
	      d |= (btns & 0x0FU) << JOY_BTN_SHIFT;
	      /* Sel / Start / L / R go into extraBtn bits 0..3                */
	      info->gamepad_data = d;
	      info->gamepad_extraBtn = (btns >> 4) & 0x0FU;
	    }
	    else if (vid == SWITCH_PRO_VID && pid == SWITCH_PRO_PID) {
	      /* Drive the wired bring-up handshake until 0x30 reports flow. */
	      USBH_HID_ProHandshake(phost, HID_Handle);

	      /* 0x30 standard full report:
	       *   [3] Y X B A SR SL R  ZR
	       *   [4] - + Rs Ls Home Cap
	       *   [5] Dn Up Rt Lf SR SL L ZL
	       *   [6..8] left stick (12-bit X then 12-bit Y)              */
	      if (info->raw_report_len >= 12 && info->raw_report[0] == 0x30U) {
	        pro_done[phost->id & 1U] = 1U;   /* streaming: stop resends */

	        uint8_t b1 = info->raw_report[3];
	        uint8_t b2 = info->raw_report[4];
	        uint8_t b3 = info->raw_report[5];
	        uint8_t d = 0;

	        if (b3 & 0x04U) d |= JOY_RIGHT;  /* D-pad (byte 5) */
	        if (b3 & 0x08U) d |= JOY_LEFT;
	        if (b3 & 0x01U) d |= JOY_DOWN;
	        if (b3 & 0x02U) d |= JOY_UP;

	        /* Left analog stick -> directions too (12-bit, ~2048 mid).
	         * If up/down read swapped on your unit, flip these two. */
	        uint16_t lx = (uint16_t)info->raw_report[6]
	                    | ((uint16_t)(info->raw_report[7] & 0x0FU) << 8);
	        uint16_t ly = (uint16_t)(info->raw_report[7] >> 4)
	                    | ((uint16_t)info->raw_report[8] << 4);
	        if (lx < 1200U) d |= JOY_LEFT;
	        if (lx > 2900U) d |= JOY_RIGHT;
	        if (ly > 2900U) d |= JOY_UP;
	        if (ly < 1200U) d |= JOY_DOWN;

	        /* Face buttons -> USB buttons 0..3 (b0=B b1=A b2=Y b3=X). */
	        uint8_t btns = 0;
	        if (b1 & 0x04U) btns |= 0x01U;   /* B */
	        if (b1 & 0x08U) btns |= 0x02U;   /* A */
	        if (b1 & 0x01U) btns |= 0x04U;   /* Y */
	        if (b1 & 0x02U) btns |= 0x08U;   /* X */
	        d |= (uint8_t)((btns & 0x0FU) << JOY_BTN_SHIFT);

	        /* USB buttons 4..11: L R ZL ZR Minus Plus Home Capture. */
	        uint8_t ex = 0;
	        if (b3 & 0x40U) ex |= 0x01U;     /* L  */
	        if (b1 & 0x40U) ex |= 0x02U;     /* R  */
	        if (b3 & 0x80U) ex |= 0x04U;     /* ZL */
	        if (b1 & 0x80U) ex |= 0x08U;     /* ZR */
	        if (b2 & 0x01U) ex |= 0x10U;     /* Minus   */
	        if (b2 & 0x02U) ex |= 0x20U;     /* Plus    */
	        if (b2 & 0x10U) ex |= 0x40U;     /* Home    */
	        if (b2 & 0x20U) ex |= 0x80U;     /* Capture */

	        info->gamepad_data = d;
	        info->gamepad_extraBtn = ex;
	      }
	    }
	  }

	  return USBH_OK;
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */


/**
  * @}
  */
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
