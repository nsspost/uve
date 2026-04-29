#include "usbd_uvc.h"
#include "usbd_uvc_desc.h"
#include "video_source.h"
#include "test_jpeg.h"
#include "usb_device.h"

#include "stm32h7xx.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"

#include <stdint.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O2")
#endif

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define UVC_FAST_CODE __attribute__((optimize("O2")))
#define UVC_UNUSED_CODE __attribute__((unused))
#else
#define UVC_FAST_CODE
#define UVC_UNUSED_CODE
#endif

#define UVC_SET_CUR                0x01U
#define UVC_GET_CUR                0x81U
#define UVC_GET_MIN                0x82U
#define UVC_GET_MAX                0x83U
#define UVC_GET_RES                0x84U
#define UVC_GET_LEN                0x85U
#define UVC_GET_INFO               0x86U
#define UVC_GET_DEF                0x87U

#define CS_DEVICE                  0x21U
#define UVC_VS_PROBE_CONTROL       0x0100U
#define UVC_VS_COMMIT_CONTROL      0x0200U

#define UVC_STATE_STOP             0U
#define UVC_STATE_READY            1U
#define UVC_STATE_STREAMING        2U

#define UVC_HEADER_SIZE            2U
#define UVC_PAYLOAD_FID            (1U << 0)
#define UVC_PAYLOAD_EOF            (1U << 1)
#define UVC_PAYLOAD_EOH            (1U << 7)
#define UVC_FS_PACKET              256U
#define UVC_EP0_CLASS_DESC_BUF_SIZE 192U
#define UVC_PACKET_INDEX_PREBUILT  0x8000U
#define UVC_PACKET_INDEX_MASK      0x7FFFU
#define UVC_PREPACKET_MAX_PACKETS  40U
#define UVC_FLUSH_POLICY_NONE      0U
#define UVC_FLUSH_POLICY_EVERY_TX  1U
#define UVC_FLUSH_POLICY_RECOVERY  2U
#define UVC_FLUSH_REASON_BEFORE_TX 1U
#define UVC_FLUSH_REASON_ALT       2U
#define UVC_FLUSH_REASON_ISO       3U
#define UVC_FLUSH_REASON_BUSY      4U

typedef struct __attribute__((packed))
{
    uint16_t bmHint;
    uint8_t  bFormatIndex;
    uint8_t  bFrameIndex;
    uint32_t dwFrameInterval;
    uint16_t wKeyFrameRate;
    uint16_t wPFrameRate;
    uint16_t wCompQuality;
    uint16_t wCompWindowSize;
    uint16_t wDelay;
    uint32_t dwMaxVideoFrameSize;
    uint32_t dwMaxPayloadTransferSize;
} uvc_probe_commit_t;

typedef struct
{
    uint32_t interface;
    uint32_t state;
    uint8_t ep0_buf[sizeof(uvc_probe_commit_t)];
    uint16_t pending_control;
} usbd_uvc_handle_t;

typedef struct
{
    uint8_t active;
    uint8_t wait_frame_interval;
    const uint8_t *frame_ptr;
    uint32_t frame_size;
    uint32_t packet_index;
    uint32_t full_packets;
    uint32_t remainder;
    uint32_t next_frame_tick;
    uint32_t next_keepalive_tick;
    uint32_t next_payload_tick;
    uint8_t last_packet;
} uvc_backend_state_t;

typedef struct
{
    uint8_t frame_active;
    uint8_t fid;
    uint8_t payload_run_count;
    const uint8_t *frame_ptr;
    uint32_t frame_size;
    uint32_t offset;
    uint32_t packet_index;
    uint32_t next_frame_tick;
} uvc_stm_stream_state_t;

typedef struct
{
    uint8_t valid;
    uint8_t fid;
    uint8_t is_last;
    const uint8_t *frame_ptr;
    uint32_t frame_size;
    uint32_t offset;
    uint32_t packet_index;
    uint16_t chunk;
} uvc_stm_payload_checkpoint_t;

typedef struct
{
    uint8_t valid;
    const uint8_t *frame_ptr;
    uint32_t frame_size;
    uint16_t first_packet;
    uint16_t packet_count;
} uvc_ram_frame_packets_t;

typedef struct
{
    uint8_t streaming_enabled;
    uint8_t ep_busy;
    uint8_t drop_current_frame;
    uint8_t fid;
    uint8_t last_packet_was_eof;
    uint8_t frame_active;
    const uint8_t *frame_ptr;
    uint32_t frame_size;
    uint32_t offset;
    uint32_t next_frame_tick;
} uvc_runtime_state_t;

extern USBD_HandleTypeDef hUsbDeviceHS;

static uint8_t USBD_UVC_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_UVC_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_UVC_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_UVC_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_UVC_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum) UVC_FAST_CODE;
static uint8_t USBD_UVC_SOF(USBD_HandleTypeDef *pdev);
static uint8_t USBD_UVC_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t *USBD_UVC_GetHSConfigDescriptor(uint16_t *length);
static uint8_t *USBD_UVC_GetFSConfigDescriptor(uint16_t *length);
static uint8_t *USBD_UVC_GetOtherSpeedConfigDescriptor(uint16_t *length);
static uint8_t *USBD_UVC_GetDeviceQualifierDescriptor(uint16_t *length);

static int8_t UVC_Itf_Init(void);
static int8_t UVC_Itf_DeInit(void);
static int8_t UVC_Itf_Start(void);
static int8_t UVC_Itf_Stop(void);
static int8_t UVC_Itf_Control(const void *ctrl);
static int8_t UVC_Itf_Data(uint8_t **pbuf, uint16_t *psize, uint16_t *pcktidx);
static int8_t UVC_Itf_DataMinimal(uint8_t **pbuf, uint16_t *psize, uint16_t *pcktidx);
static uint8_t UVC_EnsureRamPackets(uint16_t packet_size);
static const uvc_ram_frame_packets_t *UVC_FindRamPackets(const uint8_t *frame_ptr, uint32_t frame_size);

static void UVC_InitDefaultProbeCommit(void);
static void UVC_NormalizeProbeCommit(uvc_probe_commit_t *pc, USBD_SpeedTypeDef speed);
static void UVC_DebugProbeCommit(void);
static void UVC_ResetBackendForRestart(usbd_uvc_handle_t *huvc) UVC_UNUSED_CODE;
static void UVC_DropFrameOnIsoIncomplete(usbd_uvc_handle_t *huvc);
static void UVC_RuntimePublish(void);
static void UVC_RuntimeReset(uint8_t streaming_enabled);
static void UVC_RuntimeDropCurrentFrame(void);
static void UVC_RuntimeFinishFrame(void);
static uint8_t UVC_PrimeNextPacket(USBD_HandleTypeDef *pdev) UVC_FAST_CODE;
static void UVC_CleanTxBuffer(uint8_t *packet, uint16_t packet_size) UVC_FAST_CODE;
static USBD_StatusTypeDef UVC_FlushStreamEP(USBD_HandleTypeDef *pdev, uint32_t reason);
static uint16_t UVC_GetPacketSize(USBD_SpeedTypeDef speed);
static uint16_t UVC_GetPayloadTransferSize(USBD_SpeedTypeDef speed);
static uint8_t UVC_PreparePrepackets(const uint8_t *frame_ptr, uint32_t frame_size, uint16_t packet_size);
static void UVC_STM_ResetStream(void);
static uint8_t UVC_STM_SubmitPacket(USBD_HandleTypeDef *pdev, uint8_t epnum,
                                    const uint8_t *payload, uint16_t payload_len,
                                    uint8_t header_info) UVC_FAST_CODE;
static void UVC_RecordBackendPayloadCheckpoint(const uint8_t *payload, uint16_t packet_size,
                                               uint16_t packet_index, uint8_t fid) UVC_FAST_CODE;
static uint8_t UVC_ShouldHoldPayloadForMicroframe7(void) UVC_FAST_CODE;
static uint8_t UVC_STM_SendStartPayload(USBD_HandleTypeDef *pdev, uint8_t epnum) UVC_FAST_CODE UVC_UNUSED_CODE;
static void UVC_STM_FinishFrame(void) UVC_FAST_CODE;
static uint8_t UVC_STM_SendNextPacket(USBD_HandleTypeDef *pdev, uint8_t epnum) UVC_FAST_CODE UVC_UNUSED_CODE;
static uint8_t UVC_STM_ExampleDataIn(USBD_HandleTypeDef *pdev, uint8_t epnum) UVC_FAST_CODE;
static uint32_t UVC_DWT_Begin(void) UVC_FAST_CODE;
static void UVC_DWT_Record(volatile uint32_t *last, volatile uint32_t *max, uint32_t start) UVC_FAST_CODE;
static uint8_t UVC_SendPayload(USBD_HandleTypeDef *pdev, uint8_t epnum, const uint8_t *payload, uint16_t packet_size,
                               uint16_t packet_index);
static uint8_t UVC_SendPreparedPacket(USBD_HandleTypeDef *pdev, uint8_t epnum, uint8_t *packet, uint16_t packet_size) UVC_FAST_CODE;
static uint8_t UVC_SendSelectedPayload(USBD_HandleTypeDef *pdev, uint8_t epnum, uint8_t *payload,
                                       uint16_t packet_size, uint16_t packet_index);
static usbd_uvc_handle_t *UVC_GetHandle(USBD_HandleTypeDef *pdev);
static USBD_UVC_ItfTypeDef *UVC_GetItf(USBD_HandleTypeDef *pdev);

USBD_ClassTypeDef USBD_UVC =
{
    .Init = USBD_UVC_Init,
    .DeInit = USBD_UVC_DeInit,
    .Setup = USBD_UVC_Setup,
    .EP0_TxSent = NULL,
    .EP0_RxReady = USBD_UVC_EP0_RxReady,
    .DataIn = USBD_UVC_DataIn,
    .DataOut = NULL,
    .SOF = USBD_UVC_SOF,
    .IsoINIncomplete = USBD_UVC_IsoINIncomplete,
    .IsoOUTIncomplete = NULL,
    .GetHSConfigDescriptor = USBD_UVC_GetHSConfigDescriptor,
    .GetFSConfigDescriptor = USBD_UVC_GetFSConfigDescriptor,
    .GetOtherSpeedConfigDescriptor = USBD_UVC_GetOtherSpeedConfigDescriptor,
    .GetDeviceQualifierDescriptor = USBD_UVC_GetDeviceQualifierDescriptor
};

static USBD_UVC_ItfTypeDef uvc_default_itf =
{
    .Init = UVC_Itf_Init,
    .DeInit = UVC_Itf_DeInit,
    .Start = UVC_Itf_Start,
    .Stop = UVC_Itf_Stop,
    .Control = UVC_Itf_Control,
    .Data = UVC_Itf_Data
};

static uvc_backend_state_t backend_state;
static uvc_stm_stream_state_t uvc_stm_state;
static uvc_stm_payload_checkpoint_t uvc_stm_last_payload;
static uint8_t uvc_tx_packet[UVC_IN_PACKET + UVC_HEADER_SIZE] __attribute__((aligned(32)));
static uvc_runtime_state_t uvc_runtime_state;
static uint8_t uvc_runtime_tx_packet[UVC_IN_PACKET] __attribute__((aligned(32)));
static uint8_t uvc_stm_sof_start_packet[UVC_HEADER_SIZE] __attribute__((aligned(32), unused)) = {0x02U, 0x00U};
static uint8_t uvc_prepacket_buf[UVC_PREPACKET_MAX_PACKETS][UVC_IN_PACKET] __attribute__((aligned(32)));
static uint16_t uvc_prepacket_len[UVC_PREPACKET_MAX_PACKETS] __attribute__((aligned(32)));
static uint32_t uvc_prepacket_offset[UVC_PREPACKET_MAX_PACKETS] __attribute__((aligned(32)));
static const uint8_t *uvc_prepacket_frame_ptr = NULL;
static uint32_t uvc_prepacket_frame_size = 0U;
static uint16_t uvc_prepacket_packet_size = 0U;
static uint16_t uvc_prepacket_count = 0U;
static uvc_ram_frame_packets_t uvc_ram_frames[2];
static const uvc_ram_frame_packets_t *uvc_ram_active_frame = NULL;
static uint16_t uvc_ram_packet_size = 0U;
static uint8_t uvc_ram_packets_ready = 0U;
static usbd_uvc_handle_t uvc_class_handle __attribute__((aligned(32)));
static uint8_t ep0_info_buf[2] __attribute__((aligned(32)));
static uint8_t ep0_probe_buf[sizeof(uvc_probe_commit_t)] __attribute__((aligned(32)));
static uint8_t ep0_class_desc_buf[UVC_EP0_CLASS_DESC_BUF_SIZE] __attribute__((aligned(32)));
static uint16_t ep0_status_buf __attribute__((aligned(32)));
static uint16_t ep0_len_buf __attribute__((aligned(32)));
static uvc_probe_commit_t uvc_probe __attribute__((aligned(32)));
static uvc_probe_commit_t uvc_commit __attribute__((aligned(32)));
static uvc_probe_commit_t uvc_res __attribute__((aligned(32)));
static uint8_t uvc_payload_header[2] = {0x02U, 0x00U};
static uint8_t current_alt_setting = 0U;
static USBD_UVC_ItfTypeDef *uvc_fops = &uvc_default_itf;

volatile uvc_runtime_diag_t uvc_runtime_dbg = {0};
volatile uvc_runtime_watch_t uvc_watch = {0};
volatile uint32_t uvc_runtime_busy_timeout_ms = 2U;
volatile uint32_t uvc_runtime_flush_before_tx_enable = 0U;
volatile uint32_t uvc_runtime_flush_policy = UVC_FLUSH_POLICY_RECOVERY;
volatile uint32_t uvc_runtime_flush_on_iso_enable = 1U;
volatile uint32_t uvc_runtime_no_frame_gap_enable = 0U;
volatile uint32_t uvc_runtime_idle_header_only_enable = 0U;
volatile uint32_t uvc_runtime_idle_packet_mode = UVC_IDLE_PACKET_ZLP;
volatile uint32_t uvc_runtime_idle_gap_skips = 0U;
volatile uint32_t uvc_runtime_idle_iso_skips = 0U;
volatile uint32_t uvc_runtime_idle_zlp_packets = 0U;

volatile uint32_t uvc_ll_tx_calls = 0;
volatile uint32_t uvc_ll_tx_ok = 0;
volatile uint32_t uvc_ll_tx_ret_hpcd_null = 0;
volatile uint32_t uvc_ll_tx_ret_ep_closed = 0;
volatile uint32_t uvc_ll_tx_ret_maxpacket0 = 0;
volatile uint32_t uvc_ll_tx_ret_epena = 0;
volatile uint32_t uvc_ll_tx_ret_busy = 0;
volatile uint32_t uvc_ll_tx_last_len = 0;
volatile uint32_t uvc_ll_tx_ok_len = 0;
volatile uint32_t uvc_ll_tx_last_epnum = 0;
volatile uint32_t uvc_ll_tx_last_status = 0;
volatile uint32_t uvc_ll_tx_last_diepctl = 0;
volatile uint32_t uvc_ll_tx_last_dieptsiz = 0;
volatile uint32_t uvc_ll_tx_last_diepint = 0;
volatile uint32_t uvc_class_last_tx_submit_tick = 0;
volatile uint32_t uvc_class_watchdog_recoveries = 0;
volatile uint32_t uvc_class_watchdog_age = 0;
volatile uint32_t uvc_class_watchdog_state = 0;
volatile uint32_t uvc_class_watchdog_diepctl = 0;
volatile uint32_t uvc_class_watchdog_dieptsiz = 0;
volatile uint32_t uvc_class_watchdog_diepint = 0;
volatile uint32_t uvc_ll_reopen_calls = 0;
volatile uint32_t uvc_ll_reopen_ok = 0;
volatile uint32_t uvc_ll_reopen_close_status = 0xFFFFFFFFU;
volatile uint32_t uvc_ll_reopen_open_status = 0xFFFFFFFFU;
volatile uint32_t uvc_ll_reopen_abort_status = 0xFFFFFFFFU;
volatile uint32_t uvc_ll_tx_epena_abort_enable = 0;
volatile uint32_t uvc_ll_tx_epena_abort_calls = 0;
volatile uint32_t uvc_ll_tx_epena_abort_status = 0xFFFFFFFFU;
volatile uint32_t uvc_ll_tx_epena_flush_status = 0xFFFFFFFFU;
volatile uint32_t uvc_ll_reopen_wait_epena_loops = 0;
volatile uint32_t uvc_ll_reopen_wait_epena_timeout = 0;
volatile uint32_t uvc_ll_reopen_wait_epena_max_loops = 1000;
volatile uint32_t uvc_ll_reopen_after_close_diepctl = 0;
volatile uint32_t uvc_ll_reopen_before_open_diepctl = 0;

