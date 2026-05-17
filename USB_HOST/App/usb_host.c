/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file            : usb_host.c
  * @version         : v1.0_Cube
  * @brief           : This file implements the USB Host
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "usb_host.h"
#include "usbh_core.h"
#include "usbh_hid.h"
#include "gamepad_map.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/




uint8_t HSReady = 0;
uint8_t FSReady = 0;
HID_USBDevicesTypeDef usbDev;


/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USB Host core handle declaration */
USBH_HandleTypeDef hUsbHostHS;
USBH_HandleTypeDef hUsbHostFS;
ApplicationTypeDef Appli_state = APPLICATION_IDLE;

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*
 * user callback declaration
 */
static void USBH_UserProcess1(USBH_HandleTypeDef *phost, uint8_t id);
static void USBH_UserProcess2(USBH_HandleTypeDef *phost, uint8_t id);

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

//

void mapUSBDevices()
{
	/* Desired VID:PID per Amiga port slot.  Captured as each branch
	 * assigns gamepadN, then reconciled with the persisted per-port
	 * identity at the end so the right profile is auto-loaded when a
	 * pad is plugged or swapped. */
	uint16_t want_vid1 = 0U, want_pid1 = 0U;
	uint16_t want_vid2 = 0U, want_pid2 = 0U;

//process FS USB
	// check if Device is Ready
	// if it is ready
if (HSReady==1)
{
	HID_HandleTypeDef *HID_Handle1;
	HID_HandleTypeDef *HID_Handle2;
	int currentInterfaceHS = 0;
	currentInterfaceHS = hUsbHostHS.device.current_interface;



		HID_Handle1 = hUsbHostHS.pActiveClass->pData[0];
		USBH_SelectInterface(&hUsbHostHS, 0);

		usbDev.keyboardusbhost = NULL;
		/* Compute the new port-type into a local and store it as a single
		 * write at the end of the HS block.  Writing the type byte directly
		 * to usbDev mid-detection leaves a brief NONE window that the EXTI
		 * read for $14 can catch, making the Amiga see a phantom empty port. */
		uint8_t new_hs_type = USB_DEV_NONE;

		  /* Classify by the Init function pointer (set by IFACE_INITSUBCLASS
		   * based on either boot-protocol class/subclass/protocol OR
		   * RptDesc.type, whichever matched).  Using Init catches boot-mode
		   * keyboards/mice even when the report-descriptor parser couldn't
		   * classify them -- needed for e.g. the Logitech Unifying Receiver
		   * whose 148-byte mouse descriptor this parser doesn't fully
		   * decode. */
		  if (HID_Handle1->Init == USBH_HID_KeybdInit) {
			usbDev.keyboard = USBH_HID_GetKeybdInfo(&hUsbHostHS);
			usbDev.keyboardusbhost = &hUsbHostHS;
			new_hs_type = USB_DEV_KEYBOARD;
		  } else if (HID_Handle1->Init == USBH_HID_MouseInit) {
			usbDev.mouse = USBH_HID_GetMouseInfo(&hUsbHostHS);
			usbDev.mouseDetected = 1;
			usbDev.overridePorts = 1;
			new_hs_type = USB_DEV_MOUSE;
		  } else if (HID_Handle1->Init == USBH_HID_GamepadInit) {
			usbDev.gamepad1 = USBH_HID_GetGamepadInfo(&hUsbHostHS);
			usbDev.overridePorts = 1;
			new_hs_type = USB_DEV_GAMEPAD;
			want_vid1 = hUsbHostHS.device.DevDesc.idVendor;
			want_pid1 = hUsbHostHS.device.DevDesc.idProduct;
		  }


	if (hUsbHostHS.pActiveClass->interfaces>1)
	{



		HID_Handle2 = hUsbHostHS.pActiveClass->pData[1];
		USBH_SelectInterface(&hUsbHostHS, 1);

		if (HID_Handle2->Init == USBH_HID_KeybdInit) {
			usbDev.keyboard = USBH_HID_GetKeybdInfo(&hUsbHostHS);
			usbDev.keyboardusbhost = &hUsbHostHS;
			if (new_hs_type == USB_DEV_NONE)
				new_hs_type = USB_DEV_KEYBOARD;
		} else if (HID_Handle2->Init == USBH_HID_MouseInit) {
			usbDev.mouse = USBH_HID_GetMouseInfo(&hUsbHostHS);
			usbDev.mouseDetected = 1;
			usbDev.overridePorts = 1;
			if (new_hs_type != USB_DEV_GAMEPAD)
				new_hs_type = USB_DEV_MOUSE;
		} else if (HID_Handle2->Init == USBH_HID_GamepadInit) {
			usbDev.gamepad2 = USBH_HID_GetGamepadInfo(&hUsbHostHS);
			usbDev.overridePorts = 1;
			new_hs_type = USB_DEV_GAMEPAD;
			want_vid2 = hUsbHostHS.device.DevDesc.idVendor;
			want_pid2 = hUsbHostHS.device.DevDesc.idProduct;
		}
		 USBH_SelectInterface(&hUsbHostHS, currentInterfaceHS);
	}

	usbDev.hs_device_type = new_hs_type;   /* single atomic store */




}



if (FSReady==1)
{
	HID_HandleTypeDef *HID_Handle1;
	HID_HandleTypeDef *HID_Handle2;

	int currentInterfaceFS = 0;
	currentInterfaceFS = hUsbHostFS.device.current_interface;

		HID_Handle1 = hUsbHostFS.pActiveClass->pData[0];
		USBH_SelectInterface(&hUsbHostFS, 0);

		uint8_t new_fs_type = USB_DEV_NONE;

		  /* Same classify-by-Init-pointer pattern as the HS block above. */
		  if (HID_Handle1->Init == USBH_HID_KeybdInit) {
			usbDev.keyboard = USBH_HID_GetKeybdInfo(&hUsbHostFS);
			usbDev.keyboardusbhost = &hUsbHostFS;
			new_fs_type = USB_DEV_KEYBOARD;
		  } else if (HID_Handle1->Init == USBH_HID_MouseInit) {
			usbDev.mouse = USBH_HID_GetMouseInfo(&hUsbHostFS);
			usbDev.mouseDetected = 1;
			usbDev.overridePorts = 1;
			new_fs_type = USB_DEV_MOUSE;
		  } else if (HID_Handle1->Init == USBH_HID_GamepadInit) {
			usbDev.gamepad2 = USBH_HID_GetGamepadInfo(&hUsbHostFS);
			usbDev.overridePorts = 1;
			new_fs_type = USB_DEV_GAMEPAD;
			want_vid2 = hUsbHostFS.device.DevDesc.idVendor;
			want_pid2 = hUsbHostFS.device.DevDesc.idProduct;
		  }


	if (hUsbHostFS.pActiveClass->interfaces>1)
	{

		HID_Handle2 = hUsbHostFS.pActiveClass->pData[1];
		USBH_SelectInterface(&hUsbHostFS, 1);

		if (HID_Handle2->Init == USBH_HID_KeybdInit) {
			usbDev.keyboard = USBH_HID_GetKeybdInfo(&hUsbHostFS);
			usbDev.keyboardusbhost = &hUsbHostFS;
			if (new_fs_type == USB_DEV_NONE)
				new_fs_type = USB_DEV_KEYBOARD;
		} else if (HID_Handle2->Init == USBH_HID_MouseInit) {
			usbDev.mouse = USBH_HID_GetMouseInfo(&hUsbHostFS);
			usbDev.mouseDetected = 1;
			usbDev.overridePorts = 1;
			if (new_fs_type != USB_DEV_GAMEPAD)
				new_fs_type = USB_DEV_MOUSE;
		} else if (HID_Handle2->Init == USBH_HID_GamepadInit) {
			usbDev.gamepad1 = USBH_HID_GetGamepadInfo(&hUsbHostFS);
			usbDev.overridePorts = 1;
			new_fs_type = USB_DEV_GAMEPAD;
			want_vid1 = hUsbHostFS.device.DevDesc.idVendor;
			want_pid1 = hUsbHostFS.device.DevDesc.idProduct;
		}
		 USBH_SelectInterface(&hUsbHostFS, currentInterfaceFS);

}

	usbDev.fs_device_type = new_fs_type;   /* single atomic store */


}

	/* Reconcile per-port pad identity.  activate() copies the matching
	 * stored profile (or the built-in default) into the active map only
	 * when VID:PID changes -- so user edits in $20..$33 between scans
	 * aren't clobbered. */
	if (gamepad1_vid != want_vid1 || gamepad1_pid != want_pid1) {
		gamepad_map_activate(1, want_vid1, want_pid1);
	}
	if (gamepad2_vid != want_vid2 || gamepad2_pid != want_pid2) {
		gamepad_map_activate(2, want_vid2, want_pid2);
	}
}

