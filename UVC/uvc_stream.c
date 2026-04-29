#include "uvc_stream.h"
#include "video_source.h"
#include "usbd_uvc.h"
#include "stm32h7xx_hal.h"

#include <stdint.h>

#define UVC_PAYLOAD_HEADER_SIZE 2U
#define UVC_PAYLOAD_DATA_SIZE   (UVC_IN_PACKET - UVC_PAYLOAD_HEADER_SIZE)
#define UVC_PAYLOAD_FID         (1U << 0)
#define UVC_PAYLOAD_EOF         (1U << 1)
#define UVC_PAYLOAD_EOH         (1U << 7)

typedef struct
{
    uint8_t active;
    uint8_t tx_in_flight;
    uint8_t fid;
    uint8_t pending_eof;
    const uint8_t *frame_ptr;
    uint32_t frame_size;
    uint32_t offset;
    uint32_t pending_len;
} uvc_stream_ctx_t;

static uvc_stream_ctx_t ctx;
static uint8_t uvc_tx_packet[UVC_IN_PACKET] __attribute__((aligned(32)));
static uint16_t uvc_last_tx_packet_len = 0U;

/* debug */
volatile uint32_t uvc_packets_sent = 0;
volatile uint32_t uvc_frames_sent = 0;
volatile uint32_t uvc_stream_process_calls = 0;
volatile uint32_t uvc_tx_complete_calls = 0;
volatile uint32_t uvc_stream_start_calls = 0;
volatile uint32_t uvc_stream_start_attempts = 0;
volatile uint32_t uvc_stream_start_already_active = 0;
volatile uint32_t uvc_stream_start_no_frame = 0;
volatile uint32_t uvc_last_offset = 0;
volatile uint32_t uvc_last_frame_size = 0;
volatile uint32_t uvc_eof_hits = 0;
volatile uint32_t uvc_commit_attempts = 0;
volatile uint32_t uvc_current_frame_size_dbg = 0;
volatile const uint8_t *uvc_current_frame_ptr_dbg = 0;
volatile uint32_t uvc_frame_pace_skips = 0;
volatile uint8_t uvc_first_payload_b0 = 0;
volatile uint8_t uvc_first_payload_b1 = 0;
volatile uint8_t uvc_first_payload_b2 = 0;
volatile uint8_t uvc_first_payload_b3 = 0;
volatile uint8_t uvc_last_payload_b0 = 0;
volatile uint8_t uvc_last_payload_b1 = 0;
volatile uint8_t uvc_last_payload_b2 = 0;
volatile uint8_t uvc_last_payload_b3 = 0;
volatile uint8_t uvc_last_header_b0 = 0;
volatile uint8_t uvc_last_header_b1 = 0;
volatile uint32_t uvc_frame_interval_ms = UVC_FRAME_INTERVAL_MS;
volatile uint32_t uvc_frame_payload_bytes = 0;
volatile uint32_t uvc_frame_packet_count = 0;
volatile uint32_t uvc_last_frame_payload_bytes = 0;
volatile uint32_t uvc_last_frame_packet_count = 0;
volatile uint32_t uvc_last_frame_expected_packets = 0;
volatile uint32_t uvc_frame_integrity_ok = 0;
volatile uint32_t uvc_frame_integrity_fail = 0;
volatile uint32_t uvc_non_eof_short_packets = 0;
volatile uint32_t uvc_eof_short_packets = 0;
volatile uint32_t uvc_last_frame_checksum = 0;
volatile uint32_t uvc_frame_checksum = 0;
volatile uint32_t uvc_iso_frame_drops = 0;
volatile uint32_t uvc_recovery_eof_packets = 0;
volatile uint32_t uvc_frame_interval_releases = 0;
volatile uint32_t uvc_frame_preamble_packets = 0;
volatile uint32_t uvc_frame_keepalive_packets = 0;
volatile uint32_t uvc_frame_keepalive_enable = 1;
volatile uint32_t uvc_frame_keepalive_interval_ms = 8U;
volatile uint32_t uvc_start_payload_calls = 0;
volatile uint32_t uvc_start_payload_ok = 0;
volatile uint32_t uvc_start_payload_fail = 0;
volatile uint32_t uvc_first_payload_defer_calls = 0;
volatile uint32_t uvc_first_payload_releases = 0;
volatile uint32_t uvc_first_payload_wait_skips = 0;
volatile uint32_t uvc_first_payload_delay_ms = 0;
volatile uint32_t uvc_next_packet_main_releases = 0;
volatile uint32_t uvc_datain_direct_process_enable = 1;
volatile uint32_t uvc_state_dbg = 0;
volatile uint32_t uvc_last_process_tick = 0;
volatile uint32_t uvc_last_tx_ok_tick = 0;
volatile uint32_t uvc_last_tx_fail_tick = 0;
volatile uint32_t uvc_tx_payload_failures = 0;
volatile uint32_t uvc_tx_recovery_eof_failures = 0;
volatile uint32_t uvc_iso_incomplete_notified = 0;
volatile uint32_t uvc_iso_incomplete_retries = 0;
volatile uint32_t uvc_frame_preamble_enable = 0;
volatile uint32_t uvc_iso_retry_ready_events = 0;
volatile uint32_t uvc_iso_retry_wait_skips = 0;
volatile uint32_t uvc_iso_reopen_attempts = 0;
volatile uint32_t uvc_iso_reopen_ok = 0;
volatile uint32_t uvc_no_preamble_first_payloads = 0;
volatile uint32_t uvc_iso_drop_and_resync_calls = 0;
volatile uint32_t uvc_iso_resync_fid_toggles = 0;
volatile uint32_t uvc_iso_incomplete_ignored = 0;
volatile uint32_t uvc_iso_incomplete_minimal_calls = 0;
volatile uint32_t uvc_iso_incomplete_wait_next_frame = 0;
volatile uint32_t uvc_iso_incomplete_idle_reopen = 0;
volatile uint32_t uvc_iso_class_retry_calls = 0;
volatile uint32_t uvc_iso_class_retry_ok = 0;
volatile uint32_t uvc_iso_class_retry_fail = 0;
volatile uint32_t uvc_iso_class_retry_idle = 0;
volatile uint32_t uvc_iso_class_retry_suppressed = 0;
volatile uint32_t uvc_iso_immediate_resync_calls = 0;
volatile uint32_t uvc_iso_incomplete_continue_calls = 0;
volatile uint32_t uvc_iso_incomplete_drop_enable = 0;
volatile uint32_t uvc_iso_consecutive_incomplete_dbg = 0;
volatile uint32_t uvc_iso_reopen_after_incomplete_burst = 0;
volatile uint32_t uvc_iso_reopen_threshold = 0;
volatile uint32_t uvc_tx_in_flight_dbg = 0;
volatile uint32_t uvc_tx_watchdog_timeout_ms = 0U;
volatile uint32_t uvc_tx_watchdog_calls = 0;
volatile uint32_t uvc_tx_watchdog_recoveries = 0;
volatile uint32_t uvc_tx_watchdog_reopen_ok = 0;
volatile uint32_t uvc_last_tx_complete_tick = 0;
volatile uint32_t uvc_process_tx_in_flight_skips = 0;
volatile uint32_t uvc_dbg_state_live = 0;
volatile uint32_t uvc_dbg_wait_frame_interval = 0;
volatile uint32_t uvc_dbg_wait_first_payload = 0;
volatile uint32_t uvc_dbg_wait_next_packet = 0;
volatile uint32_t uvc_dbg_iso_retry_ready = 0;
volatile uint32_t uvc_dbg_pending_payload_active = 0;
volatile uint32_t uvc_dbg_pending_payload_len = 0;
volatile uint32_t uvc_dbg_offset_live = 0;
volatile uint32_t uvc_dbg_frame_size_live = 0;
volatile uint32_t uvc_dbg_frame_elapsed_ms = 0;
volatile uint32_t uvc_dbg_payload_send_calls = 0;
volatile uint32_t uvc_dbg_payload_send_ok = 0;
volatile uint32_t uvc_dbg_payload_send_fail = 0;
volatile uint32_t uvc_dbg_payload_last_offset = 0;
volatile uint32_t uvc_dbg_payload_last_data_len = 0;
volatile uint32_t uvc_dbg_payload_last_total_len = 0;
volatile uint32_t uvc_dbg_payload_last_eof = 0;
volatile uint32_t uvc_dbg_payload_first_toggle = 0;
volatile uint32_t uvc_dbg_commit_calls = 0;
volatile uint32_t uvc_dbg_commit_last_offset = 0;
volatile uint32_t uvc_stall_recover_enable = 0;
volatile uint32_t uvc_stall_recoveries = 0;
volatile uint32_t uvc_stall_recover_state = 0;
volatile uint32_t uvc_stall_recover_in_flight = 0;
volatile uint32_t uvc_stall_recover_reopen_attempts = 0;
volatile uint32_t uvc_stall_recover_reopen_ok = 0;
volatile uint32_t uvc_stall_recover_min_interval_ms = 0;
volatile uint32_t uvc_stall_recover_backoff_skips = 0;
volatile uint32_t uvc_frame_stall_guard_enable = 0;
volatile uint32_t uvc_frame_stall_timeout_ms = 0;
volatile uint32_t uvc_frame_stall_stops = 0;
volatile uint32_t uvc_frame_stall_direct_next_enable = 0;
volatile uint32_t uvc_frame_stall_force_next_calls = 0;
volatile uint32_t uvc_frame_stall_tick = 0;
volatile uint32_t uvc_frame_stall_frames_sent = 0;
volatile uint32_t uvc_frame_stall_tx_complete_calls = 0;
volatile uint32_t uvc_frame_stall_state = 0;
volatile uint32_t uvc_frame_stall_wait_frame_interval = 0;
volatile uint32_t uvc_frame_stall_offset = 0;
volatile uint32_t uvc_frame_stall_frame_size = 0;
volatile uint32_t uvc_recover_eof_sof_polls = 0;
volatile uint32_t uvc_recover_eof_timeout_ms = 0;
volatile uint32_t uvc_recover_eof_timeouts = 0;
volatile uint32_t uvc_recover_eof_timeout_state = 0;
volatile uint32_t uvc_recover_eof_timeout_in_flight = 0;
volatile uint32_t uvc_recover_eof_reopen_ok = 0;