volatile uint32_t uvc_datain_calls = 0;
volatile uint32_t uvc_datain_last_ep = 0;
volatile uint32_t uvc_set_probe_calls = 0;
volatile uint32_t uvc_set_commit_calls = 0;
volatile uint32_t uvc_get_interface_calls = 0;
volatile uint32_t uvc_set_interface_calls = 0;
volatile uint32_t uvc_set_interface_alt0_calls = 0;
volatile uint32_t uvc_set_interface_alt1_calls = 0;
volatile uint32_t uvc_last_set_interface_alt = 0;
volatile uint32_t uvc_get_interface_last_if = 0;
volatile uint32_t uvc_get_interface_last_value = 0;
volatile uint32_t uvc_set_interface_last_if = 0;
volatile uint32_t uvc_last_setup_bmrequest = 0;
volatile uint32_t uvc_last_setup_brequest = 0;
volatile uint32_t uvc_last_setup_wvalue = 0;
volatile uint32_t uvc_last_setup_windex = 0;
volatile uint32_t uvc_last_setup_wlength = 0;
volatile uint32_t uvc_last_vs_control = 0;
volatile uint32_t uvc_get_cur_probe_calls = 0;
volatile uint32_t uvc_get_cur_commit_calls = 0;
volatile uint32_t uvc_get_min_calls = 0;
volatile uint32_t uvc_get_max_calls = 0;
volatile uint32_t uvc_get_def_calls = 0;
volatile uint32_t uvc_get_res_calls = 0;
volatile uint32_t uvc_get_len_calls = 0;
volatile uint32_t uvc_get_info_calls = 0;
volatile uint32_t uvc_last_set_cur_wlength = 0;
volatile uint32_t uvc_probe_dbg_interval = 0;
volatile uint32_t uvc_probe_dbg_max_frame = 0;
volatile uint32_t uvc_probe_dbg_max_payload = 0;
volatile uint32_t uvc_commit_dbg_interval = 0;
volatile uint32_t uvc_commit_dbg_max_frame = 0;
volatile uint32_t uvc_commit_dbg_max_payload = 0;
volatile uint32_t uvc_commit_dbg_format = 0;
volatile uint32_t uvc_commit_dbg_frame = 0;
volatile uint32_t uvc_initial_process_calls = 0;
volatile uint32_t uvc_sof_start_calls = 0;
volatile uint32_t uvc_sof_start_process_calls = 0;
volatile uint32_t uvc_sof_idle_restart_calls = 0;
volatile uint32_t uvc_sof_idle_restart_ok = 0;
volatile uint32_t uvc_pending_stream_start_dbg = 0;
volatile uint32_t uvc_stream_ep_open_dbg = 1;
volatile uint32_t uvc_try_start_calls = 0;
volatile uint32_t uvc_try_start_no_pending = 0;
volatile uint32_t uvc_try_start_ep_closed = 0;
volatile uint32_t uvc_try_start_hpcd_null = 0;
volatile uint32_t uvc_try_start_maxpacket0 = 0;
volatile uint32_t uvc_try_start_already_active = 0;
volatile uint32_t uvc_class_iso_retry_enable = 0;
volatile uint32_t uvc_class_iso_incomplete_calls = 0;
volatile uint32_t uvc_class_iso_incomplete_retry_ok = 0;
volatile uint32_t uvc_class_iso_incomplete_retry_disabled = 0;
volatile uint32_t uvc_class_iso_drop_enable = 1;
volatile uint32_t uvc_class_iso_frame_drops = 0;
volatile uint32_t uvc_class_iso_drop_last_state = 0;
volatile uint32_t uvc_class_iso_drop_last_tick = 0;
volatile uint32_t uvc_class_iso_drop_last_offset = 0;
volatile uint32_t uvc_class_iso_drop_last_frame_size = 0;
volatile uint32_t uvc_class_iso_drop_last_backend_packet_index = 0;
volatile uint32_t uvc_class_iso_drop_last_stm_packet_index = 0;
volatile uint32_t uvc_class_iso_requeue_enable = 0;
volatile uint32_t uvc_class_iso_requeue_calls = 0;
volatile uint32_t uvc_class_iso_requeue_ok = 0;
volatile uint32_t uvc_class_iso_requeue_fail = 0;
volatile uint32_t uvc_class_iso_requeue_last_status = 0;
volatile uint32_t uvc_class_iso_incomplete_no_active_frame = 0;
volatile uint32_t uvc_class_iso_no_active_resync_enable = 0;
volatile uint32_t uvc_class_iso_no_active_resync = 0;
volatile uint32_t uvc_datain_active_calls = 0;
volatile uint32_t uvc_datain_process_calls = 0;
volatile uint32_t uvc_datain_inactive_calls = 0;
volatile uint32_t uvc_usb_reg_snapshot_enable = 0;
volatile uint32_t uvc_fast_path_debug_enable = 0;
volatile uint32_t uvc_dwt_cycle_enable = 1;
volatile uint32_t uvc_dwt_cycle_started = 0;
volatile uint32_t uvc_datain_cycles_last = 0;
volatile uint32_t uvc_datain_cycles_max = 0;
volatile uint32_t uvc_lltx_cycles_last = 0;
volatile uint32_t uvc_lltx_cycles_max = 0;
volatile uint32_t uvc_payload_packet_bytes = UVC_IN_PACKET;
volatile uint32_t uvc_payload_packet_bytes_dbg = 0;
volatile uint32_t uvc_payload_packet_interval_ms = 0;
volatile uint32_t uvc_payload_pace_skips = 0;
volatile uint32_t uvc_payload_next_tick_dbg = 0;
volatile uint32_t uvc_prepacket_enable = 0;
volatile uint32_t uvc_prepacket_builds = 0;
volatile uint32_t uvc_prepacket_hits = 0;
volatile uint32_t uvc_prepacket_fallbacks = 0;
volatile uint32_t uvc_prepacket_count_dbg = 0;
volatile uint32_t uvc_prepacket_last_idx = 0;
volatile uint32_t uvc_prepacket_last_packet_size = 0;
volatile uint32_t uvc_prepacket_last_frame_size = 0;
volatile uint32_t uvc_ram_packet_enable = 1;
volatile uint32_t uvc_ram_packet_ready_dbg = 0;
volatile uint32_t uvc_ram_packet_builds = 0;
volatile uint32_t uvc_ram_packet_hits = 0;
volatile uint32_t uvc_ram_packet_direct_tx = 0;
volatile uint32_t uvc_ram_packet_fallbacks = 0;
volatile uint32_t uvc_ram_packet_total_packets = 0;
volatile uint32_t uvc_ram_packet_last_frame = 0;
volatile uint32_t uvc_ram_packet_last_idx = 0;
volatile uint32_t uvc_ram_packet_last_count = 0;
volatile uint32_t uvc_ram_packet_last_size = 0;
volatile uint32_t uvc_start_payload_enable = 0;
volatile uint32_t uvc_start_sof_delay_cfg = 0;
volatile uint32_t uvc_start_sof_delay_skips = 0;
volatile uint32_t uvc_start_sof_delay_live = 0;
volatile uint32_t uvc_class_frame_done_no_tx = 0;
volatile uint32_t uvc_class_next_frame_tick_dbg = 0;
volatile uint32_t uvc_stm_like_enable = 1;
volatile uint32_t uvc_stm_header_eoh_enable = 0;
volatile uint32_t uvc_stm_header_eof_enable = 1;
volatile uint32_t uvc_stm_sof_start_calls = 0;
volatile uint32_t uvc_stm_sof_direct_payload_enable = 1;
volatile uint32_t uvc_stm_sof_direct_payload_calls = 0;
volatile uint32_t uvc_stm_sof_direct_payload_ok = 0;
volatile uint32_t uvc_stm_sof_direct_payload_fail = 0;
volatile uint32_t uvc_stm_datain_calls = 0;
volatile uint32_t uvc_stm_exact_datain_calls = 0;
volatile uint32_t uvc_stm_exact_last_size = 0;
volatile uint32_t uvc_stm_exact_last_index = 0;
volatile uint32_t uvc_stm_exact_short_packets = 0;
volatile uint32_t uvc_stm_header_only_packets = 0;
volatile uint32_t uvc_stm_no_tx_packets = 0;
volatile uint32_t uvc_stm_sof_wait_pending_calls = 0;
volatile uint32_t uvc_stm_frame_loads = 0;
volatile uint32_t uvc_stm_frame_done = 0;
volatile uint32_t uvc_stm_no_frame = 0;
volatile uint32_t uvc_stm_pace_waits = 0;
volatile uint32_t uvc_stm_last_packet_index = 0;
volatile uint32_t uvc_stm_last_offset = 0;
volatile uint32_t uvc_stm_last_chunk = 0;
volatile uint32_t uvc_stm_last_len = 0;
volatile uint32_t uvc_stm_last_header = 0;
volatile uint32_t uvc_stm_last_status = 0;
volatile uint32_t uvc_stm_iso_replay_enable = 0;
volatile uint32_t uvc_stm_iso_replay_requests = 0;
volatile uint32_t uvc_stm_iso_replay_applied = 0;
volatile uint32_t uvc_stm_iso_replay_no_payload = 0;
volatile uint32_t uvc_stm_iso_replay_disabled = 0;
volatile uint32_t uvc_stm_iso_replay_invalid = 0;
volatile uint32_t uvc_stm_iso_replay_null_frame = 0;
volatile uint32_t uvc_stm_iso_replay_zero_size = 0;
volatile uint32_t uvc_stm_iso_replay_last_offset = 0;
volatile uint32_t uvc_stm_iso_replay_last_packet_index = 0;
volatile uint32_t uvc_stm_iso_replay_last_chunk = 0;
volatile uint32_t uvc_keepalive_tx_calls = 0;
volatile uint32_t uvc_keepalive_tx_ok = 0;
volatile uint32_t uvc_keepalive_tx_fail = 0;
volatile uint32_t uvc_keepalive_last_status = 0;
volatile uint32_t uvc_keepalive_last_tick = 0;
volatile uint32_t uvc_keepalive_last_state = 0;
volatile uint32_t uvc_keepalive_last_next_frame_tick = 0;
volatile uint32_t uvc_keepalive_last_diepctl = 0;
volatile uint32_t uvc_keepalive_last_dieptsiz = 0;
volatile uint32_t uvc_keepalive_last_diepint = 0;
volatile uint32_t uvc_iso_no_active_wait_interval = 0;
volatile uint32_t uvc_iso_no_active_last_tick = 0;
volatile uint32_t uvc_iso_no_active_last_state = 0;
volatile uint32_t uvc_iso_no_active_last_next_frame_tick = 0;
volatile uint32_t uvc_iso_no_active_last_diepctl = 0;
volatile uint32_t uvc_iso_no_active_last_dieptsiz = 0;
volatile uint32_t uvc_iso_no_active_last_diepint = 0;
volatile uint32_t uvc_min_source_enable = 1;
volatile uint32_t uvc_min_usb_tx_complete = 0;
volatile uint32_t uvc_min_data_calls = 0;
volatile uint32_t uvc_min_header_only_packets = 0;
volatile uint32_t uvc_min_no_pending_frames = 0;
volatile uint32_t uvc_min_no_tx_wait_pending = 0;
volatile uint32_t uvc_min_repeat_last_frames = 0;
volatile uint32_t uvc_min_frame_loads = 0;
volatile uint32_t uvc_min_frame_done = 0;
volatile uint32_t uvc_min_last_packet_index = 0;
volatile uint32_t uvc_min_last_offset = 0;
volatile uint32_t uvc_min_last_frame_size = 0;
volatile uint32_t uvc_min_last_packet_size = 0;
volatile uint32_t uvc_stm_payload_gap_enable = 0;
volatile uint32_t uvc_stm_payload_gap_after = 1;
volatile uint32_t uvc_stm_payload_gap_packets = 0;
volatile uint32_t uvc_skip_mf7_payload_enable = 0;
volatile uint32_t uvc_skip_payload_mf_mask = 0x0CU;
volatile uint32_t uvc_skip_mf7_payload_packets = 0;
volatile uint32_t uvc_skip_mf7_last_fnsof = 0;
volatile uint32_t uvc_skip_mf7_last_mf = 0;
volatile uint32_t uvc_skip_mf7_last_offset = 0;
volatile uint32_t uvc_skip_mf7_last_packet_index = 0;

extern volatile uint32_t uvc_packets_sent;
extern volatile uint32_t uvc_frames_sent;
extern volatile uint32_t uvc_current_frame_size_dbg;
extern volatile const uint8_t *uvc_current_frame_ptr_dbg;
extern volatile uint32_t uvc_tx_complete_calls;
extern volatile uint32_t uvc_stream_start_calls;
extern volatile uint32_t uvc_stream_start_no_frame;
extern volatile uint32_t uvc_start_payload_calls;
extern volatile uint32_t uvc_start_payload_ok;
extern volatile uint32_t uvc_start_payload_fail;
extern volatile uint32_t uvc_dbg_payload_send_calls;
extern volatile uint32_t uvc_dbg_payload_send_ok;
extern volatile uint32_t uvc_dbg_payload_send_fail;
extern volatile uint32_t uvc_dbg_payload_last_offset;
extern volatile uint32_t uvc_dbg_payload_last_data_len;
extern volatile uint32_t uvc_dbg_payload_last_total_len;
extern volatile uint32_t uvc_dbg_payload_last_eof;
extern volatile uint32_t uvc_last_frame_size;
extern volatile uint32_t uvc_last_offset;
extern volatile uint32_t uvc_frame_integrity_ok;
extern volatile uint32_t uvc_frame_integrity_fail;
extern volatile uint32_t uvc_frame_interval_ms;
extern volatile uint32_t uvc_frame_interval_releases;
extern volatile uint32_t uvc_frame_pace_skips;
extern volatile uint32_t uvc_frame_keepalive_enable;
extern volatile uint32_t uvc_frame_keepalive_packets;
extern volatile uint32_t uvc_frame_keepalive_interval_ms;
extern volatile uint32_t uvc_dbg_wait_frame_interval;
extern volatile uint32_t uvc_last_tx_complete_tick;
extern volatile uint32_t uvc_tx_watchdog_timeout_ms;
extern volatile uint32_t uvc_tx_watchdog_recoveries;
extern volatile uint32_t uvc_tx_watchdog_reopen_ok;
extern volatile uint8_t uvc_last_header_b0;
extern volatile uint8_t uvc_last_header_b1;
extern volatile uint8_t uvc_first_payload_b0;
extern volatile uint8_t uvc_first_payload_b1;
extern volatile uint8_t uvc_first_payload_b2;
extern volatile uint8_t uvc_first_payload_b3;
extern volatile uint8_t uvc_last_payload_b0;
extern volatile uint8_t uvc_last_payload_b1;
extern volatile uint8_t uvc_last_payload_b2;
extern volatile uint8_t uvc_last_payload_b3;
extern volatile uint32_t uvc_tx_in_flight_dbg;
extern volatile uint32_t uvc_dbg_state_live;

uint8_t USBD_UVC_RegisterInterface(USBD_HandleTypeDef *pdev, void *fops)
{
    if (fops != NULL)
    {
        uvc_fops = (USBD_UVC_ItfTypeDef *)fops;
    }
    else
    {
        uvc_fops = &uvc_default_itf;
    }

    if (pdev != NULL)
    {
        pdev->pUserData = uvc_fops;
    }

    return (uint8_t)USBD_OK;
}

static usbd_uvc_handle_t *UVC_GetHandle(USBD_HandleTypeDef *pdev)
{
    if (pdev == NULL)
    {
        return NULL;
    }

    return (usbd_uvc_handle_t *)pdev->pClassData;
}

static void UVC_SnapshotInEpRegs(uint8_t epnum,
                                 volatile uint32_t *diepctl,
                                 volatile uint32_t *dieptsiz,
                                 volatile uint32_t *diepint)
{
    USB_OTG_INEndpointTypeDef *in_ep_regs =
        (USB_OTG_INEndpointTypeDef *)((uint32_t)USB_OTG_HS +
                                      USB_OTG_IN_ENDPOINT_BASE +
                                      ((uint32_t)epnum * USB_OTG_EP_REG_SIZE));

    if (diepctl != NULL)
    {
        *diepctl = in_ep_regs->DIEPCTL;
    }
    if (dieptsiz != NULL)
    {
        *dieptsiz = in_ep_regs->DIEPTSIZ;
    }
    if (diepint != NULL)
    {
        *diepint = in_ep_regs->DIEPINT;
    }
}

static USBD_UVC_ItfTypeDef *UVC_GetItf(USBD_HandleTypeDef *pdev)
{
    if (pdev == NULL)
    {
        return NULL;
    }

    return (USBD_UVC_ItfTypeDef *)pdev->pUserData;
}

static uint8_t UVC_PreparePrepackets(const uint8_t *frame_ptr, uint32_t frame_size, uint16_t packet_size)
{
    uint16_t payload_size;
    uint32_t packet_count32;
    uint32_t offset = 0U;
    uint16_t i;

    if ((frame_ptr == NULL) || (frame_size == 0U) ||
        (packet_size <= UVC_HEADER_SIZE) || (packet_size > UVC_IN_PACKET))
    {
        uvc_prepacket_fallbacks++;
        return 0U;
    }

    payload_size = (uint16_t)(packet_size - UVC_HEADER_SIZE);
    packet_count32 = (frame_size + payload_size - 1U) / payload_size;
    if ((packet_count32 == 0U) || (packet_count32 > UVC_PREPACKET_MAX_PACKETS))
    {
        uvc_prepacket_fallbacks++;
        return 0U;
    }

    if ((uvc_prepacket_frame_ptr == frame_ptr) &&
        (uvc_prepacket_frame_size == frame_size) &&
        (uvc_prepacket_packet_size == packet_size) &&
        (uvc_prepacket_count == (uint16_t)packet_count32))
    {
        uvc_prepacket_count_dbg = uvc_prepacket_count;
        return 1U;
    }

    for (i = 0U; i < (uint16_t)packet_count32; i++)
    {
        uint32_t remaining = frame_size - offset;
        uint16_t chunk = (remaining > payload_size) ? payload_size : (uint16_t)remaining;

        uvc_prepacket_buf[i][0] = UVC_HEADER_SIZE;
        uvc_prepacket_buf[i][1] = UVC_PAYLOAD_EOH;
        memcpy(&uvc_prepacket_buf[i][UVC_HEADER_SIZE], &frame_ptr[offset], chunk);
        uvc_prepacket_len[i] = (uint16_t)(chunk + UVC_HEADER_SIZE);
        uvc_prepacket_offset[i] = offset;
        offset += chunk;
    }

    uvc_prepacket_frame_ptr = frame_ptr;
    uvc_prepacket_frame_size = frame_size;
    uvc_prepacket_packet_size = packet_size;
    uvc_prepacket_count = (uint16_t)packet_count32;
    uvc_prepacket_builds++;
    uvc_prepacket_count_dbg = uvc_prepacket_count;
    uvc_prepacket_last_packet_size = packet_size;
    uvc_prepacket_last_frame_size = frame_size;
    return 1U;
}

static uint8_t UVC_BuildRamFramePackets(const uint8_t *frame_ptr, uint32_t frame_size,
                                        uint16_t packet_size, uint16_t *next_packet,
                                        uvc_ram_frame_packets_t *map)
{
    uint16_t payload_size;
    uint32_t packet_count32;
    uint32_t offset = 0U;
    uint16_t i;

    if ((frame_ptr == NULL) || (frame_size == 0U) || (next_packet == NULL) || (map == NULL) ||
        (packet_size <= UVC_HEADER_SIZE) || (packet_size > UVC_IN_PACKET))
    {
        uvc_ram_packet_fallbacks++;
        return 0U;
    }

    payload_size = (uint16_t)(packet_size - UVC_HEADER_SIZE);
    packet_count32 = (frame_size + payload_size - 1U) / payload_size;
    if ((packet_count32 == 0U) ||
        ((*next_packet + (uint16_t)packet_count32) > UVC_PREPACKET_MAX_PACKETS))
    {
        uvc_ram_packet_fallbacks++;
        return 0U;
    }

    map->valid = 1U;
    map->frame_ptr = frame_ptr;
    map->frame_size = frame_size;
    map->first_packet = *next_packet;
    map->packet_count = (uint16_t)packet_count32;

    for (i = 0U; i < map->packet_count; i++)
    {
        uint16_t slot = (uint16_t)(map->first_packet + i);
        uint32_t remaining = frame_size - offset;
        uint16_t chunk = (remaining > payload_size) ? payload_size : (uint16_t)remaining;

        uvc_prepacket_buf[slot][0] = UVC_HEADER_SIZE;
        uvc_prepacket_buf[slot][1] = 0U;
        memcpy(&uvc_prepacket_buf[slot][UVC_HEADER_SIZE], &frame_ptr[offset], chunk);
        uvc_prepacket_len[slot] = (uint16_t)(chunk + UVC_HEADER_SIZE);
        uvc_prepacket_offset[slot] = offset;
        offset += chunk;
    }

    *next_packet = (uint16_t)(*next_packet + map->packet_count);
    return 1U;
}

