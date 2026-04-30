#include "app_usbx_device.h"

#include <string.h>

#include "main.h"
#include "ux_device_descriptors.h"
#include "ux_device_video.h"
#include "ux_dcd_stm32.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;
extern volatile uint32_t usb_gintmsk_before_iisoixfr_mask;
extern volatile uint32_t usb_gintmsk_after_iisoixfr_mask;
extern volatile uint32_t usb_mask_iisoixfr_enable;

#define USBX_DEVICE_MEMORY_STACK_SIZE (64U * 1024U)

__attribute__((section(".xsdram"), aligned(32)))
static UCHAR usbx_memory[USBX_DEVICE_MEMORY_STACK_SIZE];

static UX_DEVICE_CLASS_VIDEO_PARAMETER video_parameter;
static UX_DEVICE_CLASS_VIDEO_STREAM_PARAMETER video_stream_parameter[USBD_VIDEO_STREAM_NMNBER];
static uint8_t usbx_initialized;

volatile ULONG usbx_init_status_dbg = 0xFFFFFFFFUL;
volatile ULONG usbx_pcd_init_status_dbg = 0xFFFFFFFFUL;
volatile ULONG usbx_dcd_init_status_dbg = 0xFFFFFFFFUL;
volatile ULONG usbx_hal_start_status_dbg = 0xFFFFFFFFUL;
volatile ULONG usbx_task_calls_dbg = 0UL;
volatile ULONG usbx_device_state_dbg = 0UL;
volatile ULONG usbx_device_speed_dbg = 0UL;
volatile ULONG usbx_video_stream_change_dbg = 0UL;
volatile ULONG usbx_video_payload_done_dbg = 0UL;
volatile ULONG usbx_gintmsk_after_start_dbg = 0UL;
volatile ULONG usbx_gintsts_after_start_dbg = 0UL;
volatile ULONG usbx_gintmsk_after_iisoixfr_mask_dbg = 0UL;

ALIGN_TYPE _ux_utility_interrupt_disable(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return (ALIGN_TYPE)primask;
}

void _ux_utility_interrupt_restore(ALIGN_TYPE flags)
{
    if ((flags & 1U) == 0U)
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

static void USBX_MaskIISOIXFRIfEnabled(void)
{
    USB_OTG_GlobalTypeDef *USBx = hpcd_USB_OTG_HS.Instance;

    if (USBx == UX_NULL)
    {
        return;
    }

    usb_gintmsk_before_iisoixfr_mask = USBx->GINTMSK;
    if (usb_mask_iisoixfr_enable != 0U)
    {
        USBx->GINTMSK &= ~USB_OTG_GINTMSK_IISOIXFRM;
        USBx->GINTSTS = USB_OTG_GINTSTS_IISOIXFR;
    }
    usb_gintmsk_after_iisoixfr_mask = USBx->GINTMSK;
    usbx_gintmsk_after_iisoixfr_mask_dbg = USBx->GINTMSK;
}

static UINT USBX_PCD_Init(void)
{
    memset(&hpcd_USB_OTG_HS, 0, sizeof(hpcd_USB_OTG_HS));

    hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
    hpcd_USB_OTG_HS.Init.dev_endpoints = 4;
    hpcd_USB_OTG_HS.Init.ep0_mps = 0x40;
    hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_HIGH;
    hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
    hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_ULPI_PHY;
    hpcd_USB_OTG_HS.Init.Sof_enable = ENABLE;
    hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
    hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
    hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
    hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
    hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;

    if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK)
    {
        return UX_ERROR;
    }

    USBX_MaskIISOIXFRIfEnabled();

    HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS, 0x80);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 0, 0x40);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 1, 0x300);

    return UX_SUCCESS;
}

