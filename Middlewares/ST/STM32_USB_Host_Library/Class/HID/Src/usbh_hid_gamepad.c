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
HID_gamepad_Info_TypeDef    gamepad_info;
static uint8_t* gamepad_report_data;

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
  gamepad_report_data = HID_Handle->pData;
  USBH_HID_FifoInit(&HID_Handle->fifo, phost->device.Data, HID_QUEUE_SIZE * reportSize);

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
	 * return the static gamepad_info so the caller can rely on the pointer
	 * being stable while the gamepad is connected -- otherwise callers see
	 * gamepad1/gamepad2 transiently NULL between USB reports, which makes
	 * the live-state and raw-report registers permanently read as zero. */
	(void)USBH_HID_GamepadDecode(phost);
	return &gamepad_info;
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
		if (copy_len > sizeof(gamepad_info.raw_report))
			copy_len = sizeof(gamepad_info.raw_report);
		for (uint16_t i = 0; i < copy_len; i++)
			gamepad_info.raw_report[i] = gamepad_report_data[i];
		for (uint16_t i = copy_len; i < sizeof(gamepad_info.raw_report); i++)
			gamepad_info.raw_report[i] = 0;
		gamepad_info.raw_report_len = (uint8_t)copy_len;
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

				gamepad_info.gamepad_data = jmap;
				gamepad_info.gamepad_extraBtn = btn_extra;
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

	    if (vid == 0x6666 && pid == 0x0667
	        && gamepad_info.raw_report_len >= 3) {
	      /* WiseGroup SmartJoy PSX -> USB.  3-byte report:
	       *   byte 0 = buttons: X=b0 A=b1 B=b2 Y=b3 Sel=b4 Start=b5 L=b6 R=b7
	       *   byte 1 = X axis  (0x00 = left, 0x80 = center, 0xFF = right)
	       *   byte 2 = Y axis  (0x00 = up,   0x80 = center, 0xFF = down)  */
	      uint8_t btns = gamepad_info.raw_report[0];
	      uint8_t xax  = gamepad_info.raw_report[1];
	      uint8_t yax  = gamepad_info.raw_report[2];

	      uint8_t d = 0;
	      if (xax < 0x40U) d |= JOY_LEFT;
	      if (xax > 0xC0U) d |= JOY_RIGHT;
	      if (yax < 0x40U) d |= JOY_UP;
	      if (yax > 0xC0U) d |= JOY_DOWN;
	      /* Move PSX X/A/B/Y into the data byte's "USB buttons 0..3" slot */
	      d |= (btns & 0x0FU) << JOY_BTN_SHIFT;
	      /* Sel / Start / L / R go into extraBtn bits 0..3                */
	      gamepad_info.gamepad_data = d;
	      gamepad_info.gamepad_extraBtn = (btns >> 4) & 0x0FU;
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