__attribute__((weak))
bool uvc_lowlevel_reopen_stream_ep(void)
{
    return false;
}

__attribute__((weak))
bool uvc_lowlevel_transmit(uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    return false;
}

static void uvc_stream_debug_snapshot(void)
{
    uvc_dbg_state_live = ctx.active;
    uvc_dbg_wait_frame_interval = 0;
    uvc_dbg_wait_first_payload = 0;
    uvc_dbg_wait_next_packet = 0;
    uvc_dbg_iso_retry_ready = 0;
    uvc_dbg_pending_payload_active = ctx.tx_in_flight;
    uvc_dbg_pending_payload_len = ctx.pending_len;
    uvc_dbg_offset_live = ctx.offset;
    uvc_dbg_frame_size_live = ctx.frame_size;
    uvc_dbg_frame_elapsed_ms = 0;
    uvc_state_dbg = ctx.active;
    uvc_tx_in_flight_dbg = ctx.tx_in_flight;
}

static void uvc_stream_load_current_frame(void)
{
    const video_frame_t *f = video_source_get_current_frame();

    if (f == 0 || f->data == 0 || f->size == 0U)
    {
        ctx.active = 0U;
        ctx.frame_ptr = 0;
        ctx.frame_size = 0U;
        ctx.offset = 0U;
        uvc_current_frame_ptr_dbg = 0;
        uvc_current_frame_size_dbg = 0;
        return;
    }

    ctx.frame_ptr = f->data;
    ctx.frame_size = f->size;
    ctx.offset = 0U;
    ctx.pending_len = 0U;
    ctx.pending_eof = 0U;
    uvc_current_frame_ptr_dbg = f->data;
    uvc_current_frame_size_dbg = f->size;
    uvc_last_frame_size = f->size;
}