HID_USBDevicesTypeDef* USBH_HID_GetUSBDev()
{
		return &usbDev;
}

/* USER CODE END 1 */

/**
  * Init USB host library, add supported class and start the library
  * @retval None
  */
void MX_USB_HOST_Init(void)
{
  /* USER CODE BEGIN USB_HOST_Init_PreTreatment */
  
  /* USER CODE END USB_HOST_Init_PreTreatment */
  
  /* Init host Library, add supported class and start the library. */
  if (USBH_Init(&hUsbHostHS, USBH_UserProcess1, HOST_HS) != USBH_OK)
  {
    Error_Handler();
  }
  if (USBH_RegisterClass(&hUsbHostHS, USBH_HID_CLASSHS) != USBH_OK)
  {
    Error_Handler();
  }
  if (USBH_Start(&hUsbHostHS) != USBH_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_HOST_Init_PreTreatment */
  
  /* USER CODE END USB_HOST_Init_PreTreatment */
  
  /* Init host Library, add supported class and start the library. */
  if (USBH_Init(&hUsbHostFS, USBH_UserProcess2, HOST_FS) != USBH_OK)
  {
    Error_Handler();
  }
  if (USBH_RegisterClass(&hUsbHostFS, USBH_HID_CLASS) != USBH_OK)
  {
    Error_Handler();
  }
  if (USBH_Start(&hUsbHostFS) != USBH_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_HOST_Init_PostTreatment */
  
  /* USER CODE END USB_HOST_Init_PostTreatment */
}

/*
 * Background task
 */
void MX_USB_HOST_Process(void)
{
  /* USB Host Background task */
  USBH_Process(&hUsbHostHS);
  USBH_Process(&hUsbHostFS);
  mapUSBDevices();
}
/*
 * user callback definition
 */
static void USBH_UserProcess1  (USBH_HandleTypeDef *phost, uint8_t id)
{
  /* USER CODE BEGIN CALL_BACK_2 */
  switch(id)
  {
  case HOST_USER_SELECT_CONFIGURATION:
  break;

  case HOST_USER_DISCONNECTION:
  Appli_state = APPLICATION_DISCONNECT;
  HSReady = 0;
  memset (&usbDev, 0, sizeof(HID_USBDevicesTypeDef));
  break;

  case HOST_USER_CLASS_ACTIVE:
  Appli_state = APPLICATION_READY;
  HSReady = 1;
  break;

  case HOST_USER_CONNECTION:
  Appli_state = APPLICATION_START;
  break;

  default:
  break;
  }
  /* USER CODE END CALL_BACK_2 */
}

static void USBH_UserProcess2  (USBH_HandleTypeDef *phost, uint8_t id)
{
  /* USER CODE BEGIN CALL_BACK_21 */
  switch(id)
  {
  case HOST_USER_SELECT_CONFIGURATION:
  break;

  case HOST_USER_DISCONNECTION:
  Appli_state = APPLICATION_DISCONNECT;
  FSReady = 0;
  memset (&usbDev, 0, sizeof(HID_USBDevicesTypeDef));
  break;

  case HOST_USER_CLASS_ACTIVE:
  Appli_state = APPLICATION_READY;
  FSReady = 1;
  break;

  case HOST_USER_CONNECTION:
  Appli_state = APPLICATION_START;
  break;

  default:
  break;
  }
  /* USER CODE END CALL_BACK_21 */
}

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