static uint8_t UVC_EnsureRamPackets(uint16_t packet_size)
{
    uint16_t next_packet = 0U;

    if (uvc_ram_packet_enable == 0U)
    {
        uvc_ram_packet_ready_dbg = 0U;
        return 0U;
    }

    if ((packet_size <= UVC_HEADER_SIZE) || (packet_size > UVC_IN_PACKET))
    {
        uvc_ram_packet_ready_dbg = 0U;
        uvc_ram_packet_fallbacks++;
        return 0U;
    }

    if ((uvc_ram_packets_ready != 0U) && (uvc_ram_packet_size == packet_size))
    {
        uvc_ram_packet_ready_dbg = 1U;
        return 1U;
    }

    memset(uvc_ram_frames, 0, sizeof(uvc_ram_frames));
    uvc_ram_packets_ready = 0U;
    uvc_ram_packet_ready_dbg = 0U;

    if (UVC_BuildRamFramePackets(jpeg_frame_0, jpeg_frame_0_size, packet_size,
                                 &next_packet, &uvc_ram_frames[0]) == 0U)
    {
        return 0U;
    }

    if (UVC_BuildRamFramePackets(jpeg_frame_1, jpeg_frame_1_size, packet_size,
                                 &next_packet, &uvc_ram_frames[1]) == 0U)
    {
        memset(uvc_ram_frames, 0, sizeof(uvc_ram_frames));
        return 0U;
    }

    uvc_ram_packet_size = packet_size;
    uvc_ram_packets_ready = 1U;
    uvc_ram_packet_ready_dbg = 1U;
    uvc_ram_packet_total_packets = next_packet;
    uvc_ram_packet_builds++;
    return 1U;
}

static const uvc_ram_frame_packets_t *UVC_FindRamPackets(const uint8_t *frame_ptr, uint32_t frame_size)
{
    uint32_t i;

    if ((uvc_ram_packets_ready == 0U) || (frame_ptr == NULL) || (frame_size == 0U))
    {
        return NULL;
    }

    for (i = 0U; i < (sizeof(uvc_ram_frames) / sizeof(uvc_ram_frames[0])); i++)
    {
        if ((uvc_ram_frames[i].valid != 0U) &&
            (uvc_ram_frames[i].frame_ptr == frame_ptr) &&
            (uvc_ram_frames[i].frame_size == frame_size))
        {
            uvc_ram_packet_last_frame = i;
            return &uvc_ram_frames[i];
        }
    }

    return NULL;
}

static uint16_t UVC_GetVsControlSelector(uint16_t wValue)
{
    uint8_t selector = (uint8_t)(wValue >> 8);

    if ((selector == 0U) && ((wValue & 0x00FFU) != 0U))
    {
        selector = (uint8_t)(wValue & 0x00FFU);
    }

    return (uint16_t)((uint16_t)selector << 8);
}

static void UVC_SendProbeCommit(USBD_HandleTypeDef *pdev, const uvc_probe_commit_t *pc, uint16_t req_len)
{
    uint16_t len = (uint16_t)MIN(req_len, sizeof(uvc_probe_commit_t));

    memcpy(ep0_probe_buf, pc, sizeof(uvc_probe_commit_t));
    (void)USBD_CtlSendData(pdev, ep0_probe_buf, len);
}

static void UVC_UNUSED_CODE UVC_ResetBackendForRestart(usbd_uvc_handle_t *huvc)
{
    (void)USBD_LL_FlushEP(&hUsbDeviceHS, UVC_IN_EP);

    backend_state.frame_ptr = NULL;
    backend_state.frame_size = 0U;
    backend_state.packet_index = 0U;
    backend_state.full_packets = 0U;
    backend_state.remainder = 0U;
    backend_state.last_packet = 0U;
    backend_state.wait_frame_interval = 0U;
    backend_state.next_frame_tick = 0U;
    backend_state.next_keepalive_tick = 0U;
    backend_state.active = 1U;
    uvc_min_usb_tx_complete = 0U;
    uvc_ram_active_frame = NULL;

    if (huvc != NULL)
    {
        huvc->state = UVC_STATE_READY;
    }

    uvc_payload_header[0] = UVC_HEADER_SIZE;
    uvc_payload_header[1] = UVC_PAYLOAD_EOH;
    uvc_tx_in_flight_dbg = 0U;
    uvc_dbg_wait_frame_interval = 0U;
    uvc_dbg_state_live = UVC_STATE_READY;
}

static uint16_t UVC_GetPacketSize(USBD_SpeedTypeDef speed)
{
    return (speed == USBD_SPEED_HIGH) ? (uint16_t)UVC_IN_PACKET : (uint16_t)UVC_FS_PACKET;
}

static uint16_t UVC_GetPayloadTransferSize(USBD_SpeedTypeDef speed)
{
    uint16_t packet_size = UVC_GetPacketSize(speed);

    if ((uvc_payload_packet_bytes >= UVC_HEADER_SIZE) &&
        (uvc_payload_packet_bytes < packet_size))
    {
        packet_size = (uint16_t)uvc_payload_packet_bytes;
    }

    return packet_size;
}

static void UVC_InitDefaultProbeCommit(void)
{
    memset(&uvc_probe, 0, sizeof(uvc_probe));
    memset(&uvc_commit, 0, sizeof(uvc_commit));
    memset(&uvc_res, 0, sizeof(uvc_res));

    uvc_probe.bFormatIndex = 1U;
    uvc_probe.bFrameIndex = 1U;
    uvc_probe.dwFrameInterval = UVC_FRAME_INTERVAL_100NS;
    uvc_probe.dwMaxVideoFrameSize = video_source_get_max_frame_size();
    uvc_probe.dwMaxPayloadTransferSize = UVC_GetPayloadTransferSize(hUsbDeviceHS.dev_speed);

    uvc_commit = uvc_probe;
    uvc_res.dwFrameInterval = UVC_FRAME_INTERVAL_100NS;
    UVC_DebugProbeCommit();
}

static void UVC_NormalizeProbeCommit(uvc_probe_commit_t *pc, USBD_SpeedTypeDef speed)
{
    if (pc == NULL)
    {
        return;
    }

    pc->bFormatIndex = 1U;
    pc->bFrameIndex = 1U;
    pc->dwFrameInterval = UVC_FRAME_INTERVAL_100NS;
    pc->dwMaxVideoFrameSize = video_source_get_max_frame_size();
    pc->dwMaxPayloadTransferSize = UVC_GetPayloadTransferSize(speed);
}

static void UVC_DebugProbeCommit(void)
{
    uvc_probe_dbg_interval = uvc_probe.dwFrameInterval;
    uvc_probe_dbg_max_frame = uvc_probe.dwMaxVideoFrameSize;
    uvc_probe_dbg_max_payload = uvc_probe.dwMaxPayloadTransferSize;
    uvc_commit_dbg_interval = uvc_commit.dwFrameInterval;
    uvc_commit_dbg_max_frame = uvc_commit.dwMaxVideoFrameSize;
    uvc_commit_dbg_max_payload = uvc_commit.dwMaxPayloadTransferSize;
    uvc_commit_dbg_format = uvc_commit.bFormatIndex;
    uvc_commit_dbg_frame = uvc_commit.bFrameIndex;
}

static void UVC_RuntimePublish(void)
{
    usbd_uvc_handle_t *huvc = UVC_GetHandle(&hUsbDeviceHS);

    uvc_runtime_dbg.streaming_enabled = uvc_runtime_state.streaming_enabled;
    uvc_runtime_dbg.ep_busy = uvc_runtime_state.ep_busy;
    uvc_runtime_dbg.drop_current_frame = uvc_runtime_state.drop_current_frame;
    uvc_runtime_dbg.current_alt_setting = current_alt_setting;
    uvc_runtime_dbg.huvc_state = (huvc != NULL) ? huvc->state : 0xFFFFFFFFU;
    uvc_runtime_dbg.frame_active = uvc_runtime_state.frame_active;
    uvc_runtime_dbg.fid = uvc_runtime_state.fid & UVC_PAYLOAD_FID;
    uvc_runtime_dbg.last_packet_was_eof = uvc_runtime_state.last_packet_was_eof;
    uvc_runtime_dbg.last_offset = uvc_runtime_state.offset;
    uvc_runtime_dbg.last_frame_size = uvc_runtime_state.frame_size;
    uvc_runtime_dbg.next_frame_tick = uvc_runtime_state.next_frame_tick;

    uvc_watch.streaming_enabled = uvc_runtime_dbg.streaming_enabled;
    uvc_watch.ep_busy = uvc_runtime_dbg.ep_busy;
    uvc_watch.current_alt_setting = uvc_runtime_dbg.current_alt_setting;
    uvc_watch.huvc_state = uvc_runtime_dbg.huvc_state;
    uvc_watch.frame_active = uvc_runtime_dbg.frame_active;
    uvc_watch.fid = uvc_runtime_dbg.fid;
    uvc_watch.cnt_eof = uvc_runtime_dbg.cnt_eof;
    uvc_watch.cnt_data_in = uvc_runtime_dbg.cnt_data_in;
    uvc_watch.cnt_iso_in_incomplete = uvc_runtime_dbg.cnt_iso_in_incomplete;
    uvc_watch.cnt_underrun = uvc_runtime_dbg.cnt_underrun;
    uvc_watch.cnt_dropped_frames = uvc_runtime_dbg.cnt_dropped_frames;
    uvc_watch.cnt_frame_load = uvc_runtime_dbg.cnt_frame_load;
    uvc_watch.cnt_payload = uvc_runtime_dbg.cnt_payload;
    uvc_watch.cnt_header_only = uvc_runtime_dbg.cnt_header_only;
    uvc_watch.cnt_idle_gap_skip = uvc_runtime_idle_gap_skips;
    uvc_watch.cnt_idle_iso_skip = uvc_runtime_idle_iso_skips;
    uvc_watch.cnt_idle_zlp = uvc_runtime_idle_zlp_packets;
    uvc_watch.cnt_flush_recovery = uvc_runtime_dbg.cnt_flush_recovery;
    uvc_watch.cnt_flush_while_epena = uvc_runtime_dbg.cnt_flush_while_epena;
    uvc_watch.last_prime_reason = uvc_runtime_dbg.last_prime_reason;
    uvc_watch.last_len = uvc_runtime_dbg.last_len;
    uvc_watch.last_header = uvc_runtime_dbg.last_header;
    uvc_watch.last_offset = uvc_runtime_dbg.last_offset;
    uvc_watch.last_frame_size = uvc_runtime_dbg.last_frame_size;
    uvc_watch.next_frame_tick = uvc_runtime_dbg.next_frame_tick;
    uvc_watch.last_submit_tick = uvc_runtime_dbg.last_submit_tick;
    uvc_watch.last_complete_tick = uvc_runtime_dbg.last_complete_tick;
    uvc_watch.frame_interval_ms = uvc_frame_interval_ms;
}

static void UVC_RuntimeReset(uint8_t streaming_enabled)
{
    memset(&uvc_runtime_state, 0, sizeof(uvc_runtime_state));
    uvc_runtime_state.streaming_enabled = (streaming_enabled != 0U) ? 1U : 0U;
    uvc_runtime_state.fid = 0U;

    backend_state.frame_ptr = NULL;
    backend_state.frame_size = 0U;
    backend_state.packet_index = 0U;
    backend_state.full_packets = 0U;
    backend_state.remainder = 0U;
    backend_state.last_packet = 0U;
    backend_state.wait_frame_interval = 0U;
    backend_state.next_frame_tick = 0U;
    backend_state.next_keepalive_tick = 0U;
    backend_state.next_payload_tick = 0U;
    backend_state.active = (streaming_enabled != 0U) ? 1U : 0U;

    uvc_payload_header[0] = UVC_HEADER_SIZE;
    uvc_payload_header[1] = UVC_PAYLOAD_EOH;
    uvc_ram_active_frame = NULL;
    uvc_min_usb_tx_complete = 0U;
    uvc_tx_in_flight_dbg = 0U;
    uvc_dbg_wait_frame_interval = 0U;
    UVC_STM_ResetStream();
    UVC_RuntimePublish();
}

static void UVC_RuntimeDropCurrentFrame(void)
{
    if (uvc_runtime_state.frame_active != 0U)
    {
        uvc_runtime_dbg.cnt_dropped_frames++;
        uvc_runtime_dbg.cnt_fid_toggle_drop++;
        uvc_class_iso_frame_drops++;
        uvc_runtime_state.fid ^= UVC_PAYLOAD_FID;
    }

    uvc_runtime_state.frame_active = 0U;
    uvc_runtime_state.drop_current_frame = 0U;
    uvc_runtime_state.last_packet_was_eof = 0U;
    uvc_runtime_state.frame_ptr = NULL;
    uvc_runtime_state.frame_size = 0U;
    uvc_runtime_state.offset = 0U;
    uvc_runtime_state.next_frame_tick = 0U;

    backend_state.frame_ptr = NULL;
    backend_state.frame_size = 0U;
    backend_state.packet_index = 0U;
    backend_state.full_packets = 0U;
    backend_state.remainder = 0U;
    backend_state.last_packet = 0U;
    backend_state.wait_frame_interval = 0U;
    backend_state.next_frame_tick = 0U;
    backend_state.next_keepalive_tick = 0U;
    backend_state.next_payload_tick = 0U;

    UVC_RuntimePublish();
}

static void UVC_RuntimeFinishFrame(void)
{
    if (uvc_runtime_state.frame_active == 0U)
    {
        uvc_runtime_state.last_packet_was_eof = 0U;
        UVC_RuntimePublish();
        return;
    }

    if ((uvc_runtime_state.frame_ptr != NULL) && (uvc_runtime_state.frame_size >= 4U))
    {
        const uint8_t *tail = uvc_runtime_state.frame_ptr + uvc_runtime_state.frame_size - 4U;

        uvc_last_payload_b0 = tail[0];
        uvc_last_payload_b1 = tail[1];
        uvc_last_payload_b2 = tail[2];
        uvc_last_payload_b3 = tail[3];

        if ((uvc_runtime_state.frame_ptr[0] == 0xFFU) &&
            (uvc_runtime_state.frame_ptr[1] == 0xD8U) &&
            (uvc_runtime_state.frame_ptr[uvc_runtime_state.frame_size - 2U] == 0xFFU) &&
            (uvc_runtime_state.frame_ptr[uvc_runtime_state.frame_size - 1U] == 0xD9U))
        {
            uvc_frame_integrity_ok++;
        }
        else
        {
            uvc_frame_integrity_fail++;
        }
    }

    uvc_runtime_dbg.cnt_eof++;
    uvc_frames_sent++;
    uvc_min_frame_done++;
    uvc_runtime_dbg.cnt_fid_toggle_eof++;
    uvc_runtime_state.fid ^= UVC_PAYLOAD_FID;
    uvc_runtime_state.frame_active = 0U;
    uvc_runtime_state.last_packet_was_eof = 0U;
    uvc_runtime_state.frame_ptr = NULL;
    uvc_runtime_state.frame_size = 0U;
    uvc_runtime_state.offset = 0U;

    if ((uvc_runtime_no_frame_gap_enable == 0U) &&
        (uvc_frame_interval_ms != 0U))
    {
        uvc_runtime_state.next_frame_tick = HAL_GetTick() + uvc_frame_interval_ms;
    }
    else
    {
        uvc_runtime_state.next_frame_tick = 0U;
    }
    uvc_class_next_frame_tick_dbg = uvc_runtime_state.next_frame_tick;

    backend_state.frame_ptr = NULL;
    backend_state.frame_size = 0U;
    backend_state.packet_index = 0U;
    backend_state.full_packets = 0U;
    backend_state.remainder = 0U;
    backend_state.last_packet = 0U;
    backend_state.wait_frame_interval = (uvc_runtime_state.next_frame_tick != 0U) ? 1U : 0U;
    backend_state.next_frame_tick = uvc_runtime_state.next_frame_tick;

    UVC_RuntimePublish();
}

static uint8_t UVC_RuntimeLoadFrameIfDue(void)
{
    const video_frame_t *frame;
    uint32_t now;
    bool repeated = false;

    if (uvc_runtime_state.frame_active != 0U)
    {
        return 1U;
    }

    if (uvc_runtime_state.next_frame_tick != 0U)
    {
        now = HAL_GetTick();
        if ((int32_t)(now - uvc_runtime_state.next_frame_tick) < 0)
        {
            uvc_dbg_wait_frame_interval = 1U;
            uvc_frame_pace_skips++;
            UVC_RuntimePublish();
            return 0U;
        }

        uvc_runtime_state.next_frame_tick = 0U;
        backend_state.wait_frame_interval = 0U;
        backend_state.next_frame_tick = 0U;
        uvc_dbg_wait_frame_interval = 0U;
        uvc_frame_interval_releases++;
    }

    if (!video_source_prepare_next_frame(&repeated))
    {
        uvc_runtime_dbg.cnt_underrun++;
        uvc_min_no_pending_frames++;
        uvc_stream_start_no_frame++;
        UVC_RuntimePublish();
        return 0U;
    }

    if (repeated)
    {
        uvc_runtime_dbg.cnt_underrun++;
        uvc_min_repeat_last_frames++;
    }

    frame = video_source_get_current_frame();
    if ((frame == NULL) || (frame->data == NULL) || (frame->size == 0U) ||
        (frame->size > video_source_get_max_frame_size()))
    {
        uvc_runtime_dbg.cnt_underrun++;
        uvc_stream_start_no_frame++;
        UVC_RuntimePublish();
        return 0U;
    }

    if ((frame->size < 4U) ||
        (frame->data[0] != 0xFFU) ||
        (frame->data[1] != 0xD8U) ||
        (frame->data[frame->size - 2U] != 0xFFU) ||
        (frame->data[frame->size - 1U] != 0xD9U))
    {
        uvc_runtime_dbg.cnt_bad_jpeg_frame++;
        uvc_runtime_dbg.cnt_underrun++;
        uvc_frame_integrity_fail++;
        UVC_RuntimePublish();
        return 0U;
    }

    uvc_runtime_state.frame_active = 1U;
    uvc_runtime_state.drop_current_frame = 0U;
    uvc_runtime_state.frame_ptr = frame->data;
    uvc_runtime_state.frame_size = frame->size;
    uvc_runtime_state.offset = 0U;

    backend_state.active = 1U;
    backend_state.frame_ptr = frame->data;
    backend_state.frame_size = frame->size;
    backend_state.packet_index = 0U;
    backend_state.last_packet = 0U;

    uvc_min_frame_loads++;
    uvc_runtime_dbg.cnt_frame_load++;
    uvc_min_last_frame_size = frame->size;
    uvc_last_frame_size = frame->size;
    uvc_current_frame_size_dbg = frame->size;
    uvc_current_frame_ptr_dbg = frame->data;

    if (frame->size >= 4U)
    {
        uvc_first_payload_b0 = frame->data[0];
        uvc_first_payload_b1 = frame->data[1];
        uvc_first_payload_b2 = frame->data[2];
        uvc_first_payload_b3 = frame->data[3];
    }

    UVC_RuntimePublish();
    return 1U;
}