void uvc_stream_init(void)
{
    ctx.active = 0U;
    ctx.tx_in_flight = 0U;
    ctx.fid = 0U;
    ctx.pending_eof = 0U;
    ctx.frame_ptr = 0;
    ctx.frame_size = 0U;
    ctx.offset = 0U;
    ctx.pending_len = 0U;
    uvc_last_tx_complete_tick = HAL_GetTick();
    uvc_stream_debug_snapshot();
}

void uvc_stream_start(void)
{
    uvc_stream_start_attempts++;

    if (ctx.active != 0U)
    {
        uvc_stream_start_already_active++;
        return;
    }

    uvc_stream_load_current_frame();
    if (ctx.frame_ptr == 0 || ctx.frame_size == 0U)
    {
        uvc_stream_start_no_frame++;
        return;
    }

    ctx.active = 1U;
    ctx.tx_in_flight = 0U;
    uvc_stream_start_calls++;
    uvc_frame_payload_bytes = 0U;
    uvc_frame_packet_count = 0U;
    uvc_frame_checksum = 0U;
    uvc_last_offset = 0U;
    uvc_last_tx_packet_len = 0U;
    uvc_stream_debug_snapshot();
}

void uvc_stream_stop(void)
{
    ctx.active = 0U;
    ctx.tx_in_flight = 0U;
    ctx.pending_len = 0U;
    ctx.pending_eof = 0U;
    uvc_stream_debug_snapshot();
}

