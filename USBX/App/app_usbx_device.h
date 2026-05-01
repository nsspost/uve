#ifndef APP_USBX_DEVICE_H
#define APP_USBX_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ux_api.h"
#include <stdint.h>

#define USBX_TRACE_DEPTH             128U

#define USBX_TRACE_EVT_STREAM_CHANGE 0x56434847UL /* VCHG */
#define USBX_TRACE_EVT_STREAM_START  0x56535441UL /* VSTA */
#define USBX_TRACE_EVT_PAYLOAD_DONE  0x56444F4EUL /* VDON */
#define USBX_TRACE_EVT_VIDEO_COMMIT  0x56434F4DUL /* VCOM */
#define USBX_TRACE_EVT_VIDEO_EOF     0x56454F46UL /* VEOF */
#define USBX_TRACE_EVT_VIDEO_GETFAIL 0x56474554UL /* VGET */
#define USBX_TRACE_EVT_VIDEO_CMTFAIL 0x5643464CUL /* VCFL */
#define USBX_TRACE_EVT_VIDEO_RESYNC  0x56525359UL /* VRSY */
#define USBX_TRACE_EVT_VIDEO_PACE    0x56504143UL /* VPAC */
#define USBX_TRACE_EVT_DCD_SUBMIT    0x5355424DUL /* SUBM */
#define USBX_TRACE_EVT_DCD_DATAIN    0x44415449UL /* DATI */
#define USBX_TRACE_EVT_DCD_IISO      0x4949534FUL /* IISO */
#define USBX_TRACE_EVT_DCD_ISOINC    0x49534F49UL /* ISOI */
#define USBX_TRACE_EVT_DCD_IREC      0x49524543UL /* IREC */
#define USBX_TRACE_EVT_DCD_DONE      0x444F4E45UL /* DONE */
#define USBX_TRACE_EVT_DCD_WAIT      0x57414954UL /* WAIT */
#define USBX_TRACE_EVT_DCD_WDOG      0x57444F47UL /* WDOG */
#define USBX_TRACE_EVT_FREEZE        0x46525A45UL /* FRZE */
#define USBX_TRACE_EVT_EP81_HREC     0x48524543UL /* HREC */

#define USBX_FREEZE_REASON_MANUAL      0x4D414E55UL /* MANU */
#define USBX_FREEZE_REASON_NO_PROGRESS 0x4E505247UL /* NPRG */
#define USBX_FREEZE_REASON_TIME_GET    0x54494D45UL /* TIME */
#define USBX_FREEZE_REASON_VIDEO_ALT0  0x414C5430UL /* ALT0 */
#define USBX_FREEZE_REASON_VIDEO_ERROR 0x56455252UL /* VERR */
#define USBX_FREEZE_REASON_EP81_IDLE   0x45384944UL /* E8ID */

typedef struct USBX_TRACE_ENTRY_STRUCT
{
  ULONG seq;
  ULONG tick;
  ULONG event;
  ULONG a0;
  ULONG a1;
  ULONG a2;
  ULONG a3;
  ULONG a4;
  ULONG a5;
  ULONG a6;
  ULONG a7;
  ULONG a8;
  ULONG a9;
  ULONG a10;
  ULONG a11;
} USBX_TRACE_ENTRY;

