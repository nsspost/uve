#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "usbd_ioreq.h"
#include "usbd_def.h"

#define UVC_IN_EP                    0x81U
#define UVC_IN_PACKET                512U
#define UVC_HS_EP_INTERVAL           1U
#define UVC_FRAME_INTERVAL_100NS     333333U
#define UVC_FRAME_INTERVAL_MS        ((UVC_FRAME_INTERVAL_100NS + 9999U) / 10000U)
#define UVC_FRAME_WIDTH              160U
#define UVC_FRAME_HEIGHT             120U
#define UVC_MAX_FRAME_SIZE           8192U
#define UVC_FRAME_RATE               30U
#define UVC_PRODUCER_INTERVAL_MS     200U
#define UVC_FRAME_BITRATE            (UVC_MAX_FRAME_SIZE * 8U * UVC_FRAME_RATE)

#define UVC_VC_IF_NUM                0U
#define UVC_VS_IF_NUM                1U

#define UVC_VC_EP0_SIZE              64U

#define UVC_PRIME_REASON_NONE        0U
#define UVC_PRIME_REASON_NO_HANDLE   1U
#define UVC_PRIME_REASON_DISABLED    2U
#define UVC_PRIME_REASON_ALT         3U
#define UVC_PRIME_REASON_BUSY        4U
#define UVC_PRIME_REASON_PAYLOAD     5U
#define UVC_PRIME_REASON_HEADER_ONLY 6U
#define UVC_PRIME_REASON_TX_FAIL     7U

typedef struct
{
    int8_t (*Init)(void);
    int8_t (*DeInit)(void);
    int8_t (*Start)(void);
    int8_t (*Stop)(void);
    int8_t (*Control)(const void *ctrl);
    int8_t (*Data)(uint8_t **pbuf, uint16_t *psize, uint16_t *pcktidx);
} USBD_UVC_ItfTypeDef;

typedef struct
{
    uint32_t streaming_enabled;
    uint32_t ep_busy;
    uint32_t drop_current_frame;
    uint32_t current_alt_setting;
    uint32_t huvc_state;
    uint32_t frame_active;
    uint32_t fid;
    uint32_t last_packet_was_eof;
    uint32_t cnt_sof;
    uint32_t cnt_sof_idle_prime;
    uint32_t cnt_sof_busy;
    uint32_t cnt_data_in;
    uint32_t cnt_data_in_bad_ep;
    uint32_t cnt_iso_in_incomplete;
    uint32_t cnt_iso_in_bad_ep;
    uint32_t cnt_transmit_fail;
    uint32_t cnt_underrun;
    uint32_t cnt_dropped_frames;
    uint32_t cnt_set_interface_alt0;
    uint32_t cnt_set_interface_alt1;
    uint32_t cnt_eof;
    uint32_t cnt_fid_toggle_eof;
    uint32_t cnt_fid_toggle_drop;
    uint32_t cnt_bad_jpeg_frame;
    uint32_t cnt_frame_load;
    uint32_t cnt_prime_calls;
    uint32_t cnt_prime_ok;
    uint32_t cnt_prime_skip_no_handle;
    uint32_t cnt_prime_skip_not_streaming;
    uint32_t cnt_prime_skip_alt;
    uint32_t cnt_prime_skip_busy;
    uint32_t cnt_header_only;
    uint32_t cnt_payload;
    uint32_t cnt_cache_clean;
    uint32_t cnt_busy_timeout;
    uint32_t cnt_flush_before_tx;
    uint32_t cnt_flush_before_tx_fail;
    uint32_t last_status;
    uint32_t last_flush_status;
    uint32_t last_prime_reason;
    uint32_t last_len;
    uint32_t last_header;
    uint32_t last_offset;
    uint32_t last_frame_size;
    uint32_t last_submit_tick;
    uint32_t last_complete_tick;
    uint32_t busy_age_ms;
    uint32_t busy_diepctl;
    uint32_t busy_dieptsiz;
    uint32_t busy_diepint;
    uint32_t next_frame_tick;
} uvc_runtime_diag_t;

extern USBD_ClassTypeDef USBD_UVC;

uint8_t USBD_UVC_RegisterInterface(USBD_HandleTypeDef *pdev, void *fops);
void USBD_UVC_WatchdogPoll(void);
bool uvc_lowlevel_reopen_stream_ep(void);

extern volatile uvc_runtime_diag_t uvc_runtime_dbg;
extern volatile uint32_t uvc_runtime_busy_timeout_ms;
extern volatile uint32_t uvc_runtime_flush_before_tx_enable;

