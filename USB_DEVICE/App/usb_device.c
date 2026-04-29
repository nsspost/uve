/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.c
  * @version        : v1.0_Cube
  * @brief          : This file implements the USB Device
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "usb_device.h"
#include "usb_stack_select.h"

#if UVC_USB_STACK_IS_USBX
#include "app_usbx_device.h"
#else
#include "usbd_core.h"
#include "usbd_desc.h"

#include "usbd_uvc.h"
#endif

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USB Device Core handle declaration. */
USBD_HandleTypeDef hUsbDeviceHS;

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * Init USB device Library, add supported class and start the library
  * @retval None
  */
void MX_USB_DEVICE_Init(void)
{
#if UVC_USB_STACK_IS_USBX
  (void)MX_USBX_Device_Init();
#else
  /* USER CODE BEGIN USB_DEVICE_Init_PreTreatment */

  /* USER CODE END USB_DEVICE_Init_PreTreatment */

  /* USER CODE BEGIN USB_DEVICE_Init_PostTreatment */
  USBD_Init(&hUsbDeviceHS, &HS_Desc, DEVICE_HS);
  USBD_UVC_RegisterInterface(&hUsbDeviceHS, NULL);
  USBD_RegisterClass(&hUsbDeviceHS, &USBD_UVC);
  USBD_Start(&hUsbDeviceHS);


  HAL_PWREx_EnableUSBVoltageDetector();

  /* USER CODE END USB_DEVICE_Init_PostTreatment */
#endif
}

/**
  * @}
  */

/**
  * @}
  */