typedef struct USBX_FREEZE_SNAPSHOT_STRUCT
{
  ULONG reason;
  ULONG tick;
  ULONG elapsed;
  ULONG count;
  ULONG trace_seq;
  ULONG trace_wr_idx;
  ULONG trace_last_idx;
  ULONG trace_last_event;
  ULONG progress_tick;
  ULONG progress_seq;
  ULONG progress_event;
  ULONG task_calls;
  ULONG device_state;
  ULONG device_speed;
  ULONG video_last_alt;
  ULONG video_start_status;
  ULONG video_stream_task_state;
  ULONG video_stream_task_status;
  ULONG video_payload_done;
  ULONG video_write_calls;
  ULONG video_last_state;
  ULONG video_img_count;
  ULONG video_packet_index;
  ULONG video_last_payload_len;
  ULONG video_last_frame_size;
  ULONG video_pace_hold_count;
  ULONG video_pace_release_count;
  ULONG video_pace_wait_ms;
  ULONG video_pace_now;
  ULONG video_pace_target;
  ULONG video_resync_count;
  ULONG video_iso_recovery_pending;
  ULONG ep81_stream_start_tick;
  ULONG ep81_last_submit_tick;
  ULONG ep81_last_datain_tick;
  ULONG ep81_last_done_tick;
  ULONG ep81_last_isoinc_tick;
  ULONG ep81_last_iiso_tick;
  ULONG ep81_last_irec_tick;
  ULONG ep81_last_payload_done_tick;
  ULONG ep81_idle_elapsed;
  ULONG ep81_idle_count;
  ULONG gintsts;
  ULONG gintmsk;
  ULONG gahbcfg;
  ULONG gusbcfg;
  ULONG dsts;
  ULONG daint;
  ULONG daintmsk;
  ULONG diepempmsk;
  ULONG diepctl;
  ULONG dieptsiz;
  ULONG diepint;
  ULONG dtxfsts;
  ULONG pcd_ep_xfer_buff;
  ULONG pcd_ep_xfer_len;
  ULONG pcd_ep_xfer_count;
  ULONG pcd_ep_maxpacket;
  ULONG pcd_ep_is_in;
  ULONG pcd_ep_is_iso_incomplete;
  ULONG pcd_ep_type;
  ULONG dcd_status;
  ULONG dcd_ed_status;
  ULONG dcd_ed_endpoint;
  ULONG endpoint_addr;
  ULONG endpoint_attributes;
  ULONG endpoint_mps;
  ULONG transfer_data;
  ULONG transfer_current_data;
  ULONG transfer_requested_len;
  ULONG transfer_actual_len;
  ULONG transfer_in_len;
  ULONG transfer_total_len;
  ULONG transfer_status;
  ULONG transfer_completion;
  ULONG transfer_phase;
  ULONG transfer_state;
} USBX_FREEZE_SNAPSHOT;

UINT MX_USBX_Device_Init(void);
void MX_USBX_Device_Process(void);
void USBX_TraceReset(void);
void USBX_FreezeCaptureNow(ULONG reason);
void USBX_FreezePoll(void);
void USBX_TraceLog(ULONG event,
                   ULONG a0,
                   ULONG a1,
                   ULONG a2,
                   ULONG a3,
                   ULONG a4,
                   ULONG a5,
                   ULONG a6,
                   ULONG a7,
                   ULONG a8,
                   ULONG a9,
                   ULONG a10,
                   ULONG a11);

extern volatile USBX_TRACE_ENTRY usbx_trace_log[USBX_TRACE_DEPTH];
extern volatile ULONG usbx_trace_enable_dbg;
extern volatile ULONG usbx_trace_wr_idx_dbg;
extern volatile ULONG usbx_trace_seq_dbg;
extern volatile ULONG usbx_trace_last_idx_dbg;
extern volatile ULONG usbx_trace_last_event_dbg;
extern volatile ULONG usbx_trace_iiso_suppressed_dbg;

extern volatile USBX_FREEZE_SNAPSHOT usbx_freeze_snapshot_dbg;
extern volatile ULONG usbx_freeze_enable_dbg;
extern volatile ULONG usbx_freeze_timeout_ms_dbg;
extern volatile ULONG usbx_freeze_poll_period_ms_dbg;
extern volatile ULONG usbx_freeze_count_dbg;
extern volatile ULONG usbx_freeze_frozen_dbg;
extern volatile ULONG usbx_freeze_progress_tick_dbg;
extern volatile ULONG usbx_freeze_progress_seq_dbg;
extern volatile ULONG usbx_freeze_progress_event_dbg;
extern volatile ULONG usbx_freeze_last_poll_tick_dbg;
extern volatile ULONG usbx_freeze_last_elapsed_dbg;
extern volatile ULONG usbx_freeze_alt0_capture_enable_dbg;
extern volatile ULONG usbx_ep81_watchdog_enable_dbg;
extern volatile ULONG usbx_ep81_watchdog_timeout_ms_dbg;
extern volatile ULONG usbx_ep81_stream_start_tick_dbg;
extern volatile ULONG usbx_ep81_last_submit_tick_dbg;
extern volatile ULONG usbx_ep81_last_datain_tick_dbg;
extern volatile ULONG usbx_ep81_last_done_tick_dbg;
extern volatile ULONG usbx_ep81_last_isoinc_tick_dbg;
extern volatile ULONG usbx_ep81_last_iiso_tick_dbg;
extern volatile ULONG usbx_ep81_last_irec_tick_dbg;
extern volatile ULONG usbx_ep81_last_payload_done_tick_dbg;
extern volatile ULONG usbx_ep81_idle_elapsed_dbg;
extern volatile ULONG usbx_ep81_idle_count_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_enable_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_pending_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_count_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_skip_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_last_tick_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_cooldown_ms_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_abort_status_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_flush_status_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_close_status_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_open_status_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_ed_status_before_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_ed_status_after_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_transfer_status_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_completion_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_mps_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_diepctl_before_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_dieptsiz_before_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_diepint_before_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_dtxfsts_before_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_diepctl_after_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_dieptsiz_after_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_diepint_after_dbg;
extern volatile ULONG usbx_ep81_hard_recovery_dtxfsts_after_dbg;

