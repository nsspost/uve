/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ux_device_video.c
  * @author  MCD Application Team
  * @brief   USBX Device Video applicative source file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2020-2021 STMicroelectronics.
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
#include "ux_device_video.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "video_source.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define UVC_PLAY_STATUS_STOP       0x00U
#define UVC_PLAY_STATUS_STREAMING  0x02U
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
ULONG uvc_state;
UX_DEVICE_CLASS_VIDEO *video;
UX_DEVICE_CLASS_VIDEO_STREAM *stream_write;

static UCHAR video_frame_buffer[512];

static uint32_t video_packet_index;
static uint32_t video_frame_sequence;
static uint8_t video_fid = 1U;
static ULONG video_next_frame_tick;
static uint8_t video_frame_pacer_armed;

/* Video Probe data structure */
static USBD_VideoControlTypeDef video_Probe_Control =
{
  .bmHint = 0x0000U,
  .bFormatIndex = 0x01U,
  .bFrameIndex = 0x01U,
  .dwFrameInterval = UVC_FRAME_INTERVAL_FS,
  .wKeyFrameRate = 0x0000U,
  .wPFrameRate = 0x0000U,
  .wCompQuality = 0x0000U,
  .wCompWindowSize = 0x0000U,
  .wDelay = 0x0000U,
  .dwMaxVideoFrameSize = 0x0000U,
  .dwMaxPayloadTransferSize = 0x00000000U,
  .dwClockFrequency = 0x00000000U,
  .bmFramingInfo = 0x00U,
  .bPreferedVersion = 0x00U,
  .bMinVersion = 0x00U,
  .bMaxVersion = 0x00U,
};

