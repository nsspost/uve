#include "app_usbx_device.h"

#include <string.h>

#include "main.h"
#include "ux_device_descriptors.h"
#include "ux_device_video.h"
#include "ux_dcd_stm32.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

#define USBX_DEVICE_MEMORY_STACK_SIZE (64U * 1024U)

#define USBX_DEVICE_USE_OTG_FS 1U

#if (USBX_DEVICE_USE_OTG_FS != 0U)
#define USBX_DEVICE_USB_INSTANCE USB_OTG_FS
#define USBX_DEVICE_PCD_SPEED PCD_SPEED_FULL
#define USBX_DEVICE_PHY_ITFACE USB_OTG_EMBEDDED_PHY
#define USBX_DEVICE_RX_FIFO_WORDS 0x80U
#define USBX_DEVICE_TX0_FIFO_WORDS 0x40U
#define USBX_DEVICE_TX1_FIFO_WORDS 0x80U
#else
#define USBX_DEVICE_USB_INSTANCE USB_OTG_HS
#define USBX_DEVICE_PCD_SPEED PCD_SPEED_HIGH
#define USBX_DEVICE_PHY_ITFACE USB_OTG_ULPI_PHY
#define USBX_DEVICE_RX_FIFO_WORDS 0x80U
#define USBX_DEVICE_TX0_FIFO_WORDS 0x40U
#define USBX_DEVICE_TX1_FIFO_WORDS 0x300U
#endif

__attribute__((section(".xsdram"), aligned(32)))
static UCHAR usbx_memory[USBX_DEVICE_MEMORY_STACK_SIZE];

static UX_DEVICE_CLASS_VIDEO_PARAMETER video_parameter;
static UX_DEVICE_CLASS_VIDEO_STREAM_PARAMETER video_stream_parameter[USBD_VIDEO_STREAM_NMNBER];
static uint8_t usbx_initialized;

ALIGN_TYPE _ux_utility_interrupt_disable(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  return (ALIGN_TYPE)primask;
}

void _ux_utility_interrupt_restore(ALIGN_TYPE flags)
{
  if (((uint32_t)flags & 1U) == 0U)
  {
    __enable_irq();
  }
}

ULONG _ux_utility_time_get(void)
{
  return (ULONG)HAL_GetTick();
}

ULONG _ux_utility_time_elapsed(ULONG start, ULONG now)
{
  return now - start;
}

static UINT USBX_PCD_Init(void)
{
  memset(&hpcd_USB_OTG_HS, 0, sizeof(hpcd_USB_OTG_HS));

  hpcd_USB_OTG_HS.Instance = USBX_DEVICE_USB_INSTANCE;
  hpcd_USB_OTG_HS.Init.dev_endpoints = 4;
  hpcd_USB_OTG_HS.Init.ep0_mps = 0x40;
  hpcd_USB_OTG_HS.Init.speed = USBX_DEVICE_PCD_SPEED;
  hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.phy_itface = USBX_DEVICE_PHY_ITFACE;
  hpcd_USB_OTG_HS.Init.Sof_enable = ENABLE;
  hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.battery_charging_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;

  if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK)
  {
    return UX_ERROR;
  }

  HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS, USBX_DEVICE_RX_FIFO_WORDS);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 0, USBX_DEVICE_TX0_FIFO_WORDS);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 1, USBX_DEVICE_TX1_FIFO_WORDS);

  return UX_SUCCESS;
}