extern volatile ULONG usbx_init_status_dbg;
extern volatile ULONG usbx_pcd_init_status_dbg;
extern volatile ULONG usbx_dcd_init_status_dbg;
extern volatile ULONG usbx_hal_start_status_dbg;
extern volatile ULONG usbx_task_calls_dbg;
extern volatile ULONG usbx_device_state_dbg;
extern volatile ULONG usbx_device_speed_dbg;
extern volatile ULONG usbx_video_stream_change_dbg;
extern volatile ULONG usbx_video_payload_done_dbg;
extern volatile ULONG usbx_gintmsk_after_start_dbg;
extern volatile ULONG usbx_gintsts_after_start_dbg;
extern volatile ULONG usbx_gintmsk_after_iisoixfr_mask_dbg;
extern volatile ULONG usbx_iisoixfr_poll_count_dbg;
extern volatile ULONG usbx_iisoixfr_poll_last_gintsts_dbg;
extern volatile ULONG usbx_iisoixfr_poll_last_gintmsk_dbg;
extern volatile ULONG usbx_iisoixfr_poll_last_dsts_dbg;
extern volatile ULONG usbx_iisoixfr_poll_last_diepctl_dbg;
extern volatile ULONG usbx_iisoixfr_poll_last_dieptsiz_dbg;
extern volatile ULONG usbx_iisoixfr_poll_last_diepint_dbg;
extern volatile ULONG usbx_iisoixfr_poll_last_dtxfsts_dbg;
extern volatile ULONG usbx_iisoixfr_poll_first_gintsts_dbg;
extern volatile ULONG usbx_iisoixfr_poll_first_gintmsk_dbg;
extern volatile ULONG usbx_iisoixfr_poll_first_dsts_dbg;
extern volatile ULONG usbx_iisoixfr_poll_first_diepctl_dbg;
extern volatile ULONG usbx_iisoixfr_poll_first_dieptsiz_dbg;
extern volatile ULONG usbx_iisoixfr_poll_first_diepint_dbg;
extern volatile ULONG usbx_iisoixfr_poll_first_dtxfsts_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_enable_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_calls_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_skip_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_abort_status_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_flush_status_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_ed_status_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_transfer_status_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_req_len_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_diepctl_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_dieptsiz_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_diepint_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_dtxfsts_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_diepint_after_abort_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_diepint_after_clear_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_iso_flag_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_clear_diepint_enable_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_enable_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_max_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_count_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_calls_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_status_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_giveup_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_diepctl_after_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_dieptsiz_after_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_diepint_after_dbg;
extern volatile ULONG usbx_iisoixfr_recovery_retry_dtxfsts_after_dbg;
extern volatile ULONG usbx_video_iso_recovery_pending_dbg;
extern volatile ULONG usbx_video_resync_count_dbg;
extern volatile ULONG usbx_video_resync_delay_ms_dbg;
extern volatile ULONG usbx_video_last_alt_dbg;
extern volatile ULONG usbx_video_start_status_dbg;
extern volatile ULONG usbx_video_write_calls_dbg;
extern volatile ULONG usbx_video_get_status_dbg;
extern volatile ULONG usbx_video_commit_status_dbg;
extern volatile ULONG usbx_video_last_done_len_dbg;
extern volatile ULONG usbx_video_last_payload_len_dbg;
extern volatile ULONG usbx_video_last_buffer_len_dbg;
extern volatile ULONG usbx_video_last_state_dbg;
extern volatile ULONG usbx_video_img_count_dbg;
extern volatile ULONG usbx_video_packet_index_dbg;
extern volatile ULONG usbx_video_packets_per_frame_dbg;
extern volatile ULONG usbx_video_frame_eof_dbg;
extern volatile ULONG usbx_video_last_header_dbg;
extern volatile ULONG usbx_video_last_offset_dbg;
extern volatile ULONG usbx_video_last_frame_size_dbg;
extern volatile ULONG usbx_video_get_fail_dbg;
extern volatile ULONG usbx_video_commit_fail_dbg;
extern volatile ULONG usbx_video_frame_interval_100ns_dbg;
extern volatile ULONG usbx_video_stream_task_state_dbg;
extern volatile ULONG usbx_video_stream_task_status_dbg;
extern volatile ULONG usbx_video_stream_error_dbg;
extern volatile ULONG usbx_video_stream_buffer_error_count_dbg;
extern volatile ULONG usbx_video_stream_buffer_size_dbg;
extern volatile ULONG usbx_video_stream_payload_buffer_size_dbg;
extern volatile ULONG usbx_video_stream_transfer_pos_dbg;
extern volatile ULONG usbx_video_stream_access_pos_dbg;
extern volatile ULONG usbx_video_stream_transfer_len_dbg;
extern volatile ULONG usbx_video_stream_access_len_dbg;
extern volatile ULONG usbx_video_stream_endpoint_addr_dbg;
extern volatile ULONG usbx_video_stream_endpoint_mps_dbg;
extern volatile ULONG usbx_video_payload_flush_count_dbg;
extern volatile ULONG usbx_video_payload_flush_slots_dbg;
extern volatile ULONG usbx_video_payload_flush_nonzero_dbg;
extern volatile ULONG usbx_video_payload_flush_bytes_dbg;
extern volatile ULONG usbx_video_payload_flush_transfer_pos_dbg;
extern volatile ULONG usbx_video_payload_flush_access_pos_dbg;
extern volatile ULONG usbx_video_fid_dbg;
extern volatile ULONG usbx_video_resync_next_fid_dbg;
extern volatile ULONG usbx_video_resync_fid_action_dbg;
extern volatile ULONG usbx_video_resync_seen_packets_dbg;
extern volatile ULONG usbx_video_done_packets_in_frame_dbg;
extern volatile ULONG usbx_video_last_done_header_dbg;
extern volatile ULONG usbx_video_frame_delay_pending_dbg;
extern volatile ULONG usbx_video_frame_delay_enable_dbg;
extern volatile ULONG usbx_video_frame_delay_count_dbg;
extern volatile ULONG usbx_video_frame_delay_skipped_dbg;
extern volatile ULONG usbx_video_pace_hold_count_dbg;
extern volatile ULONG usbx_video_pace_release_count_dbg;
extern volatile ULONG usbx_video_pace_wait_ms_dbg;
extern volatile ULONG usbx_video_pace_now_dbg;
extern volatile ULONG usbx_video_pace_target_dbg;
extern volatile ULONG usbx_video_resync_delay_cfg_ms_dbg;
extern volatile ULONG usbx_video_resync_ll_recovery_cleared_dbg;
extern volatile ULONG usbx_video_payload_fill_calls_dbg;
extern volatile ULONG usbx_video_payload_fill_count_dbg;
extern volatile ULONG usbx_video_payload_fill_full_dbg;
extern volatile uint32_t usb_ll_iso_after_recovery_dbg;
extern volatile uint32_t usb_ll_iso_clear_iiso_before_submit_enable_dbg;
extern volatile uint32_t usb_ll_iso_clear_iiso_before_submit_used_dbg;
extern volatile uint32_t usb_ll_iso_gintsts_before_submit_clear_dbg;
extern volatile uint32_t usb_ll_iso_gintsts_after_submit_clear_dbg;

#ifdef __cplusplus
}
#endif

#endif /* APP_USBX_DEVICE_H */