bool uvc_stream_is_active(void)
{
    uvc_stream_debug_snapshot();
    return (ctx.active != 0U);
}

bool uvc_stream_needs_sof_poll(void)
{
    return false;
}

bool uvc_stream_needs_iso_retry(void)
{
    return false;
}

bool uvc_stream_waiting_for_frame_interval(void)
{
    return false;
}

void uvc_stream_poll_pending(void)
{
}

bool uvc_stream_send_start_payload(void)
{
    uvc_start_payload_calls++;

    if (ctx.active == 0U || ctx.tx_in_flight != 0U)
    {
        uvc_start_payload_fail++;
        return false;
    }

    uvc_tx_packet[0] = UVC_PAYLOAD_HEADER_SIZE;
    uvc_tx_packet[1] = 0U;

    if (!uvc_lowlevel_transmit(uvc_tx_packet, UVC_PAYLOAD_HEADER_SIZE))
    {
        uvc_start_payload_fail++;
        uvc_last_tx_fail_tick = HAL_GetTick();
        uvc_tx_payload_failures++;
        return false;
    }

    ctx.tx_in_flight = 1U;
    ctx.pending_len = 0U;
    ctx.pending_eof = 0U;
    uvc_last_tx_packet_len = UVC_PAYLOAD_HEADER_SIZE;
    uvc_last_tx_ok_tick = HAL_GetTick();
    uvc_last_header_b0 = uvc_tx_packet[0];
    uvc_last_header_b1 = uvc_tx_packet[1];
    uvc_packets_sent++;
    uvc_start_payload_ok++;
    uvc_stream_debug_snapshot();
    return true;
}

void uvc_stream_process(void)
{
    uint32_t remaining;
    uint32_t to_copy;
    uint8_t eof = 0U;

    uvc_stream_process_calls++;
    uvc_last_process_tick = HAL_GetTick();

    if (ctx.active == 0U)
    {
        uvc_stream_debug_snapshot();
        return;
    }

    if (ctx.tx_in_flight != 0U)
    {
        uvc_process_tx_in_flight_skips++;
        uvc_stream_debug_snapshot();
        return;
    }

    if (ctx.frame_ptr == 0 || ctx.frame_size == 0U)
    {
        uvc_stream_load_current_frame();
        if (ctx.frame_ptr == 0 || ctx.frame_size == 0U)
        {
            uvc_stream_stop();
            return;
        }
    }

    if (ctx.offset == 0U)
    {
        ctx.fid ^= 1U;
        uvc_dbg_payload_first_toggle++;
        uvc_frame_payload_bytes = 0U;
        uvc_frame_packet_count = 0U;
        uvc_frame_checksum = 0U;
        if (ctx.frame_size >= 4U)
        {
            uvc_first_payload_b0 = ctx.frame_ptr[0];
            uvc_first_payload_b1 = ctx.frame_ptr[1];
            uvc_first_payload_b2 = ctx.frame_ptr[2];
            uvc_first_payload_b3 = ctx.frame_ptr[3];
        }
    }

    remaining = ctx.frame_size - ctx.offset;
    to_copy = remaining;
    if (to_copy > UVC_PAYLOAD_DATA_SIZE)
    {
        to_copy = UVC_PAYLOAD_DATA_SIZE;
    }

    uvc_tx_packet[0] = UVC_PAYLOAD_HEADER_SIZE;
    uvc_tx_packet[1] = UVC_PAYLOAD_EOH | ((ctx.fid & 1U) ? UVC_PAYLOAD_FID : 0U);
    for (uint32_t i = 0; i < to_copy; i++)
    {
        uint8_t b = ctx.frame_ptr[ctx.offset + i];
        uvc_tx_packet[UVC_PAYLOAD_HEADER_SIZE + i] = b;
        uvc_frame_checksum += b;
    }

    if ((ctx.offset + to_copy) >= ctx.frame_size)
    {
        eof = 1U;
        uvc_tx_packet[1] |= UVC_PAYLOAD_EOF;
    }

    uvc_dbg_payload_send_calls++;
    uvc_dbg_payload_last_offset = ctx.offset;
    uvc_dbg_payload_last_data_len = to_copy;
    uvc_dbg_payload_last_total_len = UVC_PAYLOAD_HEADER_SIZE + to_copy;
    uvc_dbg_payload_last_eof = eof;

    if (!uvc_lowlevel_transmit(uvc_tx_packet, (uint16_t)(UVC_PAYLOAD_HEADER_SIZE + to_copy)))
    {
        uvc_dbg_payload_send_fail++;
        uvc_last_tx_fail_tick = HAL_GetTick();
        uvc_tx_payload_failures++;
        uvc_stream_debug_snapshot();
        return;
    }

    uvc_dbg_payload_send_ok++;
    ctx.tx_in_flight = 1U;
    ctx.pending_len = to_copy;
    ctx.pending_eof = eof;
    uvc_last_tx_packet_len = (uint16_t)(UVC_PAYLOAD_HEADER_SIZE + to_copy);
    uvc_last_tx_ok_tick = HAL_GetTick();
    uvc_last_header_b0 = uvc_tx_packet[0];
    uvc_last_header_b1 = uvc_tx_packet[1];
    uvc_packets_sent++;
    uvc_stream_debug_snapshot();
}