static void UVC_FAST_CODE UVC_CleanTxBuffer(uint8_t *packet, uint16_t packet_size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    uintptr_t start;
    uintptr_t end;

    if ((packet == NULL) || (packet_size == 0U) ||
        ((SCB->CCR & SCB_CCR_DC_Msk) == 0U))
    {
        return;
    }

    start = ((uintptr_t)packet) & ~(uintptr_t)31U;
    end = (((uintptr_t)packet) + packet_size + 31U) & ~(uintptr_t)31U;
    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    uvc_runtime_dbg.cnt_cache_clean++;
#else
    (void)packet;
    (void)packet_size;
#endif
}

static USBD_StatusTypeDef UVC_FlushStreamEP(USBD_HandleTypeDef *pdev, uint32_t reason)
{
    USBD_StatusTypeDef flush_status;
    uint32_t before_diepctl = 0U;
    uint32_t before_dieptsiz = 0U;
    uint32_t before_diepint = 0U;
    uint32_t after_diepctl = 0U;
    uint32_t after_dieptsiz = 0U;
    uint32_t after_diepint = 0U;

    if (pdev == NULL)
    {
        uvc_runtime_dbg.last_flush_status = (uint32_t)USBD_FAIL;
        uvc_runtime_dbg.last_flush_reason = reason;
        return USBD_FAIL;
    }

    UVC_SnapshotInEpRegs((uint8_t)(UVC_IN_EP & 0x7FU),
                         &before_diepctl,
                         &before_dieptsiz,
                         &before_diepint);
    uvc_runtime_dbg.flush_before_diepctl = before_diepctl;
    uvc_runtime_dbg.flush_before_dieptsiz = before_dieptsiz;
    uvc_runtime_dbg.flush_before_diepint = before_diepint;
    uvc_runtime_dbg.last_flush_reason = reason;

    if ((before_diepctl & USB_OTG_DIEPCTL_EPENA) != 0U)
    {
        uvc_runtime_dbg.cnt_flush_while_epena++;
    }

    flush_status = USBD_LL_FlushEP(pdev, UVC_IN_EP);
    uvc_runtime_dbg.last_flush_status = (uint32_t)flush_status;

    UVC_SnapshotInEpRegs((uint8_t)(UVC_IN_EP & 0x7FU),
                         &after_diepctl,
                         &after_dieptsiz,
                         &after_diepint);
    uvc_runtime_dbg.flush_after_diepctl = after_diepctl;
    uvc_runtime_dbg.flush_after_dieptsiz = after_dieptsiz;
    uvc_runtime_dbg.flush_after_diepint = after_diepint;

    if (flush_status == USBD_OK)
    {
        if (reason == UVC_FLUSH_REASON_BEFORE_TX)
        {
            uvc_runtime_dbg.cnt_flush_before_tx++;
        }
        else
        {
            uvc_runtime_dbg.cnt_flush_recovery++;
        }
    }
    else
    {
        uvc_runtime_dbg.cnt_flush_before_tx_fail++;
    }

    return flush_status;
}

static uint8_t UVC_FAST_CODE UVC_PrimeNextPacket(USBD_HandleTypeDef *pdev)
{
    usbd_uvc_handle_t *huvc = UVC_GetHandle(pdev);
    USBD_StatusTypeDef status;
    uint16_t packet_size;
    uint16_t payload_capacity;
    uint16_t tx_len = UVC_HEADER_SIZE;
    uint16_t chunk = 0U;
    uint8_t header;
    uint8_t sending_payload = 0U;
    uint8_t is_last = 0U;
    uint32_t offset = 0U;
    uint32_t new_offset = 0U;

    if ((huvc == NULL) || (pdev == NULL))
    {
        uvc_runtime_dbg.cnt_prime_skip_no_handle++;
        uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_NO_HANDLE;
        return (uint8_t)USBD_FAIL;
    }

    if (uvc_runtime_state.streaming_enabled == 0U)
    {
        uvc_runtime_dbg.cnt_prime_skip_not_streaming++;
        uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_DISABLED;
        UVC_RuntimePublish();
        return (uint8_t)USBD_OK;
    }

    if (current_alt_setting != 1U)
    {
        uvc_runtime_dbg.cnt_prime_skip_alt++;
        uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_ALT;
        UVC_RuntimePublish();
        return (uint8_t)USBD_OK;
    }

    if (uvc_runtime_state.ep_busy != 0U)
    {
        uvc_runtime_dbg.cnt_prime_skip_busy++;
        uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_BUSY;
        UVC_RuntimePublish();
        return (uint8_t)USBD_OK;
    }

    uvc_runtime_dbg.cnt_prime_calls++;
    if (uvc_runtime_state.drop_current_frame != 0U)
    {
        UVC_RuntimeDropCurrentFrame();
    }

    packet_size = UVC_GetPayloadTransferSize(pdev->dev_speed);
    if ((packet_size <= UVC_HEADER_SIZE) || (packet_size > UVC_IN_PACKET))
    {
        packet_size = UVC_GetPacketSize(pdev->dev_speed);
    }
    if ((packet_size <= UVC_HEADER_SIZE) || (packet_size > UVC_IN_PACKET))
    {
        packet_size = UVC_IN_PACKET;
    }

    payload_capacity = (uint16_t)(packet_size - UVC_HEADER_SIZE);
    header = (uint8_t)(UVC_PAYLOAD_EOH | (uvc_runtime_state.fid & UVC_PAYLOAD_FID));

    if ((payload_capacity != 0U) && (UVC_RuntimeLoadFrameIfDue() != 0U) &&
        (uvc_runtime_state.frame_ptr != NULL) &&
        (uvc_runtime_state.offset < uvc_runtime_state.frame_size))
    {
        uint32_t remaining = uvc_runtime_state.frame_size - uvc_runtime_state.offset;

        offset = uvc_runtime_state.offset;
        chunk = (remaining > payload_capacity) ? payload_capacity : (uint16_t)remaining;
        is_last = (chunk == remaining) ? 1U : 0U;
        if (is_last != 0U)
        {
            header |= UVC_PAYLOAD_EOF;
        }

        memcpy(&uvc_runtime_tx_packet[UVC_HEADER_SIZE],
               &uvc_runtime_state.frame_ptr[offset],
               chunk);
        tx_len = (uint16_t)(UVC_HEADER_SIZE + chunk);
        sending_payload = 1U;
        new_offset = offset + chunk;
    }
    else if (uvc_runtime_state.next_frame_tick != 0U)
    {
        if ((uvc_runtime_idle_packet_mode == UVC_IDLE_PACKET_SKIP) ||
            ((uvc_runtime_idle_packet_mode == UVC_IDLE_PACKET_HEADER) &&
             (uvc_runtime_idle_header_only_enable == 0U)))
        {
            uvc_runtime_idle_gap_skips++;
            uvc_min_no_tx_wait_pending++;
            uvc_dbg_wait_frame_interval = 1U;
            uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_IDLE_GAP;
            UVC_RuntimePublish();
            return (uint8_t)USBD_OK;
        }

        if (uvc_runtime_idle_packet_mode == UVC_IDLE_PACKET_ZLP)
        {
            tx_len = 0U;
            uvc_runtime_idle_zlp_packets++;
            uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_IDLE_ZLP;
        }
        else
        {
            uvc_min_header_only_packets++;
            uvc_stm_header_only_packets++;
        }
    }
    else
    {
        uvc_min_header_only_packets++;
        uvc_stm_header_only_packets++;
    }

    if (tx_len != 0U)
    {
        uvc_runtime_tx_packet[0] = UVC_HEADER_SIZE;
        uvc_runtime_tx_packet[1] = header;
        uvc_payload_header[0] = uvc_runtime_tx_packet[0];
        uvc_payload_header[1] = header;
        uvc_last_header_b0 = uvc_runtime_tx_packet[0];
        uvc_last_header_b1 = header;
    }

    uvc_ll_tx_calls++;
    uvc_ll_tx_last_epnum = (uint8_t)(UVC_IN_EP & 0x7FU);
    uvc_ll_tx_last_len = tx_len;
    uvc_dbg_payload_last_data_len = chunk;
    uvc_dbg_payload_last_total_len = tx_len;
    uvc_dbg_payload_last_eof = is_last;
    uvc_dbg_payload_last_offset = offset;
    uvc_last_offset = offset;
    uvc_min_last_offset = offset;
    uvc_min_last_packet_size = tx_len;
    uvc_stm_last_offset = offset;
    uvc_stm_last_chunk = chunk;
    uvc_stm_last_len = tx_len;
    uvc_stm_last_header = header;
    uvc_runtime_dbg.last_len = tx_len;
    uvc_runtime_dbg.last_header = header;
    uvc_runtime_dbg.last_offset = offset;
    uvc_runtime_dbg.last_frame_size = uvc_runtime_state.frame_size;

    if ((uvc_runtime_flush_before_tx_enable != 0U) ||
        (uvc_runtime_flush_policy == UVC_FLUSH_POLICY_EVERY_TX))
    {
        (void)UVC_FlushStreamEP(pdev, UVC_FLUSH_REASON_BEFORE_TX);
    }

    UVC_CleanTxBuffer(uvc_runtime_tx_packet, tx_len);
    status = USBD_LL_Transmit(pdev, UVC_IN_EP, uvc_runtime_tx_packet, tx_len);
    uvc_ll_tx_last_status = (uint32_t)status;
    uvc_stm_last_status = (uint32_t)status;
    uvc_runtime_dbg.last_status = (uint32_t)status;
    if (uvc_usb_reg_snapshot_enable != 0U)
    {
        UVC_SnapshotInEpRegs((uint8_t)(UVC_IN_EP & 0x7FU),
                             &uvc_ll_tx_last_diepctl,
                             &uvc_ll_tx_last_dieptsiz,
                             &uvc_ll_tx_last_diepint);
    }

    if (status != USBD_OK)
    {
        uvc_runtime_state.ep_busy = 0U;
        uvc_runtime_state.last_packet_was_eof = 0U;
        uvc_runtime_dbg.cnt_transmit_fail++;
        uvc_dbg_payload_send_fail++;
        uvc_start_payload_fail++;
        if (sending_payload != 0U)
        {
            uvc_runtime_state.drop_current_frame = 1U;
            UVC_RuntimeDropCurrentFrame();
        }
        uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_TX_FAIL;
        huvc->state = UVC_STATE_READY;
        uvc_dbg_state_live = UVC_STATE_READY;
        UVC_RuntimePublish();
        return (uint8_t)USBD_FAIL;
    }

    uvc_runtime_state.ep_busy = 1U;
    uvc_runtime_state.last_packet_was_eof = is_last;
    if (sending_payload != 0U)
    {
        uvc_runtime_state.offset = new_offset;
        backend_state.packet_index++;
        backend_state.last_packet = is_last;
        uvc_min_last_packet_index = backend_state.packet_index;
        uvc_stm_last_packet_index = backend_state.packet_index;
        uvc_runtime_dbg.cnt_payload++;
        uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_PAYLOAD;
    }
    else if (tx_len == 0U)
    {
        uvc_runtime_state.last_packet_was_eof = 0U;
        uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_IDLE_ZLP;
    }
    else
    {
        uvc_runtime_dbg.cnt_header_only++;
        uvc_runtime_dbg.last_prime_reason = UVC_PRIME_REASON_HEADER_ONLY;
    }

    uvc_class_last_tx_submit_tick = HAL_GetTick();
    uvc_runtime_dbg.last_submit_tick = uvc_class_last_tx_submit_tick;
    uvc_ll_tx_ok++;
    uvc_ll_tx_ok_len = tx_len;
    uvc_packets_sent++;
    uvc_dbg_payload_send_ok++;
    uvc_runtime_dbg.cnt_prime_ok++;
    uvc_tx_in_flight_dbg = 1U;
    huvc->state = UVC_STATE_STREAMING;
    uvc_dbg_state_live = UVC_STATE_STREAMING;
    uvc_pending_stream_start_dbg = 0U;
    UVC_RuntimePublish();
    return (uint8_t)USBD_OK;
}

static int8_t UVC_Itf_Init(void)
{
    video_source_init();
    UVC_RuntimeReset(0U);
    (void)UVC_EnsureRamPackets(UVC_GetPayloadTransferSize(hUsbDeviceHS.dev_speed));
    uvc_payload_header[0] = UVC_HEADER_SIZE;
    uvc_payload_header[1] = UVC_PAYLOAD_EOH;
    return 0;
}

static int8_t UVC_Itf_DeInit(void)
{
    UVC_RuntimeReset(0U);
    return 0;
}

static int8_t UVC_Itf_Start(void)
{
    UVC_RuntimeReset(1U);
    (void)UVC_EnsureRamPackets(UVC_GetPayloadTransferSize(hUsbDeviceHS.dev_speed));
    uvc_dbg_wait_frame_interval = 0U;
    uvc_stream_start_calls++;
    uvc_dbg_state_live = UVC_STATE_READY;
    return 0;
}

static int8_t UVC_Itf_Stop(void)
{
    UVC_RuntimeReset(0U);
    uvc_dbg_state_live = UVC_STATE_STOP;
    return 0;
}

static int8_t UVC_Itf_Control(const void *ctrl)
{
    (void)ctrl;
    return 0;
}

static int8_t UVC_Itf_DataMinimal(uint8_t **pbuf, uint16_t *psize, uint16_t *pcktidx)
{
    const video_frame_t *frame;
    uint16_t tx_packet_size = UVC_GetPayloadTransferSize(hUsbDeviceHS.dev_speed);
    uint16_t payload_size;

    uvc_min_data_calls++;

    if ((tx_packet_size <= UVC_HEADER_SIZE) || (tx_packet_size > UVC_IN_PACKET))
    {
        tx_packet_size = UVC_GetPacketSize(hUsbDeviceHS.dev_speed);
    }

    payload_size = (tx_packet_size > UVC_HEADER_SIZE) ?
                   (uint16_t)(tx_packet_size - UVC_HEADER_SIZE) : 0U;
    uvc_payload_packet_bytes_dbg = tx_packet_size;

    if ((backend_state.active == 0U) || (payload_size == 0U))
    {
        backend_state.last_packet = 0U;
        uvc_ram_active_frame = NULL;
        uvc_min_header_only_packets++;
        *pbuf = NULL;
        *psize = UVC_HEADER_SIZE;
        *pcktidx = 0U;
        return 0;
    }

    if (backend_state.wait_frame_interval != 0U)
    {
        uint32_t now = HAL_GetTick();

        if ((uvc_frame_interval_ms != 0U) &&
            ((int32_t)(now - backend_state.next_frame_tick) < 0))
        {
            uvc_dbg_wait_frame_interval = 1U;
            uvc_frame_pace_skips++;
            backend_state.last_packet = 0U;
            uvc_min_header_only_packets++;
            *pbuf = NULL;
            *psize = UVC_HEADER_SIZE;
            *pcktidx = 0U;
            return 0;
        }

        backend_state.wait_frame_interval = 0U;
        backend_state.next_frame_tick = 0U;
        backend_state.next_keepalive_tick = 0U;
        backend_state.next_payload_tick = 0U;
        uvc_dbg_wait_frame_interval = 0U;
        uvc_frame_interval_releases++;
    }

    if (backend_state.frame_ptr == NULL)
    {
        uvc_ram_active_frame = NULL;

        if (uvc_min_usb_tx_complete != 0U)
        {
            bool repeated = false;

            if (!video_source_prepare_next_frame(&repeated))
            {
                uvc_min_no_pending_frames++;
                uvc_min_no_tx_wait_pending++;
                backend_state.last_packet = 0U;
                uvc_min_header_only_packets++;
                *pbuf = NULL;
                *psize = UVC_HEADER_SIZE;
                *pcktidx = 0U;
                return 0;
            }

            if (repeated)
            {
                uvc_min_no_pending_frames++;
                uvc_min_repeat_last_frames++;
            }

            uvc_min_usb_tx_complete = 0U;
        }

        frame = video_source_get_current_frame();
        if ((frame == NULL) || (frame->data == NULL) || (frame->size == 0U))
        {
            uvc_stream_start_no_frame++;
            uvc_min_no_pending_frames++;
            uvc_min_no_tx_wait_pending++;
            backend_state.last_packet = 0U;
            uvc_min_header_only_packets++;
            *pbuf = NULL;
            *psize = UVC_HEADER_SIZE;
            *pcktidx = 0U;
            return 0;
        }

        backend_state.frame_ptr = frame->data;
        backend_state.frame_size = frame->size;
        backend_state.packet_index = 0U;
        backend_state.full_packets = frame->size / payload_size;
        backend_state.remainder = frame->size % payload_size;
        backend_state.wait_frame_interval = 0U;
        backend_state.next_frame_tick = 0U;
        backend_state.next_keepalive_tick = 0U;
        backend_state.next_payload_tick = 0U;

        uvc_min_frame_loads++;
        uvc_min_last_frame_size = frame->size;
        uvc_last_frame_size = frame->size;
        uvc_current_frame_size_dbg = frame->size;
        uvc_current_frame_ptr_dbg = frame->data;

        if ((uvc_ram_packet_enable != 0U) &&
            (UVC_EnsureRamPackets(tx_packet_size) != 0U))
        {
            uvc_ram_active_frame = UVC_FindRamPackets(frame->data, frame->size);
            if (uvc_ram_active_frame != NULL)
            {
                backend_state.full_packets = uvc_ram_active_frame->packet_count;
                backend_state.remainder = 0U;
                uvc_ram_packet_last_count = uvc_ram_active_frame->packet_count;
                uvc_ram_packet_last_size = frame->size;
            }
            else
            {
                uvc_ram_packet_fallbacks++;
            }
        }

        if (frame->size >= 4U)
        {
            uvc_first_payload_b0 = frame->data[0];
            uvc_first_payload_b1 = frame->data[1];
            uvc_first_payload_b2 = frame->data[2];
            uvc_first_payload_b3 = frame->data[3];
        }
    }

    if ((uvc_ram_active_frame != NULL) &&
        (backend_state.packet_index < uvc_ram_active_frame->packet_count))
    {
        uint16_t idx = (uint16_t)backend_state.packet_index;
        uint16_t slot = (uint16_t)(uvc_ram_active_frame->first_packet + idx);

        backend_state.last_packet =
            ((idx + 1U) == uvc_ram_active_frame->packet_count) ? 1U : 0U;

        *pcktidx = (uint16_t)(UVC_PACKET_INDEX_PREBUILT | idx);
        *psize = uvc_prepacket_len[slot];
        *pbuf = uvc_prepacket_buf[slot];

        uvc_dbg_payload_last_offset = uvc_prepacket_offset[slot];
        uvc_min_last_offset = uvc_prepacket_offset[slot];
        uvc_min_last_packet_index = idx;
        uvc_min_last_packet_size = *psize;
        uvc_ram_packet_last_idx = idx;
        uvc_ram_packet_hits++;

        backend_state.packet_index++;
        return 0;
    }

    if (backend_state.packet_index < backend_state.full_packets)
    {
        uint32_t offset = backend_state.packet_index * payload_size;

        *pcktidx = (uint16_t)backend_state.packet_index;
        *psize = tx_packet_size;
        *pbuf = (uint8_t *)(backend_state.frame_ptr + offset);

        backend_state.last_packet =
            (((backend_state.packet_index + 1U) == backend_state.full_packets) &&
             (backend_state.remainder == 0U)) ? 1U : 0U;
        uvc_dbg_payload_last_offset = offset;
        uvc_min_last_offset = offset;
        uvc_min_last_packet_index = backend_state.packet_index;
        uvc_min_last_packet_size = tx_packet_size;
        backend_state.packet_index++;
        return 0;
    }

    if ((backend_state.packet_index == backend_state.full_packets) && (backend_state.remainder != 0U))
    {
        uint32_t offset = backend_state.packet_index * payload_size;

        *pcktidx = (uint16_t)backend_state.packet_index;
        *psize = (uint16_t)(backend_state.remainder + UVC_HEADER_SIZE);
        *pbuf = (uint8_t *)(backend_state.frame_ptr + offset);

        backend_state.last_packet = 1U;
        uvc_dbg_payload_last_offset = offset;
        uvc_min_last_offset = offset;
        uvc_min_last_packet_index = backend_state.packet_index;
        uvc_min_last_packet_size = *psize;
        backend_state.packet_index++;
        return 0;
    }

    if ((backend_state.frame_ptr != NULL) && (backend_state.frame_size >= 4U))
    {
        const uint8_t *tail = backend_state.frame_ptr + backend_state.frame_size - 4U;
        uvc_last_payload_b0 = tail[0];
        uvc_last_payload_b1 = tail[1];
        uvc_last_payload_b2 = tail[2];
        uvc_last_payload_b3 = tail[3];
        if ((backend_state.frame_ptr[0] == 0xFFU) &&
            (backend_state.frame_ptr[1] == 0xD8U) &&
            (backend_state.frame_ptr[backend_state.frame_size - 2U] == 0xFFU) &&
            (backend_state.frame_ptr[backend_state.frame_size - 1U] == 0xD9U))
        {
            uvc_frame_integrity_ok++;
        }
        else
        {
            uvc_frame_integrity_fail++;
        }
    }

    backend_state.frame_ptr = NULL;
    backend_state.frame_size = 0U;
    backend_state.packet_index = 0U;
    backend_state.full_packets = 0U;
    backend_state.remainder = 0U;
    backend_state.last_packet = 0U;
    if (uvc_frame_interval_ms != 0U)
    {
        uint32_t now = HAL_GetTick();

        backend_state.wait_frame_interval = 1U;
        backend_state.next_frame_tick = now + uvc_frame_interval_ms;
        backend_state.next_keepalive_tick = now + uvc_frame_keepalive_interval_ms;
    }
    else
    {
        backend_state.wait_frame_interval = 0U;
        backend_state.next_frame_tick = 0U;
        backend_state.next_keepalive_tick = 0U;
    }
    backend_state.next_payload_tick = 0U;
    uvc_class_next_frame_tick_dbg = backend_state.next_frame_tick;
    uvc_ram_active_frame = NULL;
    uvc_min_usb_tx_complete = 1U;
    uvc_min_frame_done++;
    uvc_frames_sent++;
    uvc_min_no_tx_wait_pending++;

    uvc_min_header_only_packets++;
    *pbuf = NULL;
    *psize = UVC_HEADER_SIZE;
    *pcktidx = 0U;
    return 0;
}