UINT MX_USBX_Device_Init(void)
{
    UCHAR *device_framework_high_speed;
    UCHAR *device_framework_full_speed;
    UCHAR *string_framework;
    UCHAR *language_id_framework;
    ULONG device_framework_hs_length;
    ULONG device_framework_fs_length;
    ULONG string_framework_length;
    ULONG language_id_framework_length;
    ULONG video_configuration_number;
    ULONG video_interface_number;
    UINT status;

    if (usbx_initialized != 0U)
    {
        return UX_SUCCESS;
    }

    status = ux_system_initialize(usbx_memory,
                                  USBX_DEVICE_MEMORY_STACK_SIZE,
                                  UX_NULL,
                                  0U);
    if (status != UX_SUCCESS)
    {
        usbx_init_status_dbg = status;
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
        usbx_init_status_dbg = status;
        return status;
    }

    memset(&video_parameter, 0, sizeof(video_parameter));
    memset(video_stream_parameter, 0, sizeof(video_stream_parameter));

    video_stream_parameter[0].ux_device_class_video_stream_parameter_callbacks.ux_device_class_video_stream_change =
        USBD_VIDEO_StreamChange;
    video_stream_parameter[0].ux_device_class_video_stream_parameter_callbacks.ux_device_class_video_stream_payload_done =
        USBD_VIDEO_StreamPayloadDone;
    video_stream_parameter[0].ux_device_class_video_stream_parameter_callbacks.ux_device_class_video_stream_request =
        USBD_VIDEO_StreamRequest;
    video_stream_parameter[0].ux_device_class_video_stream_parameter_max_payload_buffer_nb =
        USBD_VIDEO_PAYLOAD_BUFFER_NUMBER;
    video_stream_parameter[0].ux_device_class_video_stream_parameter_max_payload_buffer_size =
        USBD_VIDEO_StreamGetMaxPayloadBufferSize();
    video_stream_parameter[0].ux_device_class_video_stream_parameter_task_function =
        ux_device_class_video_write_task_function;

    video_parameter.ux_device_class_video_parameter_streams_nb = USBD_VIDEO_STREAM_NMNBER;
    video_parameter.ux_device_class_video_parameter_streams = video_stream_parameter;
    video_parameter.ux_device_class_video_parameter_callbacks.ux_slave_class_video_instance_activate =
        USBD_VIDEO_Activate;
    video_parameter.ux_device_class_video_parameter_callbacks.ux_slave_class_video_instance_deactivate =
        USBD_VIDEO_Deactivate;

    video_configuration_number = USBD_Get_Configuration_Number(CLASS_TYPE_VIDEO, 0);
    video_interface_number = USBD_Get_Interface_Number(CLASS_TYPE_VIDEO, 0);

    status = ux_device_stack_class_register(_ux_system_device_class_video_name,
                                            ux_device_class_video_entry,
                                            video_configuration_number,
                                            video_interface_number,
                                            (VOID *)&video_parameter);
    if (status != UX_SUCCESS)
    {
        usbx_init_status_dbg = status;
        return status;
    }

    status = USBX_PCD_Init();
    usbx_pcd_init_status_dbg = status;
    if (status != UX_SUCCESS)
    {
        usbx_init_status_dbg = status;
        return status;
    }

    status = ux_dcd_stm32_initialize((ULONG)USB_OTG_HS, (ULONG)&hpcd_USB_OTG_HS);
    usbx_dcd_init_status_dbg = status;
    if (status != UX_SUCCESS)
    {
        usbx_init_status_dbg = status;
        return status;
    }

    usbx_hal_start_status_dbg = (ULONG)HAL_PCD_Start(&hpcd_USB_OTG_HS);
    if (usbx_hal_start_status_dbg != (ULONG)HAL_OK)
    {
        usbx_init_status_dbg = UX_ERROR;
        return UX_ERROR;
    }
    USBX_MaskIISOIXFRIfEnabled();
    usbx_gintmsk_after_start_dbg = USB_OTG_HS->GINTMSK;
    usbx_gintsts_after_start_dbg = USB_OTG_HS->GINTSTS;

    HAL_PWREx_EnableUSBVoltageDetector();

    usbx_initialized = 1U;
    usbx_init_status_dbg = UX_SUCCESS;
    return UX_SUCCESS;
}

void MX_USBX_Device_Process(void)
{
    if (usbx_initialized == 0U)
    {
        return;
    }

    usbx_task_calls_dbg++;
    (void)_ux_system_tasks_run();

    usbx_device_state_dbg = _ux_system_slave->ux_system_slave_device.ux_slave_device_state;
    usbx_device_speed_dbg = _ux_system_slave->ux_system_slave_speed;
}