extern volatile uint32_t uvc_class_iso_incomplete_calls;
extern volatile uint32_t uvc_class_iso_incomplete_retry_ok;
extern volatile uint32_t uvc_class_iso_retry_enable;
extern volatile uint32_t uvc_class_iso_incomplete_retry_disabled;
extern volatile uint32_t uvc_class_iso_drop_enable;
extern volatile uint32_t uvc_class_iso_frame_drops;
extern volatile uint32_t uvc_class_iso_drop_last_state;
extern volatile uint32_t uvc_class_iso_drop_last_tick;
extern volatile uint32_t uvc_class_iso_drop_last_offset;
extern volatile uint32_t uvc_class_iso_drop_last_frame_size;
extern volatile uint32_t uvc_class_iso_drop_last_backend_packet_index;
extern volatile uint32_t uvc_class_iso_drop_last_stm_packet_index;
extern volatile uint32_t uvc_class_iso_requeue_enable;
extern volatile uint32_t uvc_class_iso_requeue_calls;
extern volatile uint32_t uvc_class_iso_requeue_ok;
extern volatile uint32_t uvc_class_iso_requeue_fail;
extern volatile uint32_t uvc_class_iso_requeue_last_status;
extern volatile uint32_t uvc_class_iso_incomplete_no_active_frame;
extern volatile uint32_t uvc_class_iso_no_active_resync_enable;
extern volatile uint32_t uvc_class_iso_no_active_resync;
extern volatile uint32_t uvc_fast_path_debug_enable;
extern volatile uint32_t uvc_dwt_cycle_enable;
extern volatile uint32_t uvc_dwt_cycle_started;
extern volatile uint32_t uvc_datain_cycles_last;
extern volatile uint32_t uvc_datain_cycles_max;
extern volatile uint32_t uvc_lltx_cycles_last;
extern volatile uint32_t uvc_lltx_cycles_max;
extern volatile uint32_t uvc_datain_active_calls;
extern volatile uint32_t uvc_datain_process_calls;
extern volatile uint32_t uvc_datain_inactive_calls;
extern volatile uint32_t uvc_start_payload_enable;
extern volatile uint32_t uvc_start_sof_delay_cfg;
extern volatile uint32_t uvc_start_sof_delay_skips;
extern volatile uint32_t uvc_start_sof_delay_live;
extern volatile uint32_t uvc_sof_idle_restart_calls;
extern volatile uint32_t uvc_sof_idle_restart_ok;
extern volatile uint32_t uvc_stm_like_enable;
extern volatile uint32_t uvc_stm_header_eoh_enable;
extern volatile uint32_t uvc_stm_header_eof_enable;
extern volatile uint32_t uvc_stm_sof_start_calls;
extern volatile uint32_t uvc_stm_sof_direct_payload_enable;
extern volatile uint32_t uvc_stm_sof_direct_payload_calls;
extern volatile uint32_t uvc_stm_sof_direct_payload_ok;
extern volatile uint32_t uvc_stm_sof_direct_payload_fail;
extern volatile uint32_t uvc_stm_datain_calls;
extern volatile uint32_t uvc_stm_exact_datain_calls;
extern volatile uint32_t uvc_stm_exact_last_size;
extern volatile uint32_t uvc_stm_exact_last_index;
extern volatile uint32_t uvc_stm_exact_short_packets;
extern volatile uint32_t uvc_stm_header_only_packets;
extern volatile uint32_t uvc_stm_no_tx_packets;
extern volatile uint32_t uvc_stm_sof_wait_pending_calls;
extern volatile uint32_t uvc_stm_frame_loads;
extern volatile uint32_t uvc_stm_frame_done;
extern volatile uint32_t uvc_stm_no_frame;
extern volatile uint32_t uvc_stm_pace_waits;
extern volatile uint32_t uvc_stm_last_packet_index;
extern volatile uint32_t uvc_stm_last_offset;
extern volatile uint32_t uvc_stm_last_chunk;
extern volatile uint32_t uvc_stm_last_len;
extern volatile uint32_t uvc_stm_last_header;
extern volatile uint32_t uvc_stm_last_status;
extern volatile uint32_t uvc_stm_iso_replay_enable;
extern volatile uint32_t uvc_stm_iso_replay_requests;
extern volatile uint32_t uvc_stm_iso_replay_applied;
extern volatile uint32_t uvc_stm_iso_replay_no_payload;
extern volatile uint32_t uvc_stm_iso_replay_disabled;
extern volatile uint32_t uvc_stm_iso_replay_invalid;
extern volatile uint32_t uvc_stm_iso_replay_null_frame;
extern volatile uint32_t uvc_stm_iso_replay_zero_size;
extern volatile uint32_t uvc_stm_iso_replay_last_offset;
extern volatile uint32_t uvc_stm_iso_replay_last_packet_index;
extern volatile uint32_t uvc_stm_iso_replay_last_chunk;
extern volatile uint32_t uvc_min_source_enable;
extern volatile uint32_t uvc_min_usb_tx_complete;
extern volatile uint32_t uvc_min_data_calls;
extern volatile uint32_t uvc_min_header_only_packets;
extern volatile uint32_t uvc_min_no_pending_frames;
extern volatile uint32_t uvc_min_no_tx_wait_pending;
extern volatile uint32_t uvc_min_repeat_last_frames;
extern volatile uint32_t uvc_min_frame_loads;
extern volatile uint32_t uvc_min_frame_done;
extern volatile uint32_t uvc_min_last_packet_index;
extern volatile uint32_t uvc_min_last_offset;
extern volatile uint32_t uvc_min_last_frame_size;
extern volatile uint32_t uvc_min_last_packet_size;
extern volatile uint32_t uvc_ram_packet_enable;
extern volatile uint32_t uvc_ram_packet_ready_dbg;
extern volatile uint32_t uvc_ram_packet_builds;
extern volatile uint32_t uvc_ram_packet_hits;
extern volatile uint32_t uvc_ram_packet_direct_tx;
extern volatile uint32_t uvc_ram_packet_fallbacks;
extern volatile uint32_t uvc_ram_packet_total_packets;
extern volatile uint32_t uvc_ram_packet_last_frame;
extern volatile uint32_t uvc_ram_packet_last_idx;
extern volatile uint32_t uvc_ram_packet_last_count;
extern volatile uint32_t uvc_ram_packet_last_size;
extern volatile uint32_t uvc_stm_payload_gap_enable;
extern volatile uint32_t uvc_stm_payload_gap_after;
extern volatile uint32_t uvc_stm_payload_gap_packets;
extern volatile uint32_t uvc_skip_mf7_payload_enable;
extern volatile uint32_t uvc_skip_payload_mf_mask;
extern volatile uint32_t uvc_skip_mf7_payload_packets;
extern volatile uint32_t uvc_skip_mf7_last_fnsof;
extern volatile uint32_t uvc_skip_mf7_last_mf;