void uvc_stream_on_tx_complete(void)
{
    uvc_tx_complete_calls++;
    uvc_last_tx_complete_tick = HAL_GetTick();
    ctx.tx_in_flight = 0U;

    if (ctx.active == 0U)
    {
        uvc_stream_debug_snapshot();
        return;
    }

    ctx.offset += ctx.pending_len;
    uvc_dbg_commit_calls++;
    uvc_dbg_commit_last_offset = ctx.offset;
    uvc_frame_payload_bytes += ctx.pending_len;
    uvc_frame_packet_count++;
    uvc_last_offset = ctx.offset;
    uvc_last_frame_size = ctx.frame_size;

    if (ctx.pending_eof != 0U)
    {
        uvc_eof_hits++;
        uvc_frames_sent++;
        uvc_last_frame_payload_bytes = uvc_frame_payload_bytes;
        uvc_last_frame_packet_count = uvc_frame_packet_count;
        uvc_last_frame_expected_packets =
            (ctx.frame_size + UVC_PAYLOAD_DATA_SIZE - 1U) / UVC_PAYLOAD_DATA_SIZE;
        uvc_last_frame_checksum = uvc_frame_checksum;

        if (ctx.frame_size >= 4U)
        {
            uvc_last_payload_b0 = ctx.frame_ptr[ctx.frame_size - 4U];
            uvc_last_payload_b1 = ctx.frame_ptr[ctx.frame_size - 3U];
            uvc_last_payload_b2 = ctx.frame_ptr[ctx.frame_size - 2U];
            uvc_last_payload_b3 = ctx.frame_ptr[ctx.frame_size - 1U];
        }

        if (ctx.frame_ptr != 0 &&
            ctx.frame_size >= 4U &&
            ctx.frame_ptr[0] == 0xFFU &&
            ctx.frame_ptr[1] == 0xD8U &&
            uvc_last_payload_b2 == 0xFFU &&
            uvc_last_payload_b3 == 0xD9U)
        {
            uvc_frame_integrity_ok++;
        }
        else
        {
            uvc_frame_integrity_fail++;
        }

        uvc_commit_attempts++;
        video_source_commit_pending_if_any();
        uvc_stream_load_current_frame();
    }

    ctx.pending_len = 0U;
    ctx.pending_eof = 0U;
    uvc_stream_debug_snapshot();
}

void uvc_stream_on_sof(void)
{
}

void uvc_stream_on_iso_incomplete(void)
{
    uvc_iso_incomplete_notified++;
}

void uvc_stream_on_iso_incomplete_minimal(void)
{
    uvc_iso_incomplete_notified++;
    uvc_iso_incomplete_minimal_calls++;
}

void uvc_stream_watchdog_poll(void)
{
    uvc_tx_watchdog_calls++;
    USBD_UVC_WatchdogPoll();
}

void uvc_stream_abort_frame(void)
{
    if (ctx.active == 0U)
    {
        return;
    }

    uvc_iso_frame_drops++;
    uvc_commit_attempts++;
    video_source_commit_pending_if_any();
    uvc_stream_load_current_frame();
    uvc_stream_debug_snapshot();
}

bool uvc_stream_retry_last_packet(void)
{
    uvc_iso_class_retry_calls++;
    uvc_iso_class_retry_fail++;
    return false;
}

uint32_t uvc_stream_get_offset(void)
{
    return ctx.offset;
}

uint32_t uvc_stream_get_packet_count(void)
{
    return uvc_frame_packet_count;
}

uint32_t uvc_stream_get_frame_size(void)
{
    return ctx.frame_size;
}

uint32_t uvc_stream_get_preamble_sent(void)
{
    return 0U;
}

uint32_t uvc_stream_get_reopen_pending(void)
{
    return 0U;
}