static int8_t UVC_Itf_Data(uint8_t **pbuf, uint16_t *psize, uint16_t *pcktidx)
{
    const video_frame_t *frame;
    uint16_t max_packet_size;
    uint16_t tx_packet_size;
    uint16_t payload_size;

    if ((pbuf == NULL) || (psize == NULL) || (pcktidx == NULL))
    {
        return -1;
    }

    if (uvc_min_source_enable != 0U)
    {
        return UVC_Itf_DataMinimal(pbuf, psize, pcktidx);
    }

    max_packet_size = UVC_GetPacketSize(hUsbDeviceHS.dev_speed);
    tx_packet_size = max_packet_size;
    if ((uvc_payload_packet_bytes >= UVC_HEADER_SIZE) &&
        (uvc_payload_packet_bytes < tx_packet_size))
    {
        tx_packet_size = (uint16_t)uvc_payload_packet_bytes;
    }
    payload_size = (uint16_t)(tx_packet_size - UVC_HEADER_SIZE);
    if (uvc_fast_path_debug_enable != 0U)
    {
        uvc_payload_packet_bytes_dbg = tx_packet_size;
    }

    if ((backend_state.active == 0U) || (payload_size == 0U))
    {
        backend_state.last_packet = 0U;
        *pbuf = NULL;
        *psize = UVC_HEADER_SIZE;
        *pcktidx = 0U;
        return 0;
    }

    if (backend_state.wait_frame_interval != 0U)
    {
        uint32_t now = HAL_GetTick();
        if ((uvc_frame_interval_ms != 0U) &&
            ((int32_t)(now - backend_state.next_frame_tick) < 0))
        {
            backend_state.last_packet = 0U;
            if (uvc_fast_path_debug_enable != 0U)
            {
                uvc_dbg_wait_frame_interval = 1U;
                uvc_frame_pace_skips++;
            }

            *pbuf = NULL;
            *pcktidx = 0U;
            if (uvc_frame_keepalive_enable != 0U)
            {
                *psize = UVC_HEADER_SIZE;
                if (uvc_fast_path_debug_enable != 0U)
                {
                    uvc_frame_keepalive_packets++;
                }
            }
            else
            {
                *psize = 0U;
            }
            return 0;
        }

        backend_state.wait_frame_interval = 0U;
        backend_state.next_frame_tick = 0U;
        backend_state.next_keepalive_tick = 0U;
        backend_state.next_payload_tick = 0U;
        uvc_dbg_wait_frame_interval = 0U;
        uvc_frame_interval_releases++;
    }

    if (backend_state.frame_ptr == NULL)
    {
        frame = video_source_get_current_frame();
        if ((frame == NULL) || (frame->data == NULL) || (frame->size == 0U))
        {
            uvc_stream_start_no_frame++;
            backend_state.last_packet = 0U;
            *pbuf = NULL;
            *psize = UVC_HEADER_SIZE;
            *pcktidx = 0U;
            return 0;
        }

        backend_state.frame_ptr = frame->data;
        backend_state.frame_size = frame->size;
        backend_state.packet_index = 0U;
        backend_state.full_packets = frame->size / payload_size;
        backend_state.remainder = frame->size % payload_size;
        backend_state.next_payload_tick = 0U;
        uvc_payload_next_tick_dbg = 0U;
        uvc_last_frame_size = frame->size;

        if (frame->size >= 4U)
        {
            uvc_first_payload_b0 = frame->data[0];
            uvc_first_payload_b1 = frame->data[1];
            uvc_first_payload_b2 = frame->data[2];
            uvc_first_payload_b3 = frame->data[3];
        }

        if ((uvc_prepacket_enable != 0U) &&
            (UVC_PreparePrepackets(backend_state.frame_ptr, backend_state.frame_size, tx_packet_size) != 0U))
        {
            backend_state.full_packets = uvc_prepacket_count;
            backend_state.remainder = 0U;
        }
    }

    if ((backend_state.packet_index != 0U) && (uvc_payload_packet_interval_ms != 0U))
    {
        uint32_t now = HAL_GetTick();

        if ((int32_t)(now - backend_state.next_payload_tick) < 0)
        {
            backend_state.last_packet = 0U;
            uvc_payload_pace_skips++;
            uvc_frame_keepalive_packets++;
            *pbuf = NULL;
            *psize = UVC_HEADER_SIZE;
            *pcktidx = 0U;
            return 0;
        }
    }

    if ((uvc_prepacket_enable != 0U) &&
        (uvc_prepacket_frame_ptr == backend_state.frame_ptr) &&
        (uvc_prepacket_frame_size == backend_state.frame_size) &&
        (uvc_prepacket_packet_size == tx_packet_size) &&
        (uvc_prepacket_count != 0U))
    {
        if (backend_state.packet_index < uvc_prepacket_count)
        {
            uint16_t idx = (uint16_t)backend_state.packet_index;
            uint8_t header_b1;

            if (idx == 0U)
            {
                uvc_payload_header[1] ^= UVC_PAYLOAD_FID;
            }

            header_b1 = (uint8_t)(UVC_PAYLOAD_EOH | (uvc_payload_header[1] & UVC_PAYLOAD_FID));
            backend_state.last_packet = ((idx + 1U) == uvc_prepacket_count) ? 1U : 0U;
            if (backend_state.last_packet != 0U)
            {
                header_b1 |= UVC_PAYLOAD_EOF;
            }

            uvc_payload_header[0] = UVC_HEADER_SIZE;
            uvc_payload_header[1] = header_b1;
            uvc_prepacket_buf[idx][0] = UVC_HEADER_SIZE;
            uvc_prepacket_buf[idx][1] = header_b1;

            *pcktidx = (uint16_t)(UVC_PACKET_INDEX_PREBUILT | idx);
            *psize = uvc_prepacket_len[idx];
            *pbuf = uvc_prepacket_buf[idx];
            uvc_dbg_payload_last_offset = uvc_prepacket_offset[idx];
            uvc_prepacket_last_idx = idx;
            uvc_prepacket_hits++;
            backend_state.packet_index++;
            if (uvc_payload_packet_interval_ms != 0U)
            {
                backend_state.next_payload_tick = HAL_GetTick() + uvc_payload_packet_interval_ms;
                if (uvc_fast_path_debug_enable != 0U)
                {
                    uvc_payload_next_tick_dbg = backend_state.next_payload_tick;
                }
            }
            return 0;
        }
    }

    if (backend_state.packet_index < backend_state.full_packets)
    {
        *pcktidx = (uint16_t)backend_state.packet_index;
        *psize = tx_packet_size;
        *pbuf = (uint8_t *)(backend_state.frame_ptr + (backend_state.packet_index * payload_size));
        uvc_dbg_payload_last_offset = backend_state.packet_index * payload_size;
        backend_state.last_packet =
            ((backend_state.packet_index + 1U) == backend_state.full_packets) &&
            (backend_state.remainder == 0U);
        backend_state.packet_index++;
        if (uvc_payload_packet_interval_ms != 0U)
        {
            backend_state.next_payload_tick = HAL_GetTick() + uvc_payload_packet_interval_ms;
            if (uvc_fast_path_debug_enable != 0U)
            {
                uvc_payload_next_tick_dbg = backend_state.next_payload_tick;
            }
        }
        return 0;
    }

    if ((backend_state.packet_index == backend_state.full_packets) && (backend_state.remainder != 0U))
    {
        *pcktidx = (uint16_t)backend_state.packet_index;
        *psize = (uint16_t)(backend_state.remainder + UVC_HEADER_SIZE);
        *pbuf = (uint8_t *)(backend_state.frame_ptr + (backend_state.packet_index * payload_size));
        uvc_dbg_payload_last_offset = backend_state.packet_index * payload_size;
        backend_state.last_packet = 1U;
        backend_state.packet_index++;
        if (uvc_payload_packet_interval_ms != 0U)
        {
            backend_state.next_payload_tick = HAL_GetTick() + uvc_payload_packet_interval_ms;
            if (uvc_fast_path_debug_enable != 0U)
            {
                uvc_payload_next_tick_dbg = backend_state.next_payload_tick;
            }
        }
        return 0;
    }

    if ((backend_state.frame_ptr != NULL) && (backend_state.frame_size >= 4U))
    {
        const uint8_t *tail = backend_state.frame_ptr + backend_state.frame_size - 4U;
        uvc_last_payload_b0 = tail[0];
        uvc_last_payload_b1 = tail[1];
        uvc_last_payload_b2 = tail[2];
        uvc_last_payload_b3 = tail[3];
        if ((backend_state.frame_ptr[0] == 0xFFU) &&
            (backend_state.frame_ptr[1] == 0xD8U) &&
            (backend_state.frame_ptr[backend_state.frame_size - 2U] == 0xFFU) &&
            (backend_state.frame_ptr[backend_state.frame_size - 1U] == 0xD9U))
        {
            uvc_frame_integrity_ok++;
        }
        else
        {
            uvc_frame_integrity_fail++;
        }
    }

    video_source_commit_pending_if_any();
    backend_state.frame_ptr = NULL;
    backend_state.frame_size = 0U;
    backend_state.packet_index = 0U;
    backend_state.full_packets = 0U;
    backend_state.remainder = 0U;
    backend_state.next_payload_tick = 0U;
    uvc_payload_next_tick_dbg = 0U;
    if (uvc_frame_interval_ms != 0U)
    {
        uint32_t now = HAL_GetTick();

        backend_state.wait_frame_interval = 1U;
        backend_state.next_frame_tick = now + uvc_frame_interval_ms;
        backend_state.next_keepalive_tick = now + uvc_frame_keepalive_interval_ms;
    }
    else
    {
        backend_state.wait_frame_interval = 0U;
        backend_state.next_frame_tick = 0U;
        backend_state.next_keepalive_tick = 0U;
    }
    uvc_class_next_frame_tick_dbg = backend_state.next_frame_tick;
    uvc_frames_sent++;

    backend_state.last_packet = 0U;
    *pbuf = NULL;
    *pcktidx = 0U;
    if ((uvc_frame_interval_ms == 0U) || (uvc_frame_keepalive_enable != 0U))
    {
        *psize = UVC_HEADER_SIZE;
        if (uvc_frame_interval_ms != 0U)
        {
            if (uvc_fast_path_debug_enable != 0U)
            {
                uvc_frame_keepalive_packets++;
            }
        }
    }
    else
    {
        *psize = 0U;
    }
    return 0;
}

static void UVC_STM_ResetStream(void)
{
    memset(&uvc_stm_state, 0, sizeof(uvc_stm_state));
    memset(&uvc_stm_last_payload, 0, sizeof(uvc_stm_last_payload));
    uvc_payload_header[0] = UVC_HEADER_SIZE;
    uvc_payload_header[1] = 0U;
    uvc_class_next_frame_tick_dbg = 0U;
}

static void UVC_STM_RecordPayloadCheckpoint(uint32_t offset, uint32_t packet_index,
                                            uint16_t chunk, uint8_t is_last)
{
    uvc_stm_last_payload.valid = 1U;
    uvc_stm_last_payload.fid = uvc_stm_state.fid;
    uvc_stm_last_payload.is_last = is_last;
    uvc_stm_last_payload.frame_ptr = uvc_stm_state.frame_ptr;
    uvc_stm_last_payload.frame_size = uvc_stm_state.frame_size;
    uvc_stm_last_payload.offset = offset;
    uvc_stm_last_payload.packet_index = packet_index;
    uvc_stm_last_payload.chunk = chunk;
}

static void UVC_STM_ClearPayloadCheckpoint(void)
{
    uvc_stm_last_payload.valid = 0U;
}

static void UVC_UNUSED_CODE UVC_DropFrameOnIsoIncomplete(usbd_uvc_handle_t *huvc)
{
    uint32_t now = HAL_GetTick();

    uvc_class_iso_frame_drops++;
    uvc_class_iso_drop_last_state = (huvc != NULL) ? huvc->state : 0xFFFFFFFFU;
    uvc_class_iso_drop_last_tick = now;
    uvc_class_iso_drop_last_offset = uvc_dbg_payload_last_offset;
    uvc_class_iso_drop_last_frame_size =
        (backend_state.frame_size != 0U) ? backend_state.frame_size : uvc_stm_state.frame_size;
    uvc_class_iso_drop_last_backend_packet_index = backend_state.packet_index;
    uvc_class_iso_drop_last_stm_packet_index = uvc_stm_state.packet_index;

    UVC_STM_ClearPayloadCheckpoint();

    backend_state.frame_ptr = NULL;
    backend_state.frame_size = 0U;
    backend_state.packet_index = 0U;
    backend_state.full_packets = 0U;
    backend_state.remainder = 0U;
    backend_state.last_packet = 0U;
    backend_state.wait_frame_interval = 0U;
    backend_state.next_frame_tick = 0U;
    backend_state.next_keepalive_tick = 0U;
    backend_state.next_payload_tick = 0U;
    backend_state.active = 1U;
    uvc_ram_active_frame = NULL;
    uvc_min_usb_tx_complete = 1U;
    uvc_payload_next_tick_dbg = 0U;

    uvc_stm_state.frame_active = 0U;
    uvc_stm_state.frame_ptr = NULL;
    uvc_stm_state.frame_size = 0U;
    uvc_stm_state.offset = 0U;
    uvc_stm_state.packet_index = 0U;
    uvc_stm_state.payload_run_count = 0U;
    if (uvc_frame_interval_ms != 0U)
    {
        uvc_stm_state.next_frame_tick = now + uvc_frame_interval_ms;
    }
    else
    {
        uvc_stm_state.next_frame_tick = 0U;
    }
    uvc_class_next_frame_tick_dbg = uvc_stm_state.next_frame_tick;

    uvc_payload_header[0] = UVC_HEADER_SIZE;
    uvc_payload_header[1] &= UVC_PAYLOAD_FID;
    uvc_tx_in_flight_dbg = 0U;

    if (huvc != NULL)
    {
        huvc->state = UVC_STATE_READY;
        uvc_dbg_state_live = UVC_STATE_READY;
    }
}

static void UVC_FAST_CODE UVC_RecordBackendPayloadCheckpoint(const uint8_t *payload, uint16_t packet_size,
                                                             uint16_t packet_index, uint8_t fid)
{
    uintptr_t frame_start;
    uintptr_t frame_end;
    uintptr_t payload_addr;
    uint32_t offset;
    uint32_t remaining;
    uint16_t chunk;

    if ((payload == NULL) ||
        (packet_size <= UVC_HEADER_SIZE) ||
        (backend_state.frame_ptr == NULL) ||
        (backend_state.frame_size == 0U))
    {
        UVC_STM_ClearPayloadCheckpoint();
        return;
    }

    frame_start = (uintptr_t)backend_state.frame_ptr;
    frame_end = frame_start + backend_state.frame_size;
    payload_addr = (uintptr_t)payload;
    if ((payload_addr < frame_start) || (payload_addr >= frame_end))
    {
        UVC_STM_ClearPayloadCheckpoint();
        return;
    }

    offset = (uint32_t)(payload_addr - frame_start);
    remaining = backend_state.frame_size - offset;
    chunk = (uint16_t)(packet_size - UVC_HEADER_SIZE);
    if (chunk > remaining)
    {
        chunk = (uint16_t)remaining;
    }

    uvc_stm_last_payload.valid = 1U;
    uvc_stm_last_payload.fid = (uint8_t)(fid & UVC_PAYLOAD_FID);
    uvc_stm_last_payload.is_last = (chunk == remaining) ? 1U : 0U;
    uvc_stm_last_payload.frame_ptr = backend_state.frame_ptr;
    uvc_stm_last_payload.frame_size = backend_state.frame_size;
    uvc_stm_last_payload.offset = offset;
    uvc_stm_last_payload.packet_index = packet_index;
    uvc_stm_last_payload.chunk = chunk;
}

