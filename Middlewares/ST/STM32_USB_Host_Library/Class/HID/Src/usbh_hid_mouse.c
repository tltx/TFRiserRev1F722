/**
  ******************************************************************************
  * @file    usbh_hid_mouse.c
  * @author  MCD Application Team
  * @brief   This file is the application layer for USB Host HID Mouse Handling.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      www.st.com/SLA0044
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
#include "usbh_hid_mouse.h"
#include "usbh_hid_parser.h"
#include "usbh_hid_gamepad.h"



/** @addtogroup USBH_LIB
  * @{
  */

/** @addtogroup USBH_CLASS
  * @{
  */

/** @addtogroup USBH_HID_CLASS
  * @{
  */

/** @defgroup USBH_HID_MOUSE
  * @brief    This file includes HID Layer Handlers for USB Host HID class.
  * @{
  */

/** @defgroup USBH_HID_MOUSE_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Defines
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Macros
  * @{
  */
/**
  * @}
  */

/** @defgroup USBH_HID_MOUSE_Private_FunctionPrototypes
  * @{
  */
static USBH_StatusTypeDef USBH_HID_MouseDecode(USBH_HandleTypeDef *phost);

/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Variables
  * @{
  */
HID_MOUSE_Info_TypeDef    mouse_info;
uint8_t*                mouse_report_data;





/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Functions
  * @{
  */

/**
  * @brief  USBH_HID_MouseInit
  *         The function init the HID mouse.
  * @param  phost: Host handle
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_MouseInit(USBH_HandleTypeDef *phost)
{
  HID_HandleTypeDef *HID_Handle =  (HID_HandleTypeDef *) phost->pActiveClass->pData[phost->device.current_interface];
  uint8_t reportSize = 0U;
  reportSize = HID_Handle->HID_Desc.RptDesc.report_size;

  mouse_info.x = 0U;
  mouse_info.y = 0U;
  mouse_info.buttons[0] = 0U;
  mouse_info.buttons[1] = 0U;
  mouse_info.buttons[2] = 0U;

 

  HID_Handle->length = reportSize;


  HID_Handle->pData = (uint8_t*) malloc (reportSize *sizeof(uint8_t)); //(uint8_t*)(void *)
  mouse_report_data = HID_Handle->pData;
  USBH_HID_FifoInit(&HID_Handle->fifo, phost->device.Data, HID_QUEUE_SIZE * reportSize);

  return USBH_OK;
}

/**
  * @brief  USBH_HID_GetMouseInfo
  *         The function return mouse information.
  * @param  phost: Host handle
  * @retval mouse information
  */
HID_MOUSE_Info_TypeDef *USBH_HID_GetMouseInfo(USBH_HandleTypeDef *phost)
{
  /* Best-effort refresh.  Always return the static mouse_info so callers
   * see a stable pointer while the mouse is connected; otherwise
   * usbDev.mouse flips to NULL between USB reports and the main loop's
   * `if (usb->mouse != NULL)` mouse-processing gate is open less than
   * 1% of the time.  Same fix shape as USBH_HID_GetGamepadInfo. */
  (void)USBH_HID_MouseDecode(phost);
  return &mouse_info;
}


static uint16_t collect_bits(uint8_t *p, uint16_t offset, uint8_t size, int is_signed) {
  // mask unused bits of first byte
  uint8_t mask = 0xff << (offset&7);
  uint8_t byte = offset/8;
  uint8_t bits = size;
  uint8_t shift = offset&7;

  //  iprintf("0 m:%x by:%d bi=%d sh=%d ->", mask, byte, bits, shift);
  uint16_t rval = (p[byte++] & mask) >> shift;
  //  iprintf("%d\n", (int16_t)rval);
  mask = 0xff;
  shift = 8-shift;
  bits -= shift;

  // first byte already contained more bits than we need
  if(shift > size) {
    //    iprintf("  too many bits, masked %x ->", (1<<size)-1);
    // mask unused bits
    rval &= (1<<size)-1;
    //    iprintf("%d\n", (int16_t)rval);
  } else {
    // further bytes if required
    while(bits) {
      mask = (bits<8)?(0xff>>(8-bits)):0xff;
      //      iprintf("+ m:%x by:%d bi=%d sh=%d ->", mask, byte, bits, shift);
      rval += (p[byte++] & mask) << shift;
      //      iprintf("%d\n", (int16_t)rval);
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
      //      iprintf(" is negative -> sign expand to %d\n", (int16_t)rval);
    }
  }

  return rval;
}
/**
  * @brief  USBH_HID_MouseDecode
  *         The function decode mouse data.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_MouseDecode(USBH_HandleTypeDef *phost)
{
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData[phost->device.current_interface];

  if(HID_Handle->length == 0U)
  {
    return USBH_FAIL;
  }
  /*Fill report */
  if(USBH_HID_FifoRead(&HID_Handle->fifo, mouse_report_data, HID_Handle->length) ==  HID_Handle->length)
  {

	  uint8_t btn = 0;
	  int16_t a[2];
	  uint8_t i;

	  // skip report id if present
	  uint8_t *p = mouse_report_data+(HID_Handle->HID_Desc.RptDesc.report_id?1:0);

	  /* Boot-mouse fallback: only when the parser couldn't extract
	   * X/Y axis info from the report descriptor (axis size == 0).
	   * Triggers e.g. for the Logitech Unifying Receiver mouse
	   * interface, whose 148-byte descriptor sets RptDesc.type to
	   * REPORT_TYPE_MOUSE but leaves axis offsets/sizes at 0 because
	   * the parser bails on a sub-collection it doesn't understand.
	   * Without this fallback the parser-based decode below reads
	   * zero-size axes via collect_bits() and mouse_info stays at 0.
	   *
	   * Gated on axis[0].size == 0 (not on RptDesc.type) so that
	   * normal mice -- whose parser successfully fills axis offsets
	   * and whose actual wire format may be larger than 3 bytes
	   * (report ID + buttons + X + Y + wheel) -- keep using the
	   * accurate parser-based path.
	   *
	   * Boot mouse format:
	   *   byte 0 = buttons (bit 0 = L, bit 1 = R, bit 2 = M)
	   *   byte 1 = X delta (int8)
	   *   byte 2 = Y delta (int8) */
	  {
	    USBH_InterfaceDescTypeDef *itf =
	      &phost->device.CfgDesc.Itf_Desc[phost->device.current_interface];
	    int is_boot_mouse = (itf->bInterfaceClass == 0x03U
	                         && itf->bInterfaceSubClass == 0x01U
	                         && itf->bInterfaceProtocol == 0x02U);
	    int parser_failed_axes =
	        (HID_Handle->HID_Desc.RptDesc.joystick_mouse.axis[0].size == 0U
	         || HID_Handle->HID_Desc.RptDesc.joystick_mouse.axis[1].size == 0U);
	    if (is_boot_mouse && parser_failed_axes
	        && HID_Handle->length >= 3U) {
	      mouse_info.buttons[0] = p[0] & 0x01U;
	      mouse_info.buttons[1] = (p[0] >> 1) & 0x01U;
	      mouse_info.buttons[2] = (p[0] >> 2) & 0x01U;
	      mouse_info.x = (int8_t)p[1];
	      mouse_info.y = (int8_t)p[2];
	      return USBH_OK;
	    }
	  }

	  //process axis
	  // two axes ...
	  		for(i=0;i<2;i++) {
	  			// if logical minimum is > logical maximum then logical minimum
	  			// is signed. This means that the value itself is also signed
	  			int is_signed = HID_Handle->HID_Desc.RptDesc.joystick_mouse.axis[i].logical.min >
	  				HID_Handle->HID_Desc.RptDesc.joystick_mouse.axis[i].logical.max;
	  			a[i] = collect_bits(p, HID_Handle->HID_Desc.RptDesc.joystick_mouse.axis[i].offset,
	  					HID_Handle->HID_Desc.RptDesc.joystick_mouse.axis[i].size, is_signed);
	  		}

	  //process 4 first buttons
	  for(i=0;i<4;i++)
	  	if(p[HID_Handle->HID_Desc.RptDesc.joystick_mouse.button[i].byte_offset] &
	  			HID_Handle->HID_Desc.RptDesc.joystick_mouse.button[i].bitmask) btn |= (1<<i);

	  //process mouse
	  if(HID_Handle->HID_Desc.RptDesc.type == REPORT_TYPE_MOUSE) {
	  		// limit mouse movement to +/- 128
	  		for(i=0;i<2;i++) {
	  		if((int16_t)a[i] >  127) a[i] =  127;
	  		if((int16_t)a[i] < -128) a[i] = -128;
	  		}
	  		//btn
	  	  mouse_info.x = a[0];
	  	  mouse_info.y = a[1];
	  	  mouse_info.buttons[0] = btn&0x1;
	  	  mouse_info.buttons[1] = (btn>>1)&0x1;
	  	  mouse_info.buttons[2] = (btn>>2)&0x1;
	  	}

    return USBH_OK;
  }
  return   USBH_FAIL;
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