UINT MX_USBX_Device_Init(void)
{
  UCHAR *device_framework_high_speed;
  UCHAR *device_framework_full_speed;
  ULONG device_framework_hs_length;
  ULONG device_framework_fs_length;
  UCHAR *string_framework;
  ULONG string_framework_length;
  UCHAR *language_id_framework;
  ULONG language_id_framework_length;
  ULONG video_configuration_number;
  ULONG video_interface_number;
  UINT status;

  if (usbx_initialized != 0U)
  {
    return UX_SUCCESS;
  }

  status = ux_system_initialize(usbx_memory, sizeof(usbx_memory), UX_NULL, 0U);
  if (status != UX_SUCCESS)
  {
    return status;
  }

  device_framework_high_speed = USBD_Get_Device_Framework_Speed(USBD_HIGH_SPEED,
                                                                &device_framework_hs_length);
  device_framework_full_speed = USBD_Get_Device_Framework_Speed(USBD_FULL_SPEED,
                                                                &device_framework_fs_length);
  string_framework = USBD_Get_String_Framework(&string_framework_length);
  language_id_framework = USBD_Get_Language_Id_Framework(&language_id_framework_length);

  status = ux_device_stack_initialize(device_framework_high_speed,
                                      device_framework_hs_length,
                                      device_framework_full_speed,
                                      device_framework_fs_length,
                                      string_framework,
                                      string_framework_length,
                                      language_id_framework,
                                      language_id_framework_length,
                                      UX_NULL);
  if (status != UX_SUCCESS)
  {
    return status;
  }

  memset(&video_parameter, 0, sizeof(video_parameter));
  memset(video_stream_parameter, 0, sizeof(video_stream_parameter));

  video_parameter.ux_device_class_video_parameter_streams_nb = USBD_VIDEO_STREAM_NMNBER;
  video_parameter.ux_device_class_video_parameter_streams = video_stream_parameter;
  video_parameter.ux_device_class_video_parameter_callbacks.ux_slave_class_video_instance_activate =
      USBD_VIDEO_Activate;
  video_parameter.ux_device_class_video_parameter_callbacks.ux_slave_class_video_instance_deactivate =
      USBD_VIDEO_Deactivate;

  video_stream_parameter[0].ux_device_class_video_stream_parameter_callbacks.ux_device_class_video_stream_change =
      USBD_VIDEO_StreamChange;
  video_stream_parameter[0].ux_device_class_video_stream_parameter_callbacks.ux_device_class_video_stream_request =
      USBD_VIDEO_StreamRequest;
  video_stream_parameter[0].ux_device_class_video_stream_parameter_callbacks.ux_device_class_video_stream_payload_done =
      USBD_VIDEO_StreamPayloadDone;
  video_stream_parameter[0].ux_device_class_video_stream_parameter_max_payload_buffer_nb =
      USBD_VIDEO_PAYLOAD_BUFFER_NUMBER;
  video_stream_parameter[0].ux_device_class_video_stream_parameter_max_payload_buffer_size =
      USBD_VIDEO_StreamGetMaxPayloadBufferSize();
  video_stream_parameter[0].ux_device_class_video_stream_parameter_task_function =
      ux_device_class_video_write_task_function;

  video_configuration_number = USBD_Get_Configuration_Number(CLASS_TYPE_VIDEO, 0);
  video_interface_number = USBD_Get_Interface_Number(CLASS_TYPE_VIDEO, 0);

  status = ux_device_stack_class_register(_ux_system_device_class_video_name,
                                          ux_device_class_video_entry,
                                          video_configuration_number,
                                          video_interface_number,
                                          &video_parameter);
  if (status != UX_SUCCESS)
  {
    return status;
  }

  status = USBX_PCD_Init();
  if (status != UX_SUCCESS)
  {
    return status;
  }

  status = ux_dcd_stm32_initialize((ULONG)USBX_DEVICE_USB_INSTANCE, (ULONG)&hpcd_USB_OTG_HS);
  if (status != UX_SUCCESS)
  {
    return status;
  }

  if (HAL_PCD_Start(&hpcd_USB_OTG_HS) != HAL_OK)
  {
    return UX_ERROR;
  }

  HAL_PWREx_EnableUSBVoltageDetector();
  usbx_initialized = 1U;

  return UX_SUCCESS;
}

void MX_USBX_Device_Process(void)
{
  if (usbx_initialized == 0U)
  {
    return;
  }

  (void)_ux_system_tasks_run();
}
