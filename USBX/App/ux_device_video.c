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
#include "app_usbx_device.h"
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
#define UVC_PLAY_STATUS_READY      0x01U
#define UVC_PLAY_STATUS_STREAMING  0x02U
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
ULONG uvc_state;
UX_DEVICE_CLASS_VIDEO *video;
UX_DEVICE_CLASS_VIDEO_STREAM *stream_write;

UCHAR video_frame_buffer[512];

static uint32_t video_packet_index = 0U;
static uint32_t video_frame_sequence = 0U;
static uint8_t video_fid = 1U;
static uint8_t video_resync_fid_valid = 0U;
static uint8_t video_resync_next_fid = 0U;
static uint32_t video_done_packets_in_frame = 0U;
static uint8_t video_frame_delay_pending = 0U;
static ULONG video_next_frame_tick = 0UL;
static uint8_t video_frame_pacer_armed = 0U;
static uint8_t video_frame_pacer_wait_logged = 0U;

volatile ULONG usbx_video_last_alt_dbg = 0UL;
volatile ULONG usbx_video_start_status_dbg = 0UL;
volatile ULONG usbx_video_write_calls_dbg = 0UL;
volatile ULONG usbx_video_get_status_dbg = 0UL;
volatile ULONG usbx_video_commit_status_dbg = 0UL;
volatile ULONG usbx_video_last_done_len_dbg = 0UL;
volatile ULONG usbx_video_last_payload_len_dbg = 0UL;
volatile ULONG usbx_video_last_buffer_len_dbg = 0UL;
volatile ULONG usbx_video_last_state_dbg = 0UL;
volatile ULONG usbx_video_img_count_dbg = 0UL;
volatile ULONG usbx_video_packet_index_dbg = 0UL;
volatile ULONG usbx_video_packets_per_frame_dbg = 0UL;
volatile ULONG usbx_video_frame_eof_dbg = 0UL;
volatile ULONG usbx_video_last_header_dbg = 0UL;
volatile ULONG usbx_video_last_offset_dbg = 0UL;
volatile ULONG usbx_video_last_frame_size_dbg = 0UL;
volatile ULONG usbx_video_get_fail_dbg = 0UL;
volatile ULONG usbx_video_commit_fail_dbg = 0UL;
volatile ULONG usbx_video_frame_interval_100ns_dbg = UVC_FRAME_INTERVAL_FS;
volatile ULONG usbx_video_stream_task_state_dbg = 0UL;
volatile ULONG usbx_video_stream_task_status_dbg = 0UL;
volatile ULONG usbx_video_stream_error_dbg = 0UL;
volatile ULONG usbx_video_stream_buffer_error_count_dbg = 0UL;
volatile ULONG usbx_video_stream_buffer_size_dbg = 0UL;
volatile ULONG usbx_video_stream_payload_buffer_size_dbg = 0UL;
volatile ULONG usbx_video_stream_transfer_pos_dbg = 0UL;
volatile ULONG usbx_video_stream_access_pos_dbg = 0UL;
volatile ULONG usbx_video_stream_transfer_len_dbg = 0UL;
volatile ULONG usbx_video_stream_access_len_dbg = 0UL;
volatile ULONG usbx_video_stream_endpoint_addr_dbg = 0UL;
volatile ULONG usbx_video_stream_endpoint_mps_dbg = 0UL;
volatile ULONG usbx_video_payload_flush_count_dbg = 0UL;
volatile ULONG usbx_video_payload_flush_slots_dbg = 0UL;
volatile ULONG usbx_video_payload_flush_nonzero_dbg = 0UL;
volatile ULONG usbx_video_payload_flush_bytes_dbg = 0UL;
volatile ULONG usbx_video_payload_flush_transfer_pos_dbg = 0UL;
volatile ULONG usbx_video_payload_flush_access_pos_dbg = 0UL;
volatile ULONG usbx_video_fid_dbg = 0UL;
volatile ULONG usbx_video_resync_next_fid_dbg = 0UL;
volatile ULONG usbx_video_resync_fid_action_dbg = 0UL;
volatile ULONG usbx_video_resync_seen_packets_dbg = 0UL;
volatile ULONG usbx_video_done_packets_in_frame_dbg = 0UL;
volatile ULONG usbx_video_last_done_header_dbg = 0UL;
volatile ULONG usbx_video_frame_delay_pending_dbg = 0UL;
volatile ULONG usbx_video_frame_delay_enable_dbg = 1UL;
volatile ULONG usbx_video_frame_delay_count_dbg = 0UL;
volatile ULONG usbx_video_frame_delay_skipped_dbg = 0UL;
volatile ULONG usbx_video_pace_hold_count_dbg = 0UL;
volatile ULONG usbx_video_pace_release_count_dbg = 0UL;
volatile ULONG usbx_video_pace_wait_ms_dbg = 0UL;
volatile ULONG usbx_video_pace_now_dbg = 0UL;
volatile ULONG usbx_video_pace_target_dbg = 0UL;
volatile ULONG usbx_video_resync_delay_cfg_ms_dbg = 0UL;
volatile ULONG usbx_video_resync_ll_recovery_cleared_dbg = 0UL;
volatile ULONG usbx_video_resync_clear_ll_recovery_on_empty_enable_dbg = 1UL;
volatile ULONG usbx_video_resync_ll_recovery_kept_dbg = 0UL;
volatile ULONG usbx_video_iso_recovery_fast_resync_enable_dbg = 0UL;
volatile ULONG usbx_video_iso_recovery_fast_resync_count_dbg = 0UL;
volatile ULONG usbx_video_iso_recovery_fast_resync_now_dbg = 0UL;
volatile ULONG usbx_video_iso_recovery_fast_resync_prev_target_dbg = 0UL;
volatile ULONG usbx_video_payload_fill_calls_dbg = 0UL;
volatile ULONG usbx_video_payload_fill_count_dbg = 0UL;
volatile ULONG usbx_video_payload_fill_full_dbg = 0UL;

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
static VOID USBD_VIDEO_CaptureStreamDebug(UX_DEVICE_CLASS_VIDEO_STREAM *stream);
static VOID USBD_VIDEO_FlushPayloadQueue(UX_DEVICE_CLASS_VIDEO_STREAM *stream);
static UINT USBD_VIDEO_GetPreviousPayloadHeader(UX_DEVICE_CLASS_VIDEO_STREAM *stream,
                                                UCHAR *header);
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

  /* Save the video instance */
  video = (UX_DEVICE_CLASS_VIDEO*) video_instance;

  /* Get the streams instances */
  ux_device_class_video_stream_get(video, 0, &stream_write);
  USBD_VIDEO_CaptureStreamDebug(stream_write);

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

  /* Reset the video instance */
  video = UX_NULL;

  /* Reset the video streams */
  stream_write = UX_NULL;

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
  usbx_video_stream_change_dbg++;
  usbx_video_last_alt_dbg = alternate_setting;
  USBD_VIDEO_CaptureStreamDebug(video_stream);
  USBX_TraceLog(USBX_TRACE_EVT_STREAM_CHANGE,
                alternate_setting,
                uvc_state,
                usbx_video_stream_change_dbg,
                usbx_video_stream_endpoint_addr_dbg,
                usbx_video_stream_endpoint_mps_dbg,
                usbx_video_stream_task_state_dbg,
                usbx_video_stream_task_status_dbg,
                usbx_video_write_calls_dbg,
                usbx_video_payload_done_dbg,
                0UL,
                0UL,
                0UL);

  /* Stop video payload loop back if stream closed */
  if (alternate_setting == 0U)
  {
    if (usbx_freeze_alt0_capture_enable_dbg != 0UL)
    {
      USBX_FreezeCaptureNow(USBX_FREEZE_REASON_VIDEO_ALT0);
    }

    /* Update State machine */
    uvc_state = UVC_PLAY_STATUS_STOP;

    video_frame_sequence = 0U;
    video_packet_index = 0U;
    video_fid = 1U;
    video_resync_fid_valid = 0U;
    video_resync_next_fid = 0U;
    video_done_packets_in_frame = 0U;
    video_frame_delay_pending = 0U;
    video_next_frame_tick = 0UL;
    video_frame_pacer_armed = 0U;
    video_frame_pacer_wait_logged = 0U;
    usbx_video_fid_dbg = video_fid;
    usbx_video_resync_next_fid_dbg = video_resync_next_fid;
    usbx_video_resync_fid_action_dbg = 0UL;
    usbx_video_resync_seen_packets_dbg = 0UL;
    usbx_video_done_packets_in_frame_dbg = video_done_packets_in_frame;
    usbx_video_frame_delay_pending_dbg = video_frame_delay_pending;
    usb_ll_iso_after_recovery_dbg = 0U;
    usbx_iisoixfr_recovery_retry_count_dbg = 0UL;
    USBD_VIDEO_CaptureStreamDebug(video_stream);

    return;
  }

  /* Start alt setting with real video payload, not a header-only priming packet. */
  uvc_state = UVC_PLAY_STATUS_STREAMING;
  video_fid = 1U;
  video_resync_fid_valid = 0U;
  video_resync_next_fid = 0U;
  video_done_packets_in_frame = 0U;
  video_frame_delay_pending = 0U;
  video_next_frame_tick = 0UL;
  video_frame_pacer_armed = 0U;
  video_frame_pacer_wait_logged = 0U;
  usbx_video_fid_dbg = video_fid;
  usbx_video_resync_next_fid_dbg = video_resync_next_fid;
  usbx_video_resync_fid_action_dbg = 0UL;
  usbx_video_resync_seen_packets_dbg = 0UL;
  usbx_video_done_packets_in_frame_dbg = video_done_packets_in_frame;
  usbx_video_frame_delay_pending_dbg = video_frame_delay_pending;
  usb_ll_iso_after_recovery_dbg = 0U;
  usbx_iisoixfr_recovery_retry_count_dbg = 0UL;
  video_packet_index = 0U;
  video_frame_sequence = 0U;
  usbx_video_frame_interval_100ns_dbg = USBD_VIDEO_GetFrameInterval100ns();

  /* Prime the standalone USBX payload queue like the STM example. */
  video_write_payload(video_stream);
  video_write_payload(video_stream);

  /* Start sending valid payloads in the Video class */
  usbx_video_start_status_dbg = ux_device_class_video_transmission_start(video_stream);
  USBD_VIDEO_CaptureStreamDebug(video_stream);
  USBX_TraceLog(USBX_TRACE_EVT_STREAM_START,
                usbx_video_start_status_dbg,
                uvc_state,
                usbx_video_stream_endpoint_addr_dbg,
                usbx_video_stream_endpoint_mps_dbg,
                usbx_video_stream_task_state_dbg,
                usbx_video_stream_task_status_dbg,
                usbx_video_stream_transfer_len_dbg,
                usbx_video_stream_access_len_dbg,
                usbx_video_write_calls_dbg,
                usbx_video_payload_done_dbg,
                0UL,
                0UL);

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
  usbx_video_payload_done_dbg++;
  usbx_video_last_done_len_dbg = length;
  USBD_VIDEO_CaptureStreamDebug(video_stream);
  USBX_TraceLog(USBX_TRACE_EVT_PAYLOAD_DONE,
                usbx_video_payload_done_dbg,
                length,
                uvc_state,
                usbx_video_stream_task_state_dbg,
                usbx_video_stream_task_status_dbg,
                usbx_video_stream_transfer_len_dbg,
                usbx_video_stream_access_len_dbg,
                usbx_video_stream_endpoint_addr_dbg,
                usbx_video_stream_endpoint_mps_dbg,
                usbx_video_write_calls_dbg,
                0UL,
                0UL);

  if (uvc_state != UVC_PLAY_STATUS_STOP)
  {
    if (length > 0UL)
    {
      UCHAR done_header = 0U;

      usbx_iisoixfr_recovery_retry_count_dbg = 0UL;

      if (USBD_VIDEO_GetPreviousPayloadHeader(video_stream, &done_header) == UX_SUCCESS)
      {
        usbx_video_last_done_header_dbg = done_header;

        if ((done_header & 0x02U) != 0U)
        {
          video_done_packets_in_frame = 0U;
        }
        else
        {
          video_done_packets_in_frame++;
        }

        usbx_video_done_packets_in_frame_dbg = video_done_packets_in_frame;
      }
    }

    if ((length == 0UL) && (usbx_video_iso_recovery_pending_dbg != 0UL))
    {
      ULONG seen_packets;

      usbx_video_iso_recovery_pending_dbg--;
      usbx_video_resync_count_dbg++;
      usbx_video_resync_delay_ms_dbg = usbx_video_resync_delay_cfg_ms_dbg;
      seen_packets = video_done_packets_in_frame;
      usbx_video_resync_seen_packets_dbg = seen_packets;

      if (seen_packets != 0UL)
      {
        video_resync_next_fid =
            (uint8_t)((usbx_video_last_done_header_dbg ^ 0x01UL) & 0x01UL);
        usbx_video_resync_fid_action_dbg = 2UL;
      }
      else
      {
        video_resync_next_fid = video_fid;
        usbx_video_resync_fid_action_dbg = 1UL;
        if (usbx_video_resync_clear_ll_recovery_on_empty_enable_dbg != 0UL)
        {
          usb_ll_iso_after_recovery_dbg = 0U;
          usbx_video_resync_ll_recovery_cleared_dbg++;
        }
        else
        {
          usbx_video_resync_ll_recovery_kept_dbg++;
        }
      }

      video_resync_fid_valid = 1U;
      video_done_packets_in_frame = 0U;
      video_frame_delay_pending = 1U;

      if (usbx_video_iso_recovery_fast_resync_enable_dbg != 0UL)
      {
        ULONG now = ux_utility_time_get();

        usbx_video_iso_recovery_fast_resync_count_dbg++;
        usbx_video_iso_recovery_fast_resync_now_dbg = now;
        usbx_video_iso_recovery_fast_resync_prev_target_dbg = video_next_frame_tick;
        video_next_frame_tick = now;
        video_frame_pacer_armed = 0U;
        video_frame_pacer_wait_logged = 0U;
        usbx_video_pace_target_dbg = video_next_frame_tick;
      }

      video_packet_index = 0U;
      video_frame_sequence++;

      usbx_video_packet_index_dbg = video_packet_index;
      usbx_video_img_count_dbg = video_frame_sequence;
      usbx_video_fid_dbg = video_fid;
      usbx_video_resync_next_fid_dbg = video_resync_next_fid;
      usbx_video_done_packets_in_frame_dbg = video_done_packets_in_frame;
      usbx_video_frame_delay_pending_dbg = video_frame_delay_pending;
      USBD_VIDEO_FlushPayloadQueue(video_stream);

      USBX_TraceLog(USBX_TRACE_EVT_VIDEO_RESYNC,
                    usbx_video_resync_count_dbg,
                    usbx_video_iso_recovery_pending_dbg,
                    length,
                    video_frame_sequence,
                    video_packet_index,
                    usbx_video_resync_delay_ms_dbg,
                    usbx_iisoixfr_recovery_calls_dbg,
                    usbx_video_payload_done_dbg,
                    usbx_video_write_calls_dbg,
                    usbx_video_resync_fid_action_dbg,
                    usbx_video_resync_next_fid_dbg,
                    seen_packets);

      if (usbx_video_resync_delay_ms_dbg != 0UL)
      {
        ux_utility_delay_ms(usbx_video_resync_delay_ms_dbg);
      }
    }

    /* Update state machine */
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

  /* Decode setup packet */
  bRequest = transfer -> ux_slave_transfer_request_setup[UX_SETUP_REQUEST];
  wValue_CS = transfer -> ux_slave_transfer_request_setup[UX_SETUP_VALUE + 1];
  wLength = ux_utility_short_get(transfer -> ux_slave_transfer_request_setup + UX_SETUP_LENGTH);
  data = transfer -> ux_slave_transfer_request_data_pointer;

  /* Check CS */
  switch(wValue_CS)
  {
    case UX_DEVICE_CLASS_VIDEO_VS_PROBE_CONTROL:

      switch(bRequest)
      {
        case UX_DEVICE_CLASS_VIDEO_SET_CUR:

          status = UX_SUCCESS;

          break;

        case UX_DEVICE_CLASS_VIDEO_GET_DEF:
        case UX_DEVICE_CLASS_VIDEO_GET_CUR:
        case UX_DEVICE_CLASS_VIDEO_GET_MIN:
        case UX_DEVICE_CLASS_VIDEO_GET_MAX:

          /* Update bPreferedVersion, bMinVersion and bMaxVersion which must be set only by Device */
          video_Probe_Control.bPreferedVersion = 0x00U;
          video_Probe_Control.bMinVersion = 0x00U;
          video_Probe_Control.bMaxVersion = 0x00U;
          video_Probe_Control.dwMaxVideoFrameSize = UVC_MAX_FRAME_SIZE;
          video_Probe_Control.dwClockFrequency = 0x02DC6C00U;
          video_Probe_Control.dwFrameInterval = USBD_VIDEO_GetFrameInterval100ns();
          video_Probe_Control.dwMaxPayloadTransferSize = USBD_VIDEO_GetMaxPayloadTransferSize();
          usbx_video_frame_interval_100ns_dbg = video_Probe_Control.dwFrameInterval;

          /* Copy data for transfer */
          ux_utility_memory_copy(data, (VOID *)&video_Probe_Control, sizeof(USBD_VideoControlTypeDef));

          /* Transfer request */
          status = ux_device_stack_transfer_request(transfer, UX_MIN(wLength, sizeof(USBD_VideoControlTypeDef)),
                                                    UX_MIN(wLength, sizeof(USBD_VideoControlTypeDef)));

          break;

        default:
          break;
      }
      break;

    case UX_DEVICE_CLASS_VIDEO_VS_COMMIT_CONTROL:

      /* Check request length */
      if (wLength < 26)
      {
        break;
      }

      switch(bRequest)
      {
        case UX_DEVICE_CLASS_VIDEO_SET_CUR:

          status = UX_SUCCESS;

          break;

        case UX_DEVICE_CLASS_VIDEO_GET_CUR:
          video_Commit_Control.dwMaxVideoFrameSize = UVC_MAX_FRAME_SIZE;
          video_Commit_Control.dwClockFrequency = 0x02DC6C00U;
          video_Commit_Control.dwFrameInterval = USBD_VIDEO_GetFrameInterval100ns();
          video_Commit_Control.dwMaxPayloadTransferSize = USBD_VIDEO_GetMaxPayloadTransferSize();
          usbx_video_frame_interval_100ns_dbg = video_Commit_Control.dwFrameInterval;

          /* Copy data for transfer */
          ux_utility_memory_copy(data, (VOID *)&video_Commit_Control, 26);

          /* Send transfer request */
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

static VOID USBD_VIDEO_CaptureStreamDebug(UX_DEVICE_CLASS_VIDEO_STREAM *stream)
{
  if (stream == UX_NULL)
  {
    usbx_video_stream_task_state_dbg = 0UL;
    usbx_video_stream_task_status_dbg = 0UL;
    usbx_video_stream_error_dbg = 0UL;
    usbx_video_stream_buffer_error_count_dbg = 0UL;
    usbx_video_stream_buffer_size_dbg = 0UL;
    usbx_video_stream_payload_buffer_size_dbg = 0UL;
    usbx_video_stream_transfer_pos_dbg = 0UL;
    usbx_video_stream_access_pos_dbg = 0UL;
    usbx_video_stream_transfer_len_dbg = 0UL;
    usbx_video_stream_access_len_dbg = 0UL;
    usbx_video_stream_endpoint_addr_dbg = 0UL;
    usbx_video_stream_endpoint_mps_dbg = 0UL;
    return;
  }

#if defined(UX_DEVICE_STANDALONE)
  usbx_video_stream_task_state_dbg = stream->ux_device_class_video_stream_task_state;
  usbx_video_stream_task_status_dbg = stream->ux_device_class_video_stream_task_status;
#else
  usbx_video_stream_task_state_dbg = 0xFFFFFFFFUL;
  usbx_video_stream_task_status_dbg = 0xFFFFFFFFUL;
#endif
  usbx_video_stream_error_dbg = stream->ux_device_class_video_stream_error;
  usbx_video_stream_buffer_error_count_dbg = stream->ux_device_class_video_stream_buffer_error_count;
  usbx_video_stream_buffer_size_dbg = stream->ux_device_class_video_stream_buffer_size;
  usbx_video_stream_payload_buffer_size_dbg = stream->ux_device_class_video_stream_payload_buffer_size;
  usbx_video_stream_transfer_pos_dbg = (ULONG)stream->ux_device_class_video_stream_transfer_pos;
  usbx_video_stream_access_pos_dbg = (ULONG)stream->ux_device_class_video_stream_access_pos;

  if (stream->ux_device_class_video_stream_transfer_pos != UX_NULL)
  {
    usbx_video_stream_transfer_len_dbg =
      stream->ux_device_class_video_stream_transfer_pos->ux_device_class_video_payload_length;
  }
  else
  {
    usbx_video_stream_transfer_len_dbg = 0UL;
  }

  if (stream->ux_device_class_video_stream_access_pos != UX_NULL)
  {
    usbx_video_stream_access_len_dbg =
      stream->ux_device_class_video_stream_access_pos->ux_device_class_video_payload_length;
  }
  else
  {
    usbx_video_stream_access_len_dbg = 0UL;
  }

  if (stream->ux_device_class_video_stream_endpoint != UX_NULL)
  {
    usbx_video_stream_endpoint_addr_dbg =
      stream->ux_device_class_video_stream_endpoint->ux_slave_endpoint_descriptor.bEndpointAddress;
    usbx_video_stream_endpoint_mps_dbg =
      stream->ux_device_class_video_stream_endpoint->ux_slave_endpoint_descriptor.wMaxPacketSize;
  }
  else
  {
    usbx_video_stream_endpoint_addr_dbg = 0UL;
    usbx_video_stream_endpoint_mps_dbg = 0UL;
  }
}

static VOID USBD_VIDEO_FlushPayloadQueue(UX_DEVICE_CLASS_VIDEO_STREAM *stream)
{
  UX_INTERRUPT_SAVE_AREA
  UCHAR *payload_buffer;
  UCHAR *payload_end;
  ULONG slots = 0UL;
  ULONG nonzero = 0UL;
  ULONG bytes = 0UL;

  if ((stream == UX_NULL) ||
      (stream->ux_device_class_video_stream_buffer == UX_NULL) ||
      (stream->ux_device_class_video_stream_buffer_size == 0UL) ||
      (stream->ux_device_class_video_stream_payload_buffer_size <= 4UL))
  {
    return;
  }

  UX_DISABLE

  payload_buffer = stream->ux_device_class_video_stream_buffer;
  payload_end = stream->ux_device_class_video_stream_buffer +
                stream->ux_device_class_video_stream_buffer_size;

  while (payload_buffer < payload_end)
  {
    UX_DEVICE_CLASS_VIDEO_PAYLOAD *payload =
        (UX_DEVICE_CLASS_VIDEO_PAYLOAD *)payload_buffer;

    if (payload->ux_device_class_video_payload_length != 0UL)
    {
      nonzero++;
      bytes += payload->ux_device_class_video_payload_length;
    }

    payload->ux_device_class_video_payload_length = 0UL;
    _ux_utility_memory_set(payload->ux_device_class_video_payload_data,
                           0,
                           stream->ux_device_class_video_stream_payload_buffer_size - 4UL);

    slots++;
    payload_buffer += stream->ux_device_class_video_stream_payload_buffer_size;
  }

  stream->ux_device_class_video_stream_transfer_pos =
      stream->ux_device_class_video_stream_access_pos;

  usbx_video_payload_flush_count_dbg++;
  usbx_video_payload_flush_slots_dbg = slots;
  usbx_video_payload_flush_nonzero_dbg = nonzero;
  usbx_video_payload_flush_bytes_dbg = bytes;
  usbx_video_payload_flush_transfer_pos_dbg =
      (ULONG)stream->ux_device_class_video_stream_transfer_pos;
  usbx_video_payload_flush_access_pos_dbg =
      (ULONG)stream->ux_device_class_video_stream_access_pos;

  UX_RESTORE

  USBD_VIDEO_CaptureStreamDebug(stream);
}

static UINT USBD_VIDEO_GetPreviousPayloadHeader(UX_DEVICE_CLASS_VIDEO_STREAM *stream,
                                                UCHAR *header)
{
  UCHAR *buffer;
  UCHAR *current;
  UCHAR *previous;
  ULONG payload_size;
  ULONG buffer_size;
  UX_DEVICE_CLASS_VIDEO_PAYLOAD *payload;

  if ((stream == UX_NULL) ||
      (header == UX_NULL) ||
      (stream->ux_device_class_video_stream_buffer == UX_NULL) ||
      (stream->ux_device_class_video_stream_transfer_pos == UX_NULL) ||
      (stream->ux_device_class_video_stream_payload_buffer_size <= 4UL))
  {
    return UX_ERROR;
  }

  buffer = stream->ux_device_class_video_stream_buffer;
  current = (UCHAR *)stream->ux_device_class_video_stream_transfer_pos;
  payload_size = stream->ux_device_class_video_stream_payload_buffer_size;
  buffer_size = stream->ux_device_class_video_stream_buffer_size;

  if ((current < buffer) || (current >= (buffer + buffer_size)))
  {
    return UX_ERROR;
  }

  if (current == buffer)
  {
    previous = buffer + buffer_size - payload_size;
  }
  else
  {
    previous = current - payload_size;
  }

  if ((previous < buffer) || ((previous + payload_size) > (buffer + buffer_size)))
  {
    return UX_ERROR;
  }

  payload = (UX_DEVICE_CLASS_VIDEO_PAYLOAD *)previous;
  *header = payload->ux_device_class_video_payload_data[1];

  return UX_SUCCESS;
}

/**
  * @brief  Pace the start of every frame like the STM sample path.
  * @retval None.
  */
static VOID USBD_VIDEO_PaceFrameStart(void)
{
  ULONG now;
  ULONG wait_ms;
  ULONG frame_period_ms;

  if (usbx_video_frame_delay_enable_dbg == 0UL)
  {
    if (video_frame_delay_pending != 0U)
    {
      video_frame_delay_pending = 0U;
      usbx_video_frame_delay_pending_dbg = video_frame_delay_pending;
      usbx_video_frame_delay_skipped_dbg++;
    }
    video_frame_pacer_wait_logged = 0U;
    return;
  }

  now = ux_utility_time_get();
  usbx_video_pace_now_dbg = now;
  usbx_video_pace_target_dbg = video_next_frame_tick;

  if ((video_frame_pacer_armed != 0U) && (now < video_next_frame_tick))
  {
    wait_ms = video_next_frame_tick - now;
    usbx_video_frame_delay_count_dbg++;
    usbx_video_pace_hold_count_dbg++;
    usbx_video_pace_wait_ms_dbg = wait_ms;

    if (video_frame_pacer_wait_logged == 0U)
    {
      video_frame_pacer_wait_logged = 1U;
      USBX_TraceLog(USBX_TRACE_EVT_VIDEO_PACE,
                    usbx_video_pace_hold_count_dbg,
                    wait_ms,
                    now,
                    video_next_frame_tick,
                    video_frame_delay_pending,
                    video_frame_sequence,
                    video_packet_index,
                    video_fid,
                    usbx_video_write_calls_dbg,
                    usbx_video_payload_done_dbg,
                    video_frame_pacer_armed,
                    0UL);
    }

    ux_utility_delay_ms(wait_ms);
    now = ux_utility_time_get();
    usbx_video_pace_now_dbg = now;
  }

  frame_period_ms = USBD_VIDEO_GetFramePeriodMs();
  video_next_frame_tick = now + frame_period_ms;
  video_frame_pacer_armed = 1U;
  video_frame_pacer_wait_logged = 0U;
  usbx_video_pace_release_count_dbg++;
  usbx_video_pace_wait_ms_dbg = 0UL;
  usbx_video_pace_target_dbg = video_next_frame_tick;

  video_frame_delay_pending = 0U;
  usbx_video_frame_delay_pending_dbg = video_frame_delay_pending;
}

/**
  * @brief  video_write_payload
            Manage the UVC data packets.
  * @param  stream : Video class stream instance.
  * @retval none
  */
VOID video_write_payload(UX_DEVICE_CLASS_VIDEO_STREAM *stream)
{
  ULONG buffer_length = 0UL;
  UCHAR *buffer = UX_NULL;
  ULONG usbd_video_ep_mps = stream->ux_device_class_video_stream_endpoint->ux_slave_endpoint_descriptor.wMaxPacketSize;
  ULONG length = 0UL;
  const video_frame_t *frame = UX_NULL;
  const uint8_t *frame_data = UX_NULL;
  uint32_t frame_size = 0U;
  uint32_t payload_capacity = ((uint32_t)usbd_video_ep_mps > 2U) ?
                              ((uint32_t)usbd_video_ep_mps - 2U) : 0U;
  uint32_t packets_per_frame = 0U;
  uint32_t payload_offset = 0U;
  uint32_t payload_length = 0U;
  uint32_t eof_packet = 0U;
  uint8_t header_info = video_fid;
  UINT status;

  usbx_video_write_calls_dbg++;
  usbx_video_last_state_dbg = uvc_state;
  USBD_VIDEO_CaptureStreamDebug(stream);

  /* Get payload buffer */
  status = ux_device_class_video_write_payload_get(stream, &buffer, &buffer_length);
  usbx_video_get_status_dbg = status;
  usbx_video_last_buffer_len_dbg = buffer_length;
  if (status != UX_SUCCESS)
  {
    usbx_video_get_fail_dbg++;
    USBX_TraceLog(USBX_TRACE_EVT_VIDEO_GETFAIL,
                  usbx_video_write_calls_dbg,
                  status,
                  uvc_state,
                  buffer_length,
                  usbx_video_stream_task_state_dbg,
                  usbx_video_stream_task_status_dbg,
                  usbx_video_stream_transfer_len_dbg,
                  usbx_video_stream_access_len_dbg,
                  usbx_video_payload_done_dbg,
                  0UL,
                  0UL,
                  0UL);
    return;
  }

  /* Check UVC state*/
  switch(uvc_state)
  {
    case UVC_PLAY_STATUS_READY:

      length = 2U;
      header_info = video_fid;

      break;

    case UVC_PLAY_STATUS_STREAMING:

      /* Reset video frame buffer */
      ux_utility_memory_set(video_frame_buffer, 0, usbd_video_ep_mps);

      if (payload_capacity == 0U)
      {
        length = 2U;
        header_info = (uint8_t)(video_fid | 0x02U);
      }
      else
      {
        if(video_packet_index == 0U)
        {
          bool repeated = false;

          USBD_VIDEO_PaceFrameStart();
          (void)video_source_prepare_next_frame(&repeated);

          if (video_resync_fid_valid != 0U)
          {
            video_fid = (uint8_t)(video_resync_next_fid & 0x01U);
            video_resync_fid_valid = 0U;
          }
          else
          {
            video_fid ^= 0x01U;
          }
          usbx_video_fid_dbg = video_fid;
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
            usbx_video_frame_eof_dbg++;
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
              USBX_TraceLog(USBX_TRACE_EVT_VIDEO_EOF,
                            usbx_video_write_calls_dbg,
                            length,
                            header_info,
                            video_frame_sequence,
                            video_packet_index,
                            packets_per_frame,
                            payload_offset,
                            payload_length,
                            frame_size,
                            usbx_video_payload_done_dbg,
                            buffer_length,
                            usbx_video_frame_eof_dbg);
            }

            video_frame_sequence++;
            usbx_video_frame_interval_100ns_dbg = USBD_VIDEO_GetFrameInterval100ns();
            video_frame_delay_pending = 1U;
            usbx_video_frame_delay_pending_dbg = video_frame_delay_pending;
          }
        }
      }
      break;

    case UVC_PLAY_STATUS_STOP:
    default:
      return;
  }

  /* Add the packet header */
  video_frame_buffer[0] = 0x02U;
  video_frame_buffer[1] = header_info;

  /* Copy video buffer in video frame buffer */
  if (length > buffer_length)
  {
    length = buffer_length;
  }
  ux_utility_memory_copy(buffer, video_frame_buffer, length);
  usbx_video_last_payload_len_dbg = length;
  usbx_video_img_count_dbg = video_frame_sequence;
  usbx_video_packet_index_dbg = video_packet_index;
  usbx_video_packets_per_frame_dbg = packets_per_frame;
  usbx_video_last_header_dbg = header_info;
  usbx_video_last_offset_dbg = payload_offset;
  usbx_video_last_frame_size_dbg = frame_size;

  /* Commit payload buffer */
  usbx_video_commit_status_dbg = ux_device_class_video_write_payload_commit(stream, length);
  if (usbx_video_commit_status_dbg != UX_SUCCESS)
  {
    usbx_video_commit_fail_dbg++;
  }
  USBX_TraceLog((usbx_video_commit_status_dbg != UX_SUCCESS) ?
                USBX_TRACE_EVT_VIDEO_CMTFAIL :
                USBX_TRACE_EVT_VIDEO_COMMIT,
                usbx_video_write_calls_dbg,
                usbx_video_commit_status_dbg,
                length,
                header_info,
                video_frame_sequence,
                video_packet_index,
                packets_per_frame,
                payload_offset,
                payload_length,
                frame_size,
                usbx_video_payload_done_dbg,
                buffer_length);
  USBD_VIDEO_CaptureStreamDebug(stream);
}

/* USER CODE END 1 */