static uint8_t UVC_STM_ShouldInsertPayloadGap(void)
{
    uint32_t gap_after = uvc_stm_payload_gap_after;

    if ((uvc_stm_payload_gap_enable == 0U) ||
        (gap_after == 0U) ||
        (gap_after > 255U) ||
        (uvc_stm_state.payload_run_count < (uint8_t)gap_after))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t UVC_STM_SendPayloadGap(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    uvc_stm_state.payload_run_count = 0U;
    uvc_stm_payload_gap_packets++;
    uvc_stm_header_only_packets++;
    return UVC_STM_SubmitPacket(pdev, epnum, NULL, 0U, uvc_stm_state.fid);
}

static void UVC_UNUSED_CODE UVC_STM_ReplayLastPayloadOnIsoError(void)
{
    uvc_stm_iso_replay_requests++;

    if (uvc_stm_iso_replay_enable == 0U)
    {
        uvc_stm_iso_replay_no_payload++;
        uvc_stm_iso_replay_disabled++;
        return;
    }

    if (uvc_stm_last_payload.valid == 0U)
    {
        uvc_stm_iso_replay_no_payload++;
        uvc_stm_iso_replay_invalid++;
        return;
    }

    if (uvc_stm_last_payload.frame_ptr == NULL)
    {
        uvc_stm_iso_replay_no_payload++;
        uvc_stm_iso_replay_null_frame++;
        return;
    }

    if (uvc_stm_last_payload.frame_size == 0U)
    {
        uvc_stm_iso_replay_no_payload++;
        uvc_stm_iso_replay_zero_size++;
        return;
    }

    uvc_stm_state.frame_active = 1U;
    uvc_stm_state.fid = uvc_stm_last_payload.fid;
    uvc_stm_state.frame_ptr = uvc_stm_last_payload.frame_ptr;
    uvc_stm_state.frame_size = uvc_stm_last_payload.frame_size;
    uvc_stm_state.offset = uvc_stm_last_payload.offset;
    uvc_stm_state.packet_index = uvc_stm_last_payload.packet_index;
    uvc_stm_state.payload_run_count = 0U;

    backend_state.frame_ptr = uvc_stm_last_payload.frame_ptr;
    backend_state.frame_size = uvc_stm_last_payload.frame_size;
    backend_state.packet_index = uvc_stm_last_payload.packet_index;
    {
        uint16_t tx_packet_size = UVC_GetPayloadTransferSize(hUsbDeviceHS.dev_speed);
        uint16_t payload_size = (tx_packet_size > UVC_HEADER_SIZE) ?
                                (uint16_t)(tx_packet_size - UVC_HEADER_SIZE) : 0U;
        if (payload_size != 0U)
        {
            backend_state.full_packets = backend_state.frame_size / payload_size;
            backend_state.remainder = backend_state.frame_size % payload_size;
        }
        else
        {
            backend_state.full_packets = 0U;
            backend_state.remainder = 0U;
        }
    }
    backend_state.last_packet = 0U;
    backend_state.wait_frame_interval = 0U;
    backend_state.next_frame_tick = 0U;
    backend_state.next_keepalive_tick = 0U;
    backend_state.next_payload_tick = 0U;

    uvc_stm_iso_replay_last_offset = uvc_stm_last_payload.offset;
    uvc_stm_iso_replay_last_packet_index = uvc_stm_last_payload.packet_index;
    uvc_stm_iso_replay_last_chunk = uvc_stm_last_payload.chunk;
    uvc_stm_iso_replay_applied++;
}

static uint8_t UVC_STM_SubmitPacket(USBD_HandleTypeDef *pdev, uint8_t epnum,
                                    const uint8_t *payload, uint16_t payload_len,
                                    uint8_t header_info)
{
    USBD_StatusTypeDef status;
    uint16_t total_len = (uint16_t)(payload_len + UVC_HEADER_SIZE);

    if (total_len > UVC_IN_PACKET)
    {
        payload_len = (uint16_t)(UVC_IN_PACKET - UVC_HEADER_SIZE);
        total_len = UVC_IN_PACKET;
    }

    if (uvc_stm_header_eoh_enable != 0U)
    {
        header_info |= UVC_PAYLOAD_EOH;
    }

    uvc_tx_packet[0] = UVC_HEADER_SIZE;
    uvc_tx_packet[1] = header_info;
    uvc_payload_header[0] = UVC_HEADER_SIZE;
    uvc_payload_header[1] = header_info;
    uvc_last_header_b0 = uvc_tx_packet[0];
    uvc_last_header_b1 = uvc_tx_packet[1];

    if ((payload != NULL) && (payload_len != 0U))
    {
        memcpy(&uvc_tx_packet[UVC_HEADER_SIZE], payload, payload_len);
    }

    uvc_ll_tx_calls++;
    uvc_ll_tx_last_epnum = epnum;
    uvc_ll_tx_last_len = total_len;
    uvc_dbg_payload_last_data_len = payload_len;
    uvc_dbg_payload_last_total_len = total_len;
    uvc_dbg_payload_last_eof = ((header_info & UVC_PAYLOAD_EOF) != 0U) ? 1U : 0U;
    uvc_stm_last_len = total_len;
    uvc_stm_last_header = header_info;

    status = USBD_LL_Transmit(pdev, (uint8_t)(epnum | 0x80U), uvc_tx_packet, total_len);
    uvc_ll_tx_last_status = (uint32_t)status;
    uvc_stm_last_status = (uint32_t)status;

    if (uvc_usb_reg_snapshot_enable != 0U)
    {
        USB_OTG_INEndpointTypeDef *in_ep_regs =
            (USB_OTG_INEndpointTypeDef *)((uint32_t)USB_OTG_HS +
                                          USB_OTG_IN_ENDPOINT_BASE +
                                          ((uint32_t)epnum * USB_OTG_EP_REG_SIZE));
        uvc_ll_tx_last_diepctl = in_ep_regs->DIEPCTL;
        uvc_ll_tx_last_dieptsiz = in_ep_regs->DIEPTSIZ;
        uvc_ll_tx_last_diepint = in_ep_regs->DIEPINT;
    }

    if (status != USBD_OK)
    {
        uvc_dbg_payload_send_fail++;
        return (uint8_t)USBD_FAIL;
    }

    if ((payload == NULL) || (payload_len == 0U))
    {
        UVC_STM_ClearPayloadCheckpoint();
    }

    uvc_class_last_tx_submit_tick = HAL_GetTick();
    uvc_ll_tx_ok++;
    uvc_ll_tx_ok_len = total_len;
    uvc_packets_sent++;
    uvc_dbg_payload_send_ok++;
    uvc_tx_in_flight_dbg = 1U;
    return (uint8_t)USBD_OK;
}

static uint8_t UVC_ShouldHoldPayloadForMicroframe7(void)
{
    uint32_t dsts;
    uint32_t fnsof;
    uint32_t microframe;

    if (uvc_skip_mf7_payload_enable == 0U)
    {
        return 0U;
    }

    dsts = ((USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_HS + USB_OTG_DEVICE_BASE))->DSTS;
    fnsof = (dsts & USB_OTG_DSTS_FNSOF_Msk) >> USB_OTG_DSTS_FNSOF_Pos;
    microframe = fnsof & 7U;
    uvc_skip_mf7_last_fnsof = fnsof;
    uvc_skip_mf7_last_mf = microframe;

    if (microframe >= 8U)
    {
        return 0U;
    }

    return (((uvc_skip_payload_mf_mask >> microframe) & 1U) != 0U) ? 1U : 0U;
}

static uint8_t UVC_FAST_CODE UVC_UNUSED_CODE UVC_STM_SendStartPayload(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    uint8_t status;

    uvc_stm_sof_start_calls++;
    uvc_start_payload_calls++;
    uvc_start_sof_delay_live = 0U;
    status = UVC_STM_SubmitPacket(pdev, epnum, NULL, 0U, uvc_stm_state.fid);
    if (status == (uint8_t)USBD_OK)
    {
        uvc_start_payload_ok++;
        uvc_stm_header_only_packets++;
    }
    else
    {
        uvc_start_payload_fail++;
    }

    return status;
}

static void UVC_STM_FinishFrame(void)
{
    if (uvc_stm_state.frame_size >= 4U)
    {
        const uint8_t *tail = uvc_stm_state.frame_ptr + uvc_stm_state.frame_size - 4U;
        uvc_last_payload_b0 = tail[0];
        uvc_last_payload_b1 = tail[1];
        uvc_last_payload_b2 = tail[2];
        uvc_last_payload_b3 = tail[3];

        if ((uvc_stm_state.frame_ptr[0] == 0xFFU) &&
            (uvc_stm_state.frame_ptr[1] == 0xD8U) &&
            (uvc_stm_state.frame_ptr[uvc_stm_state.frame_size - 2U] == 0xFFU) &&
            (uvc_stm_state.frame_ptr[uvc_stm_state.frame_size - 1U] == 0xD9U))
        {
            uvc_frame_integrity_ok++;
        }
        else
        {
            uvc_frame_integrity_fail++;
        }
    }

    video_source_commit_pending_if_any();
    uvc_frames_sent++;
    uvc_stm_frame_done++;
    uvc_stm_state.frame_active = 0U;
    uvc_stm_state.frame_ptr = NULL;
    uvc_stm_state.frame_size = 0U;
    uvc_stm_state.offset = 0U;
    uvc_stm_state.packet_index = 0U;
    uvc_stm_state.payload_run_count = 0U;
    if (uvc_frame_interval_ms != 0U)
    {
        uvc_stm_state.next_frame_tick = HAL_GetTick() + uvc_frame_interval_ms;
    }
    else
    {
        uvc_stm_state.next_frame_tick = 0U;
    }
    uvc_class_next_frame_tick_dbg = uvc_stm_state.next_frame_tick;
}

static uint8_t UVC_FAST_CODE UVC_UNUSED_CODE UVC_STM_SendNextPacket(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    const video_frame_t *frame;
    uint16_t tx_packet_size;
    uint16_t payload_capacity;
    uint32_t now = HAL_GetTick();

    tx_packet_size = UVC_GetPacketSize(pdev->dev_speed);
    if ((uvc_payload_packet_bytes >= UVC_HEADER_SIZE) &&
        (uvc_payload_packet_bytes < tx_packet_size))
    {
        tx_packet_size = (uint16_t)uvc_payload_packet_bytes;
    }

    if (tx_packet_size <= UVC_HEADER_SIZE)
    {
        tx_packet_size = UVC_GetPacketSize(pdev->dev_speed);
    }

    payload_capacity = (uint16_t)(tx_packet_size - UVC_HEADER_SIZE);
    uvc_payload_packet_bytes_dbg = tx_packet_size;

    if (uvc_stm_state.frame_active == 0U)
    {
        if ((uvc_frame_interval_ms != 0U) &&
            (uvc_stm_state.next_frame_tick != 0U) &&
            ((int32_t)(now - uvc_stm_state.next_frame_tick) < 0))
        {
            uvc_stm_pace_waits++;
            uvc_frame_pace_skips++;
            uvc_stm_header_only_packets++;
            return UVC_STM_SubmitPacket(pdev, epnum, NULL, 0U, uvc_stm_state.fid);
        }

        frame = video_source_get_current_frame();
        if ((frame == NULL) || (frame->data == NULL) || (frame->size == 0U))
        {
            uvc_stm_no_frame++;
            uvc_stream_start_no_frame++;
            uvc_stm_header_only_packets++;
            return UVC_STM_SubmitPacket(pdev, epnum, NULL, 0U, uvc_stm_state.fid);
        }

        uvc_stm_state.frame_ptr = frame->data;
        uvc_stm_state.frame_size = frame->size;
        uvc_stm_state.offset = 0U;
        uvc_stm_state.packet_index = 0U;
        uvc_stm_state.payload_run_count = 0U;
        uvc_stm_state.frame_active = 1U;
        uvc_stm_state.fid ^= UVC_PAYLOAD_FID;
        uvc_stm_state.next_frame_tick = 0U;
        uvc_stm_frame_loads++;
        uvc_last_frame_size = frame->size;
        uvc_current_frame_size_dbg = frame->size;
        uvc_current_frame_ptr_dbg = frame->data;

        if (frame->size >= 4U)
        {
            uvc_first_payload_b0 = frame->data[0];
            uvc_first_payload_b1 = frame->data[1];
            uvc_first_payload_b2 = frame->data[2];
            uvc_first_payload_b3 = frame->data[3];
        }

        if (uvc_prepacket_enable != 0U)
        {
            (void)UVC_PreparePrepackets(uvc_stm_state.frame_ptr,
                                        uvc_stm_state.frame_size,
                                        tx_packet_size);
        }
    }

    if ((uvc_prepacket_enable != 0U) &&
        (uvc_prepacket_frame_ptr == uvc_stm_state.frame_ptr) &&
        (uvc_prepacket_frame_size == uvc_stm_state.frame_size) &&
        (uvc_prepacket_packet_size == tx_packet_size) &&
        (uvc_prepacket_count != 0U) &&
        (uvc_stm_state.packet_index < uvc_prepacket_count))
    {
        uint16_t idx = (uint16_t)uvc_stm_state.packet_index;
        uint8_t header_info = uvc_stm_state.fid;
        uint16_t packet_len = uvc_prepacket_len[idx];
        uint16_t chunk = (packet_len > UVC_HEADER_SIZE) ?
                         (uint16_t)(packet_len - UVC_HEADER_SIZE) : 0U;
        uint8_t is_last = ((idx + 1U) == uvc_prepacket_count) ? 1U : 0U;
        uint8_t status;

        if (UVC_ShouldHoldPayloadForMicroframe7() != 0U)
        {
            uvc_skip_mf7_payload_packets++;
            uvc_skip_mf7_last_offset = uvc_prepacket_offset[idx];
            uvc_skip_mf7_last_packet_index = idx;
            uvc_stm_state.payload_run_count = 0U;
            uvc_stm_header_only_packets++;
            return UVC_STM_SubmitPacket(pdev, epnum, NULL, 0U, uvc_stm_state.fid);
        }

        if (UVC_STM_ShouldInsertPayloadGap() != 0U)
        {
            return UVC_STM_SendPayloadGap(pdev, epnum);
        }

        if (uvc_stm_header_eoh_enable != 0U)
        {
            header_info |= UVC_PAYLOAD_EOH;
        }
        if ((is_last != 0U) && (uvc_stm_header_eof_enable != 0U))
        {
            header_info |= UVC_PAYLOAD_EOF;
        }

        uvc_prepacket_buf[idx][0] = UVC_HEADER_SIZE;
        uvc_prepacket_buf[idx][1] = header_info;
        uvc_payload_header[0] = UVC_HEADER_SIZE;
        uvc_payload_header[1] = header_info;
        uvc_dbg_payload_last_offset = uvc_prepacket_offset[idx];
        uvc_last_offset = uvc_prepacket_offset[idx];
        uvc_stm_last_offset = uvc_prepacket_offset[idx];
        uvc_stm_last_packet_index = idx;
        uvc_stm_last_chunk = chunk;
        uvc_stm_last_header = header_info;
        uvc_stm_last_len = packet_len;
        uvc_prepacket_last_idx = idx;

        status = UVC_SendPreparedPacket(pdev, epnum, uvc_prepacket_buf[idx], packet_len);
        uvc_stm_last_status = uvc_ll_tx_last_status;
        if (status != (uint8_t)USBD_OK)
        {
            return status;
        }

        UVC_STM_RecordPayloadCheckpoint(uvc_prepacket_offset[idx],
                                        idx,
                                        chunk,
                                        is_last);
        if (uvc_stm_state.payload_run_count < 255U)
        {
            uvc_stm_state.payload_run_count++;
        }
        uvc_prepacket_hits++;
        uvc_stm_state.offset += chunk;
        uvc_stm_state.packet_index++;

        if (is_last != 0U)
        {
            UVC_STM_FinishFrame();
        }

        return (uint8_t)USBD_OK;
    }

    if ((uvc_stm_state.frame_ptr != NULL) && (uvc_stm_state.offset < uvc_stm_state.frame_size))
    {
        uint32_t remaining = uvc_stm_state.frame_size - uvc_stm_state.offset;
        uint16_t chunk = (remaining > payload_capacity) ? payload_capacity : (uint16_t)remaining;
        uint8_t header_info = uvc_stm_state.fid;
        const uint8_t *payload = &uvc_stm_state.frame_ptr[uvc_stm_state.offset];
        uint32_t packet_index = uvc_stm_state.packet_index;
        uint32_t offset = uvc_stm_state.offset;
        uint8_t is_last = (chunk == remaining) ? 1U : 0U;
        uint8_t status;

        if (UVC_ShouldHoldPayloadForMicroframe7() != 0U)
        {
            uvc_skip_mf7_payload_packets++;
            uvc_skip_mf7_last_offset = offset;
            uvc_skip_mf7_last_packet_index = packet_index;
            uvc_stm_state.payload_run_count = 0U;
            uvc_stm_header_only_packets++;
            return UVC_STM_SubmitPacket(pdev, epnum, NULL, 0U, uvc_stm_state.fid);
        }

        if (UVC_STM_ShouldInsertPayloadGap() != 0U)
        {
            return UVC_STM_SendPayloadGap(pdev, epnum);
        }

        if ((is_last != 0U) && (uvc_stm_header_eof_enable != 0U))
        {
            header_info |= UVC_PAYLOAD_EOF;
        }

        uvc_dbg_payload_last_offset = offset;
        uvc_last_offset = offset;
        uvc_stm_last_offset = offset;
        uvc_stm_last_packet_index = packet_index;
        uvc_stm_last_chunk = chunk;

        status = UVC_STM_SubmitPacket(pdev, epnum, payload, chunk, header_info);
        if (status != (uint8_t)USBD_OK)
        {
            return status;
        }

        UVC_STM_RecordPayloadCheckpoint(offset,
                                        packet_index,
                                        chunk,
                                        is_last);
        if (uvc_stm_state.payload_run_count < 255U)
        {
            uvc_stm_state.payload_run_count++;
        }
        uvc_stm_state.offset += chunk;
        uvc_stm_state.packet_index++;

        if (is_last != 0U)
        {
            UVC_STM_FinishFrame();
        }

        return (uint8_t)USBD_OK;
    }

    uvc_stm_header_only_packets++;
    uvc_stm_state.payload_run_count = 0U;
    return UVC_STM_SubmitPacket(pdev, epnum, NULL, 0U, uvc_stm_state.fid);
}

static uint8_t UVC_SendPayload(USBD_HandleTypeDef *pdev, uint8_t epnum, const uint8_t *payload, uint16_t packet_size,
                               uint16_t packet_index)
{
    USBD_StatusTypeDef status;
    uint16_t copy_len = 0U;

    if (packet_size < UVC_HEADER_SIZE)
    {
        packet_size = UVC_HEADER_SIZE;
    }

    uvc_payload_header[0] = UVC_HEADER_SIZE;
    uvc_payload_header[1] |= UVC_PAYLOAD_EOH;
    if ((payload == NULL) && (packet_size == UVC_HEADER_SIZE) && (uvc_dbg_state_live == UVC_STATE_READY))
    {
        uvc_payload_header[1] = (uint8_t)((uvc_payload_header[1] & UVC_PAYLOAD_FID) | UVC_PAYLOAD_EOH);
    }
    else if ((payload != NULL) && (packet_size > UVC_HEADER_SIZE) && (packet_index == 0U))
    {
        uvc_payload_header[1] ^= UVC_PAYLOAD_FID;
    }

    if ((payload != NULL) && (packet_size > UVC_HEADER_SIZE) && (backend_state.last_packet != 0U))
    {
        uvc_payload_header[1] |= UVC_PAYLOAD_EOF;
    }
    else
    {
        uvc_payload_header[1] &= (uint8_t)~UVC_PAYLOAD_EOF;
    }

    uvc_tx_packet[0] = uvc_payload_header[0];
    uvc_tx_packet[1] = uvc_payload_header[1];
    uvc_last_header_b0 = uvc_tx_packet[0];
    uvc_last_header_b1 = uvc_tx_packet[1];

    if ((payload != NULL) && (packet_size > UVC_HEADER_SIZE))
    {
        copy_len = (uint16_t)(packet_size - UVC_HEADER_SIZE);
        memcpy(&uvc_tx_packet[UVC_HEADER_SIZE], payload, copy_len);
    }

    uvc_ll_tx_calls++;
    uvc_ll_tx_last_epnum = epnum;
    uvc_ll_tx_last_len = packet_size;
    uvc_dbg_payload_last_data_len = copy_len;
    uvc_dbg_payload_last_total_len = packet_size;
    uvc_dbg_payload_last_eof =
        ((payload != NULL) && (packet_size > UVC_HEADER_SIZE) && (backend_state.last_packet != 0U)) ? 1U : 0U;

    status = USBD_LL_Transmit(pdev, (uint8_t)(epnum | 0x80U), uvc_tx_packet, packet_size);
    uvc_ll_tx_last_status = (uint32_t)status;
    if (uvc_usb_reg_snapshot_enable != 0U)
    {
        USB_OTG_INEndpointTypeDef *in_ep_regs =
            (USB_OTG_INEndpointTypeDef *)((uint32_t)USB_OTG_HS +
                                          USB_OTG_IN_ENDPOINT_BASE +
                                          ((uint32_t)epnum * USB_OTG_EP_REG_SIZE));
        uvc_ll_tx_last_diepctl = in_ep_regs->DIEPCTL;
        uvc_ll_tx_last_dieptsiz = in_ep_regs->DIEPTSIZ;
        uvc_ll_tx_last_diepint = in_ep_regs->DIEPINT;
    }

    if (status != USBD_OK)
    {
        uvc_dbg_payload_send_fail++;
        return (uint8_t)USBD_FAIL;
    }

    uvc_class_last_tx_submit_tick = HAL_GetTick();
    uvc_ll_tx_ok++;
    uvc_ll_tx_ok_len = packet_size;
    uvc_packets_sent++;
    if ((payload == NULL) && (packet_size == UVC_HEADER_SIZE) && (uvc_dbg_state_live == UVC_STATE_READY))
    {
        uvc_start_payload_ok++;
    }
    else
    {
        uvc_dbg_payload_send_ok++;
    }
    uvc_tx_in_flight_dbg = 1U;
    return (uint8_t)USBD_OK;
}

static uint8_t UVC_FAST_CODE UVC_SendPreparedPacket(USBD_HandleTypeDef *pdev, uint8_t epnum, uint8_t *packet, uint16_t packet_size)
{
    USBD_StatusTypeDef status;
    uint16_t data_len = 0U;

    if ((packet == NULL) || (packet_size < UVC_HEADER_SIZE))
    {
        uvc_dbg_payload_send_fail++;
        return (uint8_t)USBD_FAIL;
    }

    data_len = (uint16_t)(packet_size - UVC_HEADER_SIZE);
    uvc_last_header_b0 = packet[0];
    uvc_last_header_b1 = packet[1];

    uvc_ll_tx_calls++;
    uvc_ll_tx_last_epnum = epnum;
    uvc_ll_tx_last_len = packet_size;
    uvc_dbg_payload_last_data_len = data_len;
    uvc_dbg_payload_last_total_len = packet_size;
    uvc_dbg_payload_last_eof = ((packet[1] & UVC_PAYLOAD_EOF) != 0U) ? 1U : 0U;

    status = USBD_LL_Transmit(pdev, (uint8_t)(epnum | 0x80U), packet, packet_size);
    uvc_ll_tx_last_status = (uint32_t)status;
    if (uvc_usb_reg_snapshot_enable != 0U)
    {
        USB_OTG_INEndpointTypeDef *in_ep_regs =
            (USB_OTG_INEndpointTypeDef *)((uint32_t)USB_OTG_HS +
                                          USB_OTG_IN_ENDPOINT_BASE +
                                          ((uint32_t)epnum * USB_OTG_EP_REG_SIZE));
        uvc_ll_tx_last_diepctl = in_ep_regs->DIEPCTL;
        uvc_ll_tx_last_dieptsiz = in_ep_regs->DIEPTSIZ;
        uvc_ll_tx_last_diepint = in_ep_regs->DIEPINT;
    }

    if (status != USBD_OK)
    {
        uvc_dbg_payload_send_fail++;
        return (uint8_t)USBD_FAIL;
    }

    uvc_class_last_tx_submit_tick = HAL_GetTick();
    uvc_ll_tx_ok++;
    uvc_ll_tx_ok_len = packet_size;
    uvc_packets_sent++;
    uvc_dbg_payload_send_ok++;
    uvc_tx_in_flight_dbg = 1U;
    return (uint8_t)USBD_OK;
}

static uint8_t UVC_UNUSED_CODE UVC_SendSelectedPayload(USBD_HandleTypeDef *pdev, uint8_t epnum, uint8_t *payload,
                                                       uint16_t packet_size, uint16_t packet_index)
{
    if (((packet_index & UVC_PACKET_INDEX_PREBUILT) != 0U) && (payload != NULL))
    {
        return UVC_SendPreparedPacket(pdev, epnum, payload, packet_size);
    }

    return UVC_SendPayload(pdev, epnum, payload, packet_size,
                           (uint16_t)(packet_index & UVC_PACKET_INDEX_MASK));
}

static uint32_t UVC_FAST_CODE UVC_DWT_Begin(void)
{
    if (uvc_dwt_cycle_enable == 0U)
    {
        return 0U;
    }

    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    }

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        uvc_dwt_cycle_started++;
    }

    return DWT->CYCCNT;
}