/* Video Commit data structure */
static USBD_VideoControlTypeDef video_Commit_Control =
{
  .bmHint = 0x0000U,
  .bFormatIndex = 0x01U,
  .bFrameIndex = 0x01U,
  .dwFrameInterval = UVC_FRAME_INTERVAL_FS,
  .wKeyFrameRate = 0x0000U,
  .wPFrameRate = 0x0000U,
  .wCompQuality = 0x0000U,
  .wCompWindowSize = 0x0000U,
  .wDelay = 0x0000U,
  .dwMaxVideoFrameSize = 0x0000U,
  .dwMaxPayloadTransferSize = 0x00000000U,
  .dwClockFrequency = 0x00000000U,
  .bmFramingInfo = 0x00U,
  .bPreferedVersion = 0x00U,
  .bMinVersion = 0x00U,
  .bMaxVersion = 0x00U,
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static ULONG USBD_VIDEO_GetFrameInterval100ns(void);
static ULONG USBD_VIDEO_GetFramePeriodMs(void);
static ULONG USBD_VIDEO_GetMaxPayloadTransferSize(void);
static VOID USBD_VIDEO_PaceFrameStart(void);
static VOID video_write_payload(UX_DEVICE_CLASS_VIDEO_STREAM *stream);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  USBD_VIDEO_Activate
  *         This function is called when insertion of a Video device.
  * @param  video_instance: Pointer to the video class instance.
  * @retval none
  */
VOID USBD_VIDEO_Activate(VOID *video_instance)
{
  /* USER CODE BEGIN USBD_VIDEO_Activate */
  video = (UX_DEVICE_CLASS_VIDEO *)video_instance;
  stream_write = video->ux_device_class_video_streams;
  /* USER CODE END USBD_VIDEO_Activate */

  return;
}

/**
  * @brief  USBD_VIDEO_Deactivate
  *         This function is called when extraction of a Video device.
  * @param  video_instance: Pointer to the video class instance.
  * @retval none
  */
VOID USBD_VIDEO_Deactivate(VOID *video_instance)
{
  /* USER CODE BEGIN USBD_VIDEO_Deactivate */
  UX_PARAMETER_NOT_USED(video_instance);
  video = UX_NULL;
  stream_write = UX_NULL;
  uvc_state = UVC_PLAY_STATUS_STOP;
  /* USER CODE END USBD_VIDEO_Deactivate */

  return;
}

/**
  * @brief  USBD_VIDEO_StreamChange
  *         This function is invoked to inform application that the
  *         alternate setting are changed.
  * @param  video_stream: Pointer to video class stream instance.
  * @param  alternate_setting: interface alternate setting.
  * @retval none
  */
VOID USBD_VIDEO_StreamChange(UX_DEVICE_CLASS_VIDEO_STREAM *video_stream,
                             ULONG alternate_setting)
{
  /* USER CODE BEGIN USBD_VIDEO_StreamChange */
  if (alternate_setting == 0U)
  {
    uvc_state = UVC_PLAY_STATUS_STOP;
    video_packet_index = 0U;
    video_frame_sequence = 0U;
    video_fid = 1U;
    video_next_frame_tick = 0UL;
    video_frame_pacer_armed = 0U;
    return;
  }

  uvc_state = UVC_PLAY_STATUS_STREAMING;
  video_packet_index = 0U;
  video_frame_sequence = 0U;
  video_fid = 1U;
  video_next_frame_tick = 0UL;
  video_frame_pacer_armed = 0U;

  video_write_payload(video_stream);
  video_write_payload(video_stream);
  (void)ux_device_class_video_transmission_start(video_stream);
  /* USER CODE END USBD_VIDEO_StreamChange */

  return;
}

/**
  * @brief  USBD_VIDEO_StreamPayloadDone
  *         This function is invoked when stream data payload received.
  * @param  video_stream: Pointer to video class stream instance.
  * @param  length: transfer length.
  * @retval none
  */
VOID USBD_VIDEO_StreamPayloadDone(UX_DEVICE_CLASS_VIDEO_STREAM *video_stream,
                                  ULONG length)
{
  /* USER CODE BEGIN USBD_VIDEO_StreamPayloadDone */
  UX_PARAMETER_NOT_USED(length);

  if (uvc_state != UVC_PLAY_STATUS_STOP)
  {
    uvc_state = UVC_PLAY_STATUS_STREAMING;
    video_write_payload(video_stream);
  }
  /* USER CODE END USBD_VIDEO_StreamPayloadDone */

  return;
}

/**
  * @brief  USBD_VIDEO_StreamRequest
  *         This function is invoked to manage the UVC class requests.
  * @param  video_stream: Pointer to video class stream instance.
  * @param  transfer: Pointer to the transfer request.
  * @retval status
  */
UINT USBD_VIDEO_StreamRequest(UX_DEVICE_CLASS_VIDEO_STREAM *video_stream,
                              UX_SLAVE_TRANSFER *transfer)
{
  UINT status  = UX_SUCCESS;

  /* USER CODE BEGIN USBD_VIDEO_StreamRequest */
  UCHAR *data, bRequest;
  USHORT wValue_CS, wLength;

  UX_PARAMETER_NOT_USED(video_stream);

  bRequest = transfer->ux_slave_transfer_request_setup[UX_SETUP_REQUEST];
  wValue_CS = transfer->ux_slave_transfer_request_setup[UX_SETUP_VALUE + 1];
  wLength = ux_utility_short_get(transfer->ux_slave_transfer_request_setup + UX_SETUP_LENGTH);
  data = transfer->ux_slave_transfer_request_data_pointer;

  switch (wValue_CS)
  {
    case UX_DEVICE_CLASS_VIDEO_VS_PROBE_CONTROL:

      switch (bRequest)
      {
        case UX_DEVICE_CLASS_VIDEO_SET_CUR:
          status = UX_SUCCESS;
          break;

        case UX_DEVICE_CLASS_VIDEO_GET_DEF:
        case UX_DEVICE_CLASS_VIDEO_GET_CUR:
        case UX_DEVICE_CLASS_VIDEO_GET_MIN:
        case UX_DEVICE_CLASS_VIDEO_GET_MAX:
          video_Probe_Control.bPreferedVersion = 0x00U;
          video_Probe_Control.bMinVersion = 0x00U;
          video_Probe_Control.bMaxVersion = 0x00U;
          video_Probe_Control.dwMaxVideoFrameSize = UVC_MAX_FRAME_SIZE;
          video_Probe_Control.dwClockFrequency = 0x02DC6C00U;
          video_Probe_Control.dwFrameInterval = USBD_VIDEO_GetFrameInterval100ns();
          video_Probe_Control.dwMaxPayloadTransferSize = USBD_VIDEO_GetMaxPayloadTransferSize();

          ux_utility_memory_copy(data, (VOID *)&video_Probe_Control, sizeof(USBD_VideoControlTypeDef));
          status = ux_device_stack_transfer_request(transfer,
                                                    UX_MIN(wLength, sizeof(USBD_VideoControlTypeDef)),
                                                    UX_MIN(wLength, sizeof(USBD_VideoControlTypeDef)));
          break;

        default:
          break;
      }
      break;

    case UX_DEVICE_CLASS_VIDEO_VS_COMMIT_CONTROL:

      if (wLength < 26U)
      {
        break;
      }

      switch (bRequest)
      {
        case UX_DEVICE_CLASS_VIDEO_SET_CUR:
          status = UX_SUCCESS;
          break;

        case UX_DEVICE_CLASS_VIDEO_GET_CUR:
          video_Commit_Control.dwMaxVideoFrameSize = UVC_MAX_FRAME_SIZE;
          video_Commit_Control.dwClockFrequency = 0x02DC6C00U;
          video_Commit_Control.dwFrameInterval = USBD_VIDEO_GetFrameInterval100ns();
          video_Commit_Control.dwMaxPayloadTransferSize = USBD_VIDEO_GetMaxPayloadTransferSize();

          ux_utility_memory_copy(data, (VOID *)&video_Commit_Control, 26U);
          status = ux_device_stack_transfer_request(transfer,
                                                    UX_MIN(wLength, sizeof(USBD_VideoControlTypeDef)),
                                                    UX_MIN(wLength, sizeof(USBD_VideoControlTypeDef)));
          break;

        default:
          break;
      }
      break;

    default:
      status = UX_ERROR;
      break;
  }
  /* USER CODE END USBD_VIDEO_StreamRequest */

  return status;
}

/**
  * @brief  USBD_VIDEO_StreamGetMaxPayloadBufferSize
  *         Get video stream max payload buffer size.
  * @param  none
  * @retval max payload
  */
ULONG USBD_VIDEO_StreamGetMaxPayloadBufferSize(VOID)
{
  ULONG max_playload = USBD_VIDEO_EPIN_HS_MPS;

  /* USER CODE BEGIN USBD_VIDEO_StreamGetMaxPayloadBufferSize */
  if (USBD_VIDEO_EPIN_FS_MPS > max_playload)
  {
    max_playload = USBD_VIDEO_EPIN_FS_MPS;
  }
  /* USER CODE END USBD_VIDEO_StreamGetMaxPayloadBufferSize */

  return max_playload;
}

/* USER CODE BEGIN 1 */

static UINT USBD_VIDEO_IsFullSpeed(void)
{
  if ((_ux_system_slave != UX_NULL) &&
      (_ux_system_slave->ux_system_slave_speed == UX_FULL_SPEED_DEVICE))
  {
    return 1U;
  }

  return 0U;
}

static ULONG USBD_VIDEO_GetFrameInterval100ns(void)
{
  return (USBD_VIDEO_IsFullSpeed() != 0U) ? UVC_FRAME_INTERVAL_FS : UVC_FRAME_INTERVAL_HS;
}

static ULONG USBD_VIDEO_GetFramePeriodMs(void)
{
  return (USBD_VIDEO_IsFullSpeed() != 0U) ? UVC_FRAME_PERIOD_FS_MS : UVC_FRAME_PERIOD_HS_MS;
}

static ULONG USBD_VIDEO_GetMaxPayloadTransferSize(void)
{
  return (USBD_VIDEO_IsFullSpeed() != 0U) ? USBD_VIDEO_EPIN_FS_MPS : USBD_VIDEO_EPIN_HS_MPS;
}

static VOID USBD_VIDEO_PaceFrameStart(void)
{
  ULONG now = ux_utility_time_get();
  ULONG frame_period_ms;

  if ((video_frame_pacer_armed != 0U) && (now < video_next_frame_tick))
  {
    ux_utility_delay_ms(video_next_frame_tick - now);
    now = ux_utility_time_get();
  }

  frame_period_ms = USBD_VIDEO_GetFramePeriodMs();
  video_next_frame_tick = now + frame_period_ms;
  video_frame_pacer_armed = 1U;
}

static VOID video_write_payload(UX_DEVICE_CLASS_VIDEO_STREAM *stream)
{
  ULONG buffer_length = 0UL;
  UCHAR *buffer = UX_NULL;
  ULONG endpoint_mps;
  ULONG length = 0UL;
  const video_frame_t *frame = UX_NULL;
  const uint8_t *frame_data = UX_NULL;
  uint32_t frame_size = 0U;
  uint32_t payload_capacity;
  uint32_t packets_per_frame = 0U;
  uint32_t payload_offset = 0U;
  uint32_t payload_length = 0U;
  uint32_t eof_packet = 0U;
  uint8_t header_info = video_fid;
  UINT status;

  if ((stream == UX_NULL) ||
      (stream->ux_device_class_video_stream_endpoint == UX_NULL) ||
      (uvc_state == UVC_PLAY_STATUS_STOP))
  {
    return;
  }

  endpoint_mps = stream->ux_device_class_video_stream_endpoint->ux_slave_endpoint_descriptor.wMaxPacketSize;
  if (endpoint_mps > sizeof(video_frame_buffer))
  {
    endpoint_mps = sizeof(video_frame_buffer);
  }

  payload_capacity = ((uint32_t)endpoint_mps > 2U) ? ((uint32_t)endpoint_mps - 2U) : 0U;

  status = ux_device_class_video_write_payload_get(stream, &buffer, &buffer_length);
  if (status != UX_SUCCESS)
  {
    return;
  }

  ux_utility_memory_set(video_frame_buffer, 0, endpoint_mps);

  if (payload_capacity == 0U)
  {
    length = 2U;
    header_info = (uint8_t)(video_fid | 0x02U);
  }
  else
  {
    if (video_packet_index == 0U)
    {
      bool repeated = false;

      USBD_VIDEO_PaceFrameStart();
      (void)video_source_prepare_next_frame(&repeated);
      video_fid ^= 0x01U;
    }

    frame = video_source_get_current_frame();
    if ((frame != UX_NULL) && (frame->data != UX_NULL) && (frame->size != 0U))
    {
      frame_data = frame->data;
      frame_size = frame->size;
    }

    if (frame_size == 0U)
    {
      length = 2U;
      header_info = (uint8_t)(video_fid | 0x02U);
    }
    else
    {
      packets_per_frame = (frame_size + payload_capacity - 1U) / payload_capacity;
      if (video_packet_index >= packets_per_frame)
      {
        video_packet_index = 0U;
      }

      payload_offset = video_packet_index * payload_capacity;
      payload_length = frame_size - payload_offset;
      if (payload_length > payload_capacity)
      {
        payload_length = payload_capacity;
      }

      header_info = video_fid;
      if ((payload_offset + payload_length) >= frame_size)
      {
        header_info = (uint8_t)(header_info | 0x02U);
        eof_packet = 1U;
      }

      length = payload_length + 2U;
      ux_utility_memory_copy((video_frame_buffer + 2U),
                             (VOID *)(frame_data + payload_offset),
                             payload_length);

      video_packet_index++;
      if (video_packet_index >= packets_per_frame)
      {
        video_packet_index = 0U;
        if (eof_packet != 0U)
        {
          video_frame_sequence++;
        }
      }
    }
  }

  video_frame_buffer[0] = 0x02U;
  video_frame_buffer[1] = header_info;

  if (length > buffer_length)
  {
    length = buffer_length;
  }

  ux_utility_memory_copy(buffer, video_frame_buffer, length);
  (void)ux_device_class_video_write_payload_commit(stream, length);
}

/* USER CODE END 1 */