static void UVC_FAST_CODE UVC_DWT_Record(volatile uint32_t *last, volatile uint32_t *max, uint32_t start)
{
    uint32_t elapsed;

    if ((uvc_dwt_cycle_enable == 0U) ||
        ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U))
    {
        return;
    }

    elapsed = DWT->CYCCNT - start;
    *last = elapsed;
    if (elapsed > *max)
    {
        *max = elapsed;
    }
}

static uint8_t UVC_FAST_CODE UVC_UNUSED_CODE UVC_STM_ExampleDataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    static uint8_t packet[UVC_IN_PACKET + UVC_HEADER_SIZE] __attribute__((aligned(32))) = {0x00U};
    static uint8_t *Pcktdata = packet;
    static uint16_t PcktIdx = 0U;
    static uint16_t PcktSze = UVC_IN_PACKET;
    static uint8_t payload_header[2] = {0x02U, 0x00U};
    USBD_UVC_ItfTypeDef *itf = UVC_GetItf(pdev);
    USBD_StatusTypeDef status;
    uint32_t tx_cycle_start;
    uint8_t header_info;
    uint16_t logical_idx;
    uint16_t copy_len = 0U;
    const uint8_t *payload_src = NULL;

    if ((itf != NULL) && (itf->Data != NULL))
    {
        (void)itf->Data(&Pcktdata, &PcktSze, &PcktIdx);
    }
    else
    {
        Pcktdata = NULL;
        PcktSze = 0U;
        PcktIdx = 0U;
    }

    if (PcktSze == 0U)
    {
        usbd_uvc_handle_t *huvc = UVC_GetHandle(pdev);

        uvc_stm_no_tx_packets++;
        if (huvc != NULL)
        {
            huvc->state = UVC_STATE_READY;
            uvc_dbg_state_live = UVC_STATE_READY;
        }
        uvc_tx_in_flight_dbg = 0U;
        return (uint8_t)USBD_FAIL;
    }

    if (PcktSze < UVC_HEADER_SIZE)
    {
        PcktSze = UVC_HEADER_SIZE;
    }
    if (PcktSze > UVC_IN_PACKET)
    {
        PcktSze = UVC_IN_PACKET;
    }

    uvc_stm_exact_datain_calls++;
    uvc_stm_exact_last_size = PcktSze;
    uvc_stm_exact_last_index = PcktIdx;
    logical_idx = (uint16_t)(PcktIdx & UVC_PACKET_INDEX_MASK);

    if ((PcktSze > UVC_HEADER_SIZE) && (Pcktdata != NULL))
    {
        if (logical_idx == 0U)
        {
            payload_header[1] ^= UVC_PAYLOAD_FID;
        }
        payload_header[1] &= UVC_PAYLOAD_FID;

        header_info = (uint8_t)(payload_header[1] & UVC_PAYLOAD_FID);
        if (uvc_stm_header_eoh_enable != 0U)
        {
            header_info |= UVC_PAYLOAD_EOH;
        }
        if ((uvc_stm_header_eof_enable != 0U) && (backend_state.last_packet != 0U))
        {
            header_info |= UVC_PAYLOAD_EOF;
        }

        if ((PcktIdx & UVC_PACKET_INDEX_PREBUILT) != 0U)
        {
            payload_src = &Pcktdata[UVC_HEADER_SIZE];
            uvc_ram_packet_direct_tx++;
        }
        else
        {
            payload_src = Pcktdata;
            UVC_RecordBackendPayloadCheckpoint(Pcktdata, PcktSze, logical_idx, payload_header[1]);
        }

        copy_len = (uint16_t)(PcktSze - UVC_HEADER_SIZE);
        packet[0] = UVC_HEADER_SIZE;
        packet[1] = header_info;
        memcpy(&packet[UVC_HEADER_SIZE], payload_src, copy_len);

        uvc_payload_header[0] = UVC_HEADER_SIZE;
        uvc_payload_header[1] = header_info;
        uvc_last_header_b0 = packet[0];
        uvc_last_header_b1 = packet[1];
        uvc_last_offset = uvc_dbg_payload_last_offset;
        uvc_stm_last_offset = uvc_dbg_payload_last_offset;
        uvc_stm_last_packet_index = logical_idx;
        uvc_stm_last_chunk = copy_len;
        uvc_stm_last_header = header_info;
    }
    else
    {
        header_info = (uint8_t)(payload_header[1] & UVC_PAYLOAD_FID);
        if (uvc_stm_header_eoh_enable != 0U)
        {
            header_info |= UVC_PAYLOAD_EOH;
        }
        packet[0] = UVC_HEADER_SIZE;
        packet[1] = header_info;
        if (uvc_fast_path_debug_enable != 0U)
        {
            uvc_stm_exact_short_packets++;
            uvc_stm_header_only_packets++;
        }
    }

    if (uvc_fast_path_debug_enable != 0U)
    {
        uvc_payload_header[0] = payload_header[0];
        uvc_payload_header[1] = packet[1];
        uvc_last_header_b0 = packet[0];
        uvc_last_header_b1 = packet[1];

        if (PcktSze > UVC_HEADER_SIZE)
        {
            uvc_last_offset = uvc_dbg_payload_last_offset;
            uvc_dbg_payload_last_data_len = (uint16_t)(PcktSze - UVC_HEADER_SIZE);
        }
        else
        {
            uvc_dbg_payload_last_data_len = 0U;
        }

        uvc_ll_tx_calls++;
        uvc_ll_tx_last_epnum = epnum;
        uvc_dbg_payload_last_total_len = PcktSze;
        uvc_dbg_payload_last_eof = ((packet[1] & UVC_PAYLOAD_EOF) != 0U) ? 1U : 0U;
        uvc_stm_last_len = PcktSze;
        uvc_stm_last_header = packet[1];
    }

    uvc_ll_tx_last_len = PcktSze;

    tx_cycle_start = UVC_DWT_Begin();
    status = USBD_LL_Transmit(pdev, (uint8_t)(epnum | 0x80U),
                              (uint8_t *)&packet, (uint32_t)PcktSze);
    UVC_DWT_Record(&uvc_lltx_cycles_last, &uvc_lltx_cycles_max, tx_cycle_start);
    uvc_ll_tx_last_status = (uint32_t)status;
    if (uvc_fast_path_debug_enable != 0U)
    {
        uvc_stm_last_status = (uint32_t)status;
    }

    if (status != USBD_OK)
    {
        if (uvc_fast_path_debug_enable != 0U)
        {
            uvc_dbg_payload_send_fail++;
        }
        return (uint8_t)USBD_FAIL;
    }

    if ((uvc_fast_path_debug_enable != 0U) || (uvc_tx_watchdog_timeout_ms != 0U))
    {
        uvc_class_last_tx_submit_tick = HAL_GetTick();
    }
    if (uvc_fast_path_debug_enable != 0U)
    {
        uvc_ll_tx_ok++;
        uvc_ll_tx_ok_len = PcktSze;
        uvc_packets_sent++;
        uvc_dbg_payload_send_ok++;
        uvc_tx_in_flight_dbg = 1U;
    }

    return (uint8_t)USBD_OK;
}

bool uvc_lowlevel_transmit(uint8_t *data, uint16_t len)
{
    PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)hUsbDeviceHS.pData;
    uint8_t epnum = (uint8_t)(UVC_IN_EP & 0x7FU);
    USB_OTG_INEndpointTypeDef *in_ep_regs;

    uvc_ll_tx_calls++;
    uvc_ll_tx_last_len = len;
    uvc_ll_tx_last_epnum = epnum;

    if (hpcd == NULL)
    {
        uvc_ll_tx_ret_hpcd_null++;
        return false;
    }

    if (hUsbDeviceHS.ep_in[epnum].is_used == 0U)
    {
        uvc_ll_tx_ret_ep_closed++;
        return false;
    }

    if (hpcd->IN_ep[epnum].maxpacket == 0U)
    {
        uvc_ll_tx_ret_maxpacket0++;
        return false;
    }

    if (uvc_usb_reg_snapshot_enable != 0U)
    {
        in_ep_regs = (USB_OTG_INEndpointTypeDef *)((uint32_t)USB_OTG_HS +
                                                   USB_OTG_IN_ENDPOINT_BASE +
                                                   ((uint32_t)epnum * USB_OTG_EP_REG_SIZE));
        uvc_ll_tx_last_diepctl = in_ep_regs->DIEPCTL;
        uvc_ll_tx_last_dieptsiz = in_ep_regs->DIEPTSIZ;
    }

    if (USBD_LL_Transmit(&hUsbDeviceHS, UVC_IN_EP, data, len) != USBD_OK)
    {
        uvc_ll_tx_ret_busy++;
        return false;
    }

    uvc_ll_tx_ok++;
    uvc_ll_tx_ok_len = len;
    return true;
}

bool uvc_lowlevel_reopen_stream_ep(void)
{
    return false;
}

void USBD_UVC_WatchdogPoll(void)
{
    usbd_uvc_handle_t *huvc = UVC_GetHandle(&hUsbDeviceHS);
    USB_OTG_INEndpointTypeDef *in_ep_regs;
    uint32_t now;
    uint32_t last_progress;
    uint32_t timeout;

    if ((huvc == NULL) || (current_alt_setting != 1U))
    {
        return;
    }

    if ((huvc->state != UVC_STATE_STREAMING) && (huvc->state != UVC_STATE_READY))
    {
        return;
    }

    timeout = uvc_tx_watchdog_timeout_ms;
    if (timeout == 0U)
    {
        return;
    }

    last_progress = uvc_last_tx_complete_tick;
    if (uvc_class_last_tx_submit_tick > last_progress)
    {
        last_progress = uvc_class_last_tx_submit_tick;
    }
    if (last_progress == 0U)
    {
        return;
    }

    now = HAL_GetTick();
    uvc_class_watchdog_state = huvc->state;
    uvc_class_watchdog_age = now - last_progress;

    if ((huvc->state == UVC_STATE_STREAMING) &&
        (uvc_class_watchdog_age > timeout))
    {
        in_ep_regs = (USB_OTG_INEndpointTypeDef *)((uint32_t)USB_OTG_HS +
                                                  USB_OTG_IN_ENDPOINT_BASE +
                                                  ((uint32_t)(UVC_IN_EP & 0x7FU) * USB_OTG_EP_REG_SIZE));
        uvc_class_watchdog_diepctl = in_ep_regs->DIEPCTL;
        uvc_class_watchdog_dieptsiz = in_ep_regs->DIEPTSIZ;
        uvc_class_watchdog_diepint = in_ep_regs->DIEPINT;

        uvc_runtime_state.ep_busy = 0U;
        uvc_tx_in_flight_dbg = 0U;
        if (uvc_runtime_state.frame_active != 0U)
        {
            uvc_runtime_state.drop_current_frame = 1U;
            UVC_RuntimeDropCurrentFrame();
        }
        (void)UVC_PrimeNextPacket(&hUsbDeviceHS);
        uvc_class_watchdog_recoveries++;
        uvc_tx_watchdog_recoveries++;
    }
}

static uint8_t USBD_UVC_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    usbd_uvc_handle_t *huvc = &uvc_class_handle;
    uint16_t packet_size;

    UNUSED(cfgidx);

    memset(huvc, 0, sizeof(*huvc));
    pdev->pClassData = huvc;
    if (pdev->pUserData == NULL)
    {
        pdev->pUserData = uvc_fops;
    }

    UVC_InitDefaultProbeCommit();
    packet_size = UVC_GetPacketSize(pdev->dev_speed);
    if (USBD_LL_OpenEP(pdev, UVC_IN_EP, USBD_EP_TYPE_ISOC, packet_size) != USBD_OK)
    {
        return (uint8_t)USBD_FAIL;
    }

    pdev->ep_in[UVC_IN_EP & 0xFU].is_used = 1U;
    pdev->ep_in[UVC_IN_EP & 0xFU].maxpacket = packet_size;
    current_alt_setting = 0U;
    uvc_payload_header[0] = 0x02U;
    uvc_payload_header[1] = 0x00U;
    uvc_stream_ep_open_dbg = 1U;
    uvc_pending_stream_start_dbg = 0U;

    if ((UVC_GetItf(pdev) != NULL) && (UVC_GetItf(pdev)->Init != NULL))
    {
        UVC_GetItf(pdev)->Init();
    }

    return (uint8_t)USBD_OK;
}

static uint8_t USBD_UVC_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);

    if (pdev->pClassData == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    (void)USBD_LL_CloseEP(pdev, UVC_IN_EP);
    pdev->ep_in[UVC_IN_EP & 0xFU].is_used = 0U;
    current_alt_setting = 0U;
    uvc_stream_ep_open_dbg = 0U;

    if ((UVC_GetItf(pdev) != NULL) && (UVC_GetItf(pdev)->DeInit != NULL))
    {
        UVC_GetItf(pdev)->DeInit();
    }

    pdev->pClassData = NULL;
    return (uint8_t)USBD_OK;
}

static uint8_t USBD_UVC_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    usbd_uvc_handle_t *huvc = UVC_GetHandle(pdev);
    uint8_t if_num = (uint8_t)(req->wIndex & 0xFFU);
    uint16_t cs = UVC_GetVsControlSelector(req->wValue);
    uint8_t ret = (uint8_t)USBD_OK;

    if (huvc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    uvc_last_setup_bmrequest = req->bmRequest;
    uvc_last_setup_brequest = req->bRequest;
    uvc_last_setup_wvalue = req->wValue;
    uvc_last_setup_windex = req->wIndex;
    uvc_last_setup_wlength = req->wLength;
    uvc_last_vs_control = cs;

    switch (req->bmRequest & USB_REQ_TYPE_MASK)
    {
    case USB_REQ_TYPE_CLASS:
        switch (req->bRequest)
        {
        case UVC_GET_CUR:
        case UVC_GET_MIN:
        case UVC_GET_MAX:
        case UVC_GET_DEF:
        case UVC_GET_RES:
        case UVC_GET_LEN:
        case UVC_GET_INFO:
            if (if_num != UVC_VS_IF_NUM)
            {
                break;
            }

            switch (req->bRequest)
            {
            case UVC_GET_CUR:
                if (cs == UVC_VS_PROBE_CONTROL)
                {
                    UVC_NormalizeProbeCommit(&uvc_probe, pdev->dev_speed);
                    UVC_DebugProbeCommit();
                    uvc_get_cur_probe_calls++;
                    UVC_SendProbeCommit(pdev, &uvc_probe, req->wLength);
                    return (uint8_t)USBD_OK;
                }
                if (cs == UVC_VS_COMMIT_CONTROL)
                {
                    UVC_NormalizeProbeCommit(&uvc_commit, pdev->dev_speed);
                    UVC_DebugProbeCommit();
                    uvc_get_cur_commit_calls++;
                    UVC_SendProbeCommit(pdev, &uvc_commit, req->wLength);
                    return (uint8_t)USBD_OK;
                }
                break;

            case UVC_GET_MIN:
                UVC_NormalizeProbeCommit(&uvc_probe, pdev->dev_speed);
                uvc_get_min_calls++;
                UVC_SendProbeCommit(pdev, &uvc_probe, req->wLength);
                return (uint8_t)USBD_OK;

            case UVC_GET_MAX:
                UVC_NormalizeProbeCommit(&uvc_probe, pdev->dev_speed);
                uvc_get_max_calls++;
                UVC_SendProbeCommit(pdev, &uvc_probe, req->wLength);
                return (uint8_t)USBD_OK;

            case UVC_GET_DEF:
                UVC_NormalizeProbeCommit(&uvc_probe, pdev->dev_speed);
                uvc_get_def_calls++;
                UVC_SendProbeCommit(pdev, &uvc_probe, req->wLength);
                return (uint8_t)USBD_OK;

            case UVC_GET_RES:
                memset(&uvc_res, 0, sizeof(uvc_res));
                uvc_res.dwFrameInterval = UVC_FRAME_INTERVAL_100NS;
                uvc_get_res_calls++;
                UVC_SendProbeCommit(pdev, &uvc_res, req->wLength);
                return (uint8_t)USBD_OK;

            case UVC_GET_LEN:
                ep0_len_buf = sizeof(uvc_probe_commit_t);
                uvc_get_len_calls++;
                (void)USBD_CtlSendData(pdev, (uint8_t *)&ep0_len_buf, 2U);
                return (uint8_t)USBD_OK;

            case UVC_GET_INFO:
                ep0_info_buf[0] = 0x03U;
                uvc_get_info_calls++;
                (void)USBD_CtlSendData(pdev, ep0_info_buf, 1U);
                return (uint8_t)USBD_OK;

            default:
                break;
            }
            break;

        case UVC_SET_CUR:
            if ((if_num == UVC_VS_IF_NUM) &&
                ((cs == UVC_VS_PROBE_CONTROL) || (cs == UVC_VS_COMMIT_CONTROL)) &&
                (req->wLength <= sizeof(huvc->ep0_buf)))
            {
                huvc->pending_control = cs;
                uvc_last_set_cur_wlength = req->wLength;
                memset(huvc->ep0_buf, 0, sizeof(huvc->ep0_buf));
                (void)USBD_CtlPrepareRx(pdev, huvc->ep0_buf, req->wLength);
                return (uint8_t)USBD_OK;
            }
            break;

        default:
            break;
        }
        break;

    case USB_REQ_TYPE_STANDARD:
        switch (req->bRequest)
        {
        case USB_REQ_GET_STATUS:
            if (pdev->dev_state == USBD_STATE_CONFIGURED)
            {
                ep0_status_buf = 0U;
                (void)USBD_CtlSendData(pdev, (uint8_t *)&ep0_status_buf, 2U);
                return (uint8_t)USBD_OK;
            }
            break;

        case USB_REQ_GET_DESCRIPTOR:
            if ((req->wValue >> 8) == CS_DEVICE)
            {
                uint16_t len = MIN((uint16_t)(UVC_ConfigDescSize - 18U), (uint16_t)req->wLength);
                memcpy(ep0_class_desc_buf, UVC_ConfigDesc + 18, len);
                (void)USBD_CtlSendData(pdev, ep0_class_desc_buf, len);
                return (uint8_t)USBD_OK;
            }
            break;

        case USB_REQ_GET_INTERFACE:
            uvc_get_interface_calls++;
            uvc_get_interface_last_if = if_num;
            if (if_num == UVC_VS_IF_NUM)
            {
                ep0_info_buf[0] = current_alt_setting;
                uvc_get_interface_last_value = current_alt_setting;
                (void)USBD_CtlSendData(pdev, ep0_info_buf, 1U);
                return (uint8_t)USBD_OK;
            }
            if (if_num == UVC_VC_IF_NUM)
            {
                ep0_info_buf[0] = 0U;
                uvc_get_interface_last_value = 0U;
                (void)USBD_CtlSendData(pdev, ep0_info_buf, 1U);
                return (uint8_t)USBD_OK;
            }
            break;

        case USB_REQ_SET_INTERFACE:
            if (if_num == UVC_VS_IF_NUM)
            {
                uvc_set_interface_calls++;
                uvc_set_interface_last_if = if_num;
                current_alt_setting = (uint8_t)(req->wValue & 0xFFU);
                uvc_last_set_interface_alt = current_alt_setting;
                huvc->interface = current_alt_setting;

                if (current_alt_setting == 1U)
                {
                    uvc_set_interface_alt1_calls++;
                    uvc_runtime_dbg.cnt_set_interface_alt1++;
                    uvc_pending_stream_start_dbg = 1U;
                    if (uvc_runtime_flush_policy != UVC_FLUSH_POLICY_NONE)
                    {
                        (void)UVC_FlushStreamEP(pdev, UVC_FLUSH_REASON_ALT);
                    }
                    if (UVC_GetItf(pdev) != NULL)
                    {
                        UVC_GetItf(pdev)->Start();
                    }
                    UVC_RuntimeReset(1U);
                    huvc->state = UVC_STATE_READY;
                    uvc_dbg_state_live = UVC_STATE_READY;
                }
                else
                {
                    uvc_set_interface_alt0_calls++;
                    uvc_runtime_dbg.cnt_set_interface_alt0++;
                    uvc_pending_stream_start_dbg = 0U;
                    UVC_RuntimeReset(0U);
                    huvc->state = UVC_STATE_STOP;
                    uvc_dbg_state_live = UVC_STATE_STOP;
                    if (UVC_GetItf(pdev) != NULL)
                    {
                        UVC_GetItf(pdev)->Stop();
                    }
                    if (uvc_runtime_flush_policy != UVC_FLUSH_POLICY_NONE)
                    {
                        (void)UVC_FlushStreamEP(pdev, UVC_FLUSH_REASON_ALT);
                    }
                }
                return (uint8_t)USBD_OK;
            }
            break;

        default:
            break;
        }
        break;

    default:
        break;
    }

    USBD_CtlError(pdev, req);
    ret = (uint8_t)USBD_FAIL;
    return ret;
}

static uint8_t USBD_UVC_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
    usbd_uvc_handle_t *huvc = UVC_GetHandle(pdev);

    if (huvc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    if (huvc->pending_control == UVC_VS_PROBE_CONTROL)
    {
        memcpy(&uvc_probe, huvc->ep0_buf, sizeof(uvc_probe));
        UVC_NormalizeProbeCommit(&uvc_probe, pdev->dev_speed);
        UVC_DebugProbeCommit();
        uvc_set_probe_calls++;
    }
    else if (huvc->pending_control == UVC_VS_COMMIT_CONTROL)
    {
        memcpy(&uvc_commit, huvc->ep0_buf, sizeof(uvc_commit));
        UVC_NormalizeProbeCommit(&uvc_commit, pdev->dev_speed);
        UVC_DebugProbeCommit();
        uvc_set_commit_calls++;
        if ((UVC_GetItf(pdev) != NULL) && (UVC_GetItf(pdev)->Control != NULL))
        {
            UVC_GetItf(pdev)->Control(&uvc_commit);
        }
    }

    huvc->pending_control = 0U;
    return (uint8_t)USBD_OK;
}

static uint8_t USBD_UVC_SOF(USBD_HandleTypeDef *pdev)
{
    usbd_uvc_handle_t *huvc = UVC_GetHandle(pdev);

    if (huvc == NULL)
    {
        return (uint8_t)USBD_FAIL;
    }

    uvc_runtime_dbg.cnt_sof++;
    uvc_sof_start_calls++;

    if ((uvc_runtime_state.streaming_enabled != 0U) &&
        (uvc_runtime_state.ep_busy == 0U))
    {
        uvc_runtime_dbg.cnt_sof_idle_prime++;
        uvc_stm_sof_direct_payload_calls++;
        uvc_start_payload_calls++;
        if (UVC_PrimeNextPacket(pdev) == (uint8_t)USBD_OK)
        {
            uvc_stm_sof_direct_payload_ok++;
            uvc_start_payload_ok++;
            uvc_initial_process_calls++;
            uvc_sof_start_process_calls++;
        }
        else
        {
            uvc_stm_sof_direct_payload_fail++;
            uvc_start_payload_fail++;
        }
    }
    else if ((uvc_runtime_state.streaming_enabled != 0U) &&
             (uvc_runtime_state.ep_busy != 0U))
    {
        uvc_runtime_dbg.cnt_sof_busy++;
        if ((uvc_runtime_busy_timeout_ms != 0U) &&
            (uvc_runtime_dbg.last_submit_tick != 0U))
        {
            uint32_t now = HAL_GetTick();
            uint32_t age = now - uvc_runtime_dbg.last_submit_tick;

            if (age >= uvc_runtime_busy_timeout_ms)
            {
                uvc_runtime_dbg.cnt_busy_timeout++;
                uvc_runtime_dbg.busy_age_ms = age;
                UVC_SnapshotInEpRegs((uint8_t)(UVC_IN_EP & 0x7FU),
                                     &uvc_runtime_dbg.busy_diepctl,
                                     &uvc_runtime_dbg.busy_dieptsiz,
                                     &uvc_runtime_dbg.busy_diepint);

                if (uvc_runtime_flush_policy != UVC_FLUSH_POLICY_NONE)
                {
                    (void)UVC_FlushStreamEP(pdev, UVC_FLUSH_REASON_BUSY);
                }
                uvc_runtime_state.ep_busy = 0U;
                uvc_runtime_state.last_packet_was_eof = 0U;
                uvc_tx_in_flight_dbg = 0U;

                if (uvc_runtime_state.frame_active != 0U)
                {
                    uvc_runtime_state.drop_current_frame = 1U;
                    UVC_RuntimeDropCurrentFrame();
                }
                else
                {
                    UVC_RuntimePublish();
                }

                (void)UVC_PrimeNextPacket(pdev);
            }
        }
    }

    return (uint8_t)USBD_OK;
}

static uint8_t UVC_FAST_CODE USBD_UVC_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    usbd_uvc_handle_t *huvc = UVC_GetHandle(pdev);
    uint32_t cycle_start = UVC_DWT_Begin();

    uvc_datain_calls++;
    uvc_runtime_dbg.cnt_data_in++;
    uvc_tx_complete_calls++;
    uvc_datain_last_ep = epnum;
    uvc_last_tx_complete_tick = HAL_GetTick();
    uvc_runtime_dbg.last_complete_tick = uvc_last_tx_complete_tick;
    uvc_tx_in_flight_dbg = 0U;

    if ((huvc == NULL) || (epnum != (UVC_IN_EP & 0x7FU)))
    {
        uvc_runtime_dbg.cnt_data_in_bad_ep++;
        UVC_DWT_Record(&uvc_datain_cycles_last, &uvc_datain_cycles_max, cycle_start);
        return (uint8_t)USBD_OK;
    }

    if (uvc_runtime_state.streaming_enabled == 0U)
    {
        uvc_datain_inactive_calls++;
        UVC_DWT_Record(&uvc_datain_cycles_last, &uvc_datain_cycles_max, cycle_start);
        return (uint8_t)USBD_OK;
    }

    uvc_runtime_state.ep_busy = 0U;
    uvc_datain_active_calls++;
    uvc_datain_process_calls++;
    uvc_dbg_payload_send_calls++;
    uvc_stm_datain_calls++;

    if (uvc_runtime_state.last_packet_was_eof != 0U)
    {
        UVC_RuntimeFinishFrame();
    }
    else
    {
        UVC_RuntimePublish();
    }

    if (UVC_PrimeNextPacket(pdev) != (uint8_t)USBD_OK)
    {
        huvc->state = UVC_STATE_READY;
        uvc_dbg_state_live = UVC_STATE_READY;
    }

    UVC_DWT_Record(&uvc_datain_cycles_last, &uvc_datain_cycles_max, cycle_start);
    return (uint8_t)USBD_OK;
}

static uint8_t USBD_UVC_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    usbd_uvc_handle_t *huvc = UVC_GetHandle(pdev);
    uint32_t now = HAL_GetTick();
    uint8_t idle_gap = 0U;

    uvc_class_iso_incomplete_calls++;
    uvc_runtime_dbg.cnt_iso_in_incomplete++;

    if ((huvc == NULL) || (epnum != (UVC_IN_EP & 0x7FU)))
    {
        uvc_runtime_dbg.cnt_iso_in_bad_ep++;
        return (uint8_t)USBD_OK;
    }

    uvc_runtime_state.ep_busy = 0U;
    uvc_tx_in_flight_dbg = 0U;
    uvc_iso_no_active_last_tick = now;
    uvc_iso_no_active_last_state = huvc->state;
    uvc_iso_no_active_last_next_frame_tick = uvc_runtime_state.next_frame_tick;
    UVC_SnapshotInEpRegs(epnum,
                         &uvc_iso_no_active_last_diepctl,
                         &uvc_iso_no_active_last_dieptsiz,
                         &uvc_iso_no_active_last_diepint);

    if ((uvc_runtime_state.frame_active == 0U) &&
        (uvc_runtime_state.next_frame_tick != 0U) &&
        ((int32_t)(now - uvc_runtime_state.next_frame_tick) < 0))
    {
        idle_gap = 1U;
    }

    if (uvc_runtime_state.frame_active != 0U)
    {
        uvc_runtime_state.drop_current_frame = 1U;
        UVC_RuntimeDropCurrentFrame();
    }
    else
    {
        uvc_class_iso_incomplete_no_active_frame++;
        uvc_runtime_state.last_packet_was_eof = 0U;
        if (idle_gap != 0U)
        {
            uvc_runtime_idle_iso_skips++;
            uvc_dbg_wait_frame_interval = 1U;
            UVC_RuntimePublish();
            return (uint8_t)USBD_OK;
        }

        UVC_RuntimePublish();
    }

    if ((uvc_runtime_flush_policy == UVC_FLUSH_POLICY_RECOVERY) &&
        (uvc_runtime_flush_on_iso_enable != 0U))
    {
        (void)UVC_FlushStreamEP(pdev, UVC_FLUSH_REASON_ISO);
    }

    if ((uvc_runtime_state.streaming_enabled != 0U) && (current_alt_setting == 1U))
    {
        uvc_class_iso_requeue_calls++;
        if (UVC_PrimeNextPacket(pdev) == (uint8_t)USBD_OK)
        {
            uvc_class_iso_requeue_ok++;
        }
        else
        {
            uvc_class_iso_requeue_fail++;
        }
    }

    return (uint8_t)USBD_OK;
}

static uint8_t *USBD_UVC_GetHSConfigDescriptor(uint16_t *length)
{
    UVC_DescDebugUpdate();
    *length = UVC_ConfigDescSize;
    return UVC_ConfigDesc;
}

static uint8_t *USBD_UVC_GetFSConfigDescriptor(uint16_t *length)
{
    UVC_DescDebugUpdate();
    *length = UVC_ConfigDescSize;
    return UVC_ConfigDesc;
}

static uint8_t *USBD_UVC_GetOtherSpeedConfigDescriptor(uint16_t *length)
{
    UVC_DescDebugUpdate();
    *length = UVC_ConfigDescSize;
    return UVC_ConfigDesc;
}

static uint8_t *USBD_UVC_GetDeviceQualifierDescriptor(uint16_t *length)
{
    *length = sizeof(UVC_DeviceQualifierDesc);
    return (uint8_t *)UVC_DeviceQualifierDesc;
}
