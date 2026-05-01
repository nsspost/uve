#include "app_usbx_device.h"

#include <string.h>

#include "main.h"
#include "stm32h7xx_ll_usb.h"
#include "ux_device_descriptors.h"
#include "ux_device_video.h"
#include "ux_dcd_stm32.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;
extern volatile uint32_t usb_gintmsk_before_iisoixfr_mask;
extern volatile uint32_t usb_gintmsk_after_iisoixfr_mask;
extern volatile uint32_t usb_mask_iisoixfr_enable;
extern volatile uint32_t usb_ll_iso_after_recovery_dbg;

#define USBX_DEVICE_MEMORY_STACK_SIZE (64U * 1024U)

#if defined(__GNUC__)
#define USBX_DEBUG_RETAIN __attribute__((used))
#else
#define USBX_DEBUG_RETAIN
#endif

__attribute__((section(".xsdram"), aligned(32)))
static UCHAR usbx_memory[USBX_DEVICE_MEMORY_STACK_SIZE];

static UX_DEVICE_CLASS_VIDEO_PARAMETER video_parameter;
static UX_DEVICE_CLASS_VIDEO_STREAM_PARAMETER video_stream_parameter[USBD_VIDEO_STREAM_NMNBER];
static uint8_t usbx_initialized;

__attribute__((used, aligned(4)))
volatile USBX_TRACE_ENTRY usbx_trace_log[USBX_TRACE_DEPTH];
volatile ULONG usbx_trace_enable_dbg = 1UL;
volatile ULONG usbx_trace_wr_idx_dbg = 0UL;
volatile ULONG usbx_trace_seq_dbg = 0UL;
volatile ULONG usbx_trace_last_idx_dbg = 0UL;
volatile ULONG usbx_trace_last_event_dbg = 0UL;
volatile ULONG usbx_trace_iiso_suppressed_dbg = 0UL;

USBX_DEBUG_RETAIN volatile USBX_FREEZE_SNAPSHOT usbx_freeze_snapshot_dbg;
volatile ULONG usbx_freeze_enable_dbg = 1UL;
volatile ULONG usbx_freeze_timeout_ms_dbg = 250UL;
volatile ULONG usbx_freeze_poll_period_ms_dbg = 10UL;
volatile ULONG usbx_freeze_count_dbg = 0UL;
volatile ULONG usbx_freeze_frozen_dbg = 0UL;
volatile ULONG usbx_freeze_progress_tick_dbg = 0UL;
volatile ULONG usbx_freeze_progress_seq_dbg = 0UL;
volatile ULONG usbx_freeze_progress_event_dbg = 0UL;
volatile ULONG usbx_freeze_last_poll_tick_dbg = 0UL;
volatile ULONG usbx_freeze_last_elapsed_dbg = 0UL;
volatile ULONG usbx_freeze_alt0_capture_enable_dbg = 0UL;
volatile ULONG usbx_ep81_watchdog_enable_dbg = 1UL;
volatile ULONG usbx_ep81_watchdog_timeout_ms_dbg = 3000UL;
volatile ULONG usbx_ep81_stream_start_tick_dbg = 0UL;
volatile ULONG usbx_ep81_last_submit_tick_dbg = 0UL;
volatile ULONG usbx_ep81_last_datain_tick_dbg = 0UL;
volatile ULONG usbx_ep81_last_done_tick_dbg = 0UL;
volatile ULONG usbx_ep81_last_isoinc_tick_dbg = 0UL;
volatile ULONG usbx_ep81_last_iiso_tick_dbg = 0UL;
volatile ULONG usbx_ep81_last_irec_tick_dbg = 0UL;
volatile ULONG usbx_ep81_last_payload_done_tick_dbg = 0UL;
volatile ULONG usbx_ep81_idle_elapsed_dbg = 0UL;
volatile ULONG usbx_ep81_idle_count_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_enable_dbg = 1UL;
volatile ULONG usbx_ep81_hard_recovery_pending_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_count_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_skip_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_last_tick_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_cooldown_ms_dbg = 1000UL;
volatile ULONG usbx_ep81_hard_recovery_abort_status_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_flush_status_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_close_status_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_open_status_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_ed_status_before_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_ed_status_after_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_transfer_status_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_completion_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_mps_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_diepctl_before_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_dieptsiz_before_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_diepint_before_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_dtxfsts_before_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_diepctl_after_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_dieptsiz_after_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_diepint_after_dbg = 0UL;
volatile ULONG usbx_ep81_hard_recovery_dtxfsts_after_dbg = 0UL;

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
volatile ULONG usbx_iisoixfr_poll_count_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_last_gintsts_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_last_gintmsk_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_last_dsts_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_last_diepctl_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_last_dieptsiz_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_last_diepint_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_last_dtxfsts_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_first_gintsts_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_first_gintmsk_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_first_dsts_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_first_diepctl_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_first_dieptsiz_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_first_diepint_dbg = 0UL;
volatile ULONG usbx_iisoixfr_poll_first_dtxfsts_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_enable_dbg = 1UL;
volatile ULONG usbx_iisoixfr_recovery_calls_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_skip_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_abort_status_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_flush_status_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_ed_status_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_transfer_status_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_req_len_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_diepctl_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_dieptsiz_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_diepint_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_dtxfsts_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_diepint_after_abort_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_diepint_after_clear_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_iso_flag_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_clear_diepint_enable_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_retry_enable_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_retry_max_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_retry_count_dbg = 0UL;
USBX_DEBUG_RETAIN volatile ULONG usbx_iisoixfr_recovery_retry_calls_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_retry_status_dbg = 0UL;
USBX_DEBUG_RETAIN volatile ULONG usbx_iisoixfr_recovery_retry_giveup_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_retry_diepctl_after_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_retry_dieptsiz_after_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_retry_diepint_after_dbg = 0UL;
volatile ULONG usbx_iisoixfr_recovery_retry_dtxfsts_after_dbg = 0UL;
volatile ULONG usbx_video_iso_recovery_pending_dbg = 0UL;
volatile ULONG usbx_video_resync_count_dbg = 0UL;
volatile ULONG usbx_video_resync_delay_ms_dbg = 0UL;

static uint32_t USBX_FreezeIsProgressEvent(ULONG event);
static void USBX_FreezeMarkProgress(ULONG event, ULONG seq, ULONG tick);
static void USBX_Ep81TraceMark(ULONG event, ULONG seq, ULONG tick, ULONG a0);
static void USBX_Ep81WatchdogPollAt(ULONG now_tick);
static void USBX_Ep81HardRecoveryPoll(void);
static void USBX_FreezeCapture(ULONG reason, ULONG now_tick);
static void USBX_FreezePollAt(ULONG now_tick, ULONG reason);

void USBX_TraceReset(void)
{
    uint32_t primask = __get_PRIMASK();
    ULONG i;

    __disable_irq();

    usbx_trace_wr_idx_dbg = 0UL;
    usbx_trace_seq_dbg = 0UL;
    usbx_trace_last_idx_dbg = 0UL;
    usbx_trace_last_event_dbg = 0UL;
    usbx_trace_iiso_suppressed_dbg = 0UL;
    usbx_freeze_frozen_dbg = 0UL;
    usbx_freeze_progress_tick_dbg = 0UL;
    usbx_freeze_progress_seq_dbg = 0UL;
    usbx_freeze_progress_event_dbg = 0UL;
    usbx_freeze_last_poll_tick_dbg = 0UL;
    usbx_freeze_last_elapsed_dbg = 0UL;
    usbx_ep81_stream_start_tick_dbg = 0UL;
    usbx_ep81_last_submit_tick_dbg = 0UL;
    usbx_ep81_last_datain_tick_dbg = 0UL;
    usbx_ep81_last_done_tick_dbg = 0UL;
    usbx_ep81_last_isoinc_tick_dbg = 0UL;
    usbx_ep81_last_iiso_tick_dbg = 0UL;
    usbx_ep81_last_irec_tick_dbg = 0UL;
    usbx_ep81_last_payload_done_tick_dbg = 0UL;
    usbx_ep81_idle_elapsed_dbg = 0UL;
    usbx_ep81_hard_recovery_pending_dbg = 0UL;
    usbx_ep81_hard_recovery_last_tick_dbg = 0UL;
    usbx_ep81_hard_recovery_abort_status_dbg = 0UL;
    usbx_ep81_hard_recovery_flush_status_dbg = 0UL;
    usbx_ep81_hard_recovery_close_status_dbg = 0UL;
    usbx_ep81_hard_recovery_open_status_dbg = 0UL;
    usbx_ep81_hard_recovery_ed_status_before_dbg = 0UL;
    usbx_ep81_hard_recovery_ed_status_after_dbg = 0UL;
    usbx_ep81_hard_recovery_transfer_status_dbg = 0UL;
    usbx_ep81_hard_recovery_completion_dbg = 0UL;
    usbx_ep81_hard_recovery_mps_dbg = 0UL;
    usbx_ep81_hard_recovery_diepctl_before_dbg = 0UL;
    usbx_ep81_hard_recovery_dieptsiz_before_dbg = 0UL;
    usbx_ep81_hard_recovery_diepint_before_dbg = 0UL;
    usbx_ep81_hard_recovery_dtxfsts_before_dbg = 0UL;
    usbx_ep81_hard_recovery_diepctl_after_dbg = 0UL;
    usbx_ep81_hard_recovery_dieptsiz_after_dbg = 0UL;
    usbx_ep81_hard_recovery_diepint_after_dbg = 0UL;
    usbx_ep81_hard_recovery_dtxfsts_after_dbg = 0UL;
    memset((void *)&usbx_freeze_snapshot_dbg, 0, sizeof(usbx_freeze_snapshot_dbg));

    for (i = 0UL; i < USBX_TRACE_DEPTH; i++)
    {
        usbx_trace_log[i].seq = 0UL;
        usbx_trace_log[i].tick = 0UL;
        usbx_trace_log[i].event = 0UL;
        usbx_trace_log[i].a0 = 0UL;
        usbx_trace_log[i].a1 = 0UL;
        usbx_trace_log[i].a2 = 0UL;
        usbx_trace_log[i].a3 = 0UL;
        usbx_trace_log[i].a4 = 0UL;
        usbx_trace_log[i].a5 = 0UL;
        usbx_trace_log[i].a6 = 0UL;
        usbx_trace_log[i].a7 = 0UL;
        usbx_trace_log[i].a8 = 0UL;
        usbx_trace_log[i].a9 = 0UL;
        usbx_trace_log[i].a10 = 0UL;
        usbx_trace_log[i].a11 = 0UL;
    }

    __set_PRIMASK(primask);
}

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
                   ULONG a11)
{
    uint32_t primask;
    ULONG idx;
    ULONG seq;
    ULONG tick;

    if (usbx_trace_enable_dbg == 0UL)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    idx = usbx_trace_wr_idx_dbg & (USBX_TRACE_DEPTH - 1UL);
    seq = usbx_trace_seq_dbg + 1UL;
    tick = (ULONG)HAL_GetTick();

    usbx_trace_log[idx].seq = seq;
    usbx_trace_log[idx].tick = tick;
    usbx_trace_log[idx].event = event;
    usbx_trace_log[idx].a0 = a0;
    usbx_trace_log[idx].a1 = a1;
    usbx_trace_log[idx].a2 = a2;
    usbx_trace_log[idx].a3 = a3;
    usbx_trace_log[idx].a4 = a4;
    usbx_trace_log[idx].a5 = a5;
    usbx_trace_log[idx].a6 = a6;
    usbx_trace_log[idx].a7 = a7;
    usbx_trace_log[idx].a8 = a8;
    usbx_trace_log[idx].a9 = a9;
    usbx_trace_log[idx].a10 = a10;
    usbx_trace_log[idx].a11 = a11;

    usbx_trace_seq_dbg = seq;
    usbx_trace_wr_idx_dbg++;
    usbx_trace_last_idx_dbg = idx;
    usbx_trace_last_event_dbg = event;
    USBX_Ep81TraceMark(event, seq, tick, a0);
    USBX_FreezeMarkProgress(event, seq, tick);

    __set_PRIMASK(primask);
}

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
    ULONG tick = (ULONG)HAL_GetTick();

    USBX_FreezePollAt(tick, USBX_FREEZE_REASON_TIME_GET);
    return tick;
}

ULONG _ux_utility_time_elapsed(ULONG start, ULONG now)
{
    return now - start;
}

static uint32_t USBX_FreezeIsProgressEvent(ULONG event)
{
    switch (event)
    {
    case USBX_TRACE_EVT_STREAM_CHANGE:
    case USBX_TRACE_EVT_STREAM_START:
    case USBX_TRACE_EVT_PAYLOAD_DONE:
    case USBX_TRACE_EVT_VIDEO_COMMIT:
    case USBX_TRACE_EVT_VIDEO_EOF:
    case USBX_TRACE_EVT_VIDEO_GETFAIL:
    case USBX_TRACE_EVT_VIDEO_CMTFAIL:
    case USBX_TRACE_EVT_VIDEO_RESYNC:
    case USBX_TRACE_EVT_VIDEO_PACE:
    case USBX_TRACE_EVT_DCD_SUBMIT:
    case USBX_TRACE_EVT_DCD_DATAIN:
    case USBX_TRACE_EVT_DCD_ISOINC:
    case USBX_TRACE_EVT_DCD_IREC:
    case USBX_TRACE_EVT_DCD_DONE:
    case USBX_TRACE_EVT_DCD_WDOG:
    case USBX_TRACE_EVT_EP81_HREC:
        return 1U;
    default:
        return 0U;
    }
}

static void USBX_FreezeMarkProgress(ULONG event, ULONG seq, ULONG tick)
{
    if ((usbx_freeze_frozen_dbg == 0UL) &&
        (USBX_FreezeIsProgressEvent(event) != 0U))
    {
        usbx_freeze_progress_tick_dbg = tick;
        usbx_freeze_progress_seq_dbg = seq;
        usbx_freeze_progress_event_dbg = event;
    }
}

static void USBX_Ep81TraceMark(ULONG event, ULONG seq, ULONG tick, ULONG a0)
{
    UX_PARAMETER_NOT_USED(seq);

    switch (event)
    {
    case USBX_TRACE_EVT_STREAM_CHANGE:
        if (a0 == 1UL)
        {
            usbx_ep81_stream_start_tick_dbg = tick;
            usbx_ep81_last_submit_tick_dbg = 0UL;
            usbx_ep81_last_datain_tick_dbg = 0UL;
            usbx_ep81_last_done_tick_dbg = 0UL;
            usbx_ep81_last_isoinc_tick_dbg = 0UL;
            usbx_ep81_last_iiso_tick_dbg = 0UL;
            usbx_ep81_last_irec_tick_dbg = 0UL;
            usbx_ep81_last_payload_done_tick_dbg = 0UL;
            usbx_ep81_idle_elapsed_dbg = 0UL;
            usbx_ep81_hard_recovery_pending_dbg = 0UL;
        }
        else if (a0 == 0UL)
        {
            usbx_ep81_stream_start_tick_dbg = 0UL;
            usbx_ep81_idle_elapsed_dbg = 0UL;
            usbx_ep81_hard_recovery_pending_dbg = 0UL;
        }
        break;

    case USBX_TRACE_EVT_STREAM_START:
        if ((usbx_video_last_alt_dbg != 0UL) &&
            (usbx_ep81_stream_start_tick_dbg == 0UL))
        {
            usbx_ep81_stream_start_tick_dbg = tick;
        }
        break;

    case USBX_TRACE_EVT_DCD_SUBMIT:
        if ((a0 & 0x8FUL) == 0x81UL)
        {
            usbx_ep81_last_submit_tick_dbg = tick;
        }
        break;

    case USBX_TRACE_EVT_DCD_DATAIN:
        if ((a0 & 0x0FUL) == 1UL)
        {
            usbx_ep81_last_datain_tick_dbg = tick;
            usbx_ep81_idle_elapsed_dbg = 0UL;
        }
        break;

    case USBX_TRACE_EVT_DCD_DONE:
        if ((a0 & 0x8FUL) == 0x81UL)
        {
            usbx_ep81_last_done_tick_dbg = tick;
        }
        break;

    case USBX_TRACE_EVT_DCD_ISOINC:
        if ((a0 & 0x0FUL) == 1UL)
        {
            usbx_ep81_last_isoinc_tick_dbg = tick;
        }
        break;

    case USBX_TRACE_EVT_DCD_IISO:
        usbx_ep81_last_iiso_tick_dbg = tick;
        break;

    case USBX_TRACE_EVT_DCD_IREC:
        if ((a0 & 0x8FUL) == 0x81UL)
        {
            usbx_ep81_last_irec_tick_dbg = tick;
        }
        break;

    case USBX_TRACE_EVT_PAYLOAD_DONE:
        usbx_ep81_last_payload_done_tick_dbg = tick;
        break;

    case USBX_TRACE_EVT_EP81_HREC:
        usbx_ep81_idle_elapsed_dbg = 0UL;
        break;

    default:
        break;
    }
}

static void USBX_Ep81WatchdogPollAt(ULONG now_tick)
{
    ULONG last_real_in_tick;
    ULONG elapsed;

    if ((usbx_ep81_watchdog_enable_dbg == 0UL) ||
        (usbx_video_last_alt_dbg == 0UL) ||
        (usbx_ep81_stream_start_tick_dbg == 0UL))
    {
        return;
    }

    if ((usbx_ep81_hard_recovery_last_tick_dbg != 0UL) &&
        ((now_tick - usbx_ep81_hard_recovery_last_tick_dbg) <
         usbx_ep81_hard_recovery_cooldown_ms_dbg))
    {
        usbx_ep81_idle_elapsed_dbg = 0UL;
        return;
    }

    last_real_in_tick = usbx_ep81_last_datain_tick_dbg;
    if (last_real_in_tick == 0UL)
    {
        if ((usbx_ep81_hard_recovery_count_dbg == 0UL) ||
            (usbx_ep81_hard_recovery_last_tick_dbg == 0UL))
        {
            usbx_ep81_idle_elapsed_dbg = 0UL;
            return;
        }
        last_real_in_tick = usbx_ep81_hard_recovery_last_tick_dbg;
    }
    if ((usbx_ep81_last_submit_tick_dbg <= last_real_in_tick) ||
        (usbx_ep81_last_isoinc_tick_dbg <= last_real_in_tick))
    {
        usbx_ep81_idle_elapsed_dbg = 0UL;
        return;
    }

    elapsed = now_tick - last_real_in_tick;
    usbx_ep81_idle_elapsed_dbg = elapsed;
    if (elapsed >= usbx_ep81_watchdog_timeout_ms_dbg)
    {
        usbx_ep81_idle_count_dbg++;
        if (usbx_ep81_hard_recovery_enable_dbg != 0UL)
        {
            usbx_ep81_hard_recovery_pending_dbg = 1UL;
        }
        USBX_FreezeCapture(USBX_FREEZE_REASON_EP81_IDLE, now_tick);
    }
}

static void USBX_Ep81HardRecoveryPoll(void)
{
    UX_INTERRUPT_SAVE_AREA
    UX_SLAVE_DCD *dcd;
    UX_DCD_STM32 *dcd_stm32;
    UX_DCD_STM32_ED *ed;
    UX_SLAVE_ENDPOINT *endpoint;
    UX_SLAVE_TRANSFER *transfer_request;
    USB_OTG_GlobalTypeDef *USBx = hpcd_USB_OTG_HS.Instance;
    USB_OTG_INEndpointTypeDef *in_ep_regs;
    uint32_t USBx_BASE;
    uint32_t ed_status;
    ULONG now_tick;
    uint16_t ep_mps;
    HAL_StatusTypeDef abort_status;
    HAL_StatusTypeDef flush_status;
    HAL_StatusTypeDef close_status;
    HAL_StatusTypeDef open_status;
    const uint8_t ep_addr = 0x81U;
    const uint8_t epnum = 1U;

    if ((usbx_ep81_hard_recovery_enable_dbg == 0UL) ||
        (usbx_ep81_hard_recovery_pending_dbg == 0UL))
    {
        return;
    }

    usbx_ep81_hard_recovery_pending_dbg = 0UL;
    now_tick = (ULONG)HAL_GetTick();

    if ((usbx_video_last_alt_dbg == 0UL) ||
        (USBx == UX_NULL) ||
        (_ux_system_slave == UX_NULL))
    {
        usbx_ep81_hard_recovery_skip_dbg++;
        return;
    }

    dcd = &_ux_system_slave->ux_system_slave_dcd;
    dcd_stm32 = (UX_DCD_STM32 *)dcd->ux_slave_dcd_controller_hardware;
    if (dcd_stm32 == UX_NULL)
    {
        usbx_ep81_hard_recovery_skip_dbg++;
        return;
    }

#if defined(UX_DEVICE_BIDIRECTIONAL_ENDPOINT_SUPPORT)
    ed = &dcd_stm32->ux_dcd_stm32_ed_in[epnum];
#else
    ed = &dcd_stm32->ux_dcd_stm32_ed[epnum];
#endif

    if ((ed->ux_dcd_stm32_ed_status & UX_DCD_STM32_ED_STATUS_USED) == 0U)
    {
        usbx_ep81_hard_recovery_skip_dbg++;
        return;
    }

    endpoint = ed->ux_dcd_stm32_ed_endpoint;
    if ((endpoint == UX_NULL) ||
        ((endpoint->ux_slave_endpoint_descriptor.bEndpointAddress & 0x8FU) != ep_addr))
    {
        usbx_ep81_hard_recovery_skip_dbg++;
        return;
    }

    ep_mps = endpoint->ux_slave_endpoint_descriptor.wMaxPacketSize;
    transfer_request = &endpoint->ux_slave_endpoint_transfer_request;
    USBx_BASE = (uint32_t)USBx;
    in_ep_regs = USBx_INEP(epnum);

    usbx_ep81_hard_recovery_diepctl_before_dbg = in_ep_regs->DIEPCTL;
    usbx_ep81_hard_recovery_dieptsiz_before_dbg = in_ep_regs->DIEPTSIZ;
    usbx_ep81_hard_recovery_diepint_before_dbg = in_ep_regs->DIEPINT;
    usbx_ep81_hard_recovery_dtxfsts_before_dbg = in_ep_regs->DTXFSTS;
    usbx_ep81_hard_recovery_ed_status_before_dbg = ed->ux_dcd_stm32_ed_status;
    usbx_ep81_hard_recovery_transfer_status_dbg =
        transfer_request->ux_slave_transfer_request_status;
    usbx_ep81_hard_recovery_completion_dbg =
        transfer_request->ux_slave_transfer_request_completion_code;
    usbx_ep81_hard_recovery_mps_dbg = ep_mps;

    abort_status = HAL_PCD_EP_Abort(&hpcd_USB_OTG_HS, ep_addr);
    flush_status = USB_FlushTxFifo(USBx, epnum);
    close_status = HAL_PCD_EP_Close(&hpcd_USB_OTG_HS, ep_addr);

    hpcd_USB_OTG_HS.IN_ep[epnum].is_iso_incomplete = 0U;
    hpcd_USB_OTG_HS.IN_ep[epnum].xfer_count = 0U;
    hpcd_USB_OTG_HS.IN_ep[epnum].xfer_len = 0U;
    USBx_DEVICE->DIEPEMPMSK &= ~(1UL << epnum);
    in_ep_regs->DIEPINT =
        USB_OTG_DIEPINT_XFRC |
        USB_OTG_DIEPINT_EPDISD |
        USB_OTG_DIEPINT_TOC |
        USB_OTG_DIEPINT_ITTXFE |
        USB_OTG_DIEPINT_INEPNM |
        USB_OTG_DIEPINT_INEPNE |
        USB_OTG_DIEPINT_TXFE |
        USB_OTG_DIEPINT_PKTDRPSTS |
        USB_OTG_DIEPINT_NAK;
    USBx->GINTSTS = USB_OTG_GINTSTS_IISOIXFR | USB_OTG_GINTSTS_IEPINT;

    open_status = HAL_PCD_EP_Open(&hpcd_USB_OTG_HS, ep_addr, ep_mps, EP_TYPE_ISOC);

    UX_DISABLE

    ed_status = ed->ux_dcd_stm32_ed_status;
    transfer_request->ux_slave_transfer_request_completion_code = UX_SUCCESS;
    transfer_request->ux_slave_transfer_request_status = UX_TRANSFER_STATUS_COMPLETED;
    transfer_request->ux_slave_transfer_request_actual_length = 0UL;
    ed->ux_dcd_stm32_ed_status &= (UX_DCD_STM32_ED_STATUS_USED |
                                   UX_DCD_STM32_ED_STATUS_STALLED |
                                   UX_DCD_STM32_ED_STATUS_TASK_PENDING);

    UX_RESTORE

    usbx_ep81_hard_recovery_count_dbg++;
    usbx_ep81_hard_recovery_last_tick_dbg = now_tick;
    usbx_ep81_hard_recovery_abort_status_dbg = (ULONG)abort_status;
    usbx_ep81_hard_recovery_flush_status_dbg = (ULONG)flush_status;
    usbx_ep81_hard_recovery_close_status_dbg = (ULONG)close_status;
    usbx_ep81_hard_recovery_open_status_dbg = (ULONG)open_status;
    usbx_ep81_hard_recovery_ed_status_after_dbg = ed->ux_dcd_stm32_ed_status;
    usbx_ep81_hard_recovery_diepctl_after_dbg = in_ep_regs->DIEPCTL;
    usbx_ep81_hard_recovery_dieptsiz_after_dbg = in_ep_regs->DIEPTSIZ;
    usbx_ep81_hard_recovery_diepint_after_dbg = in_ep_regs->DIEPINT;
    usbx_ep81_hard_recovery_dtxfsts_after_dbg = in_ep_regs->DTXFSTS;

    usbx_freeze_frozen_dbg = 0UL;
    usbx_ep81_stream_start_tick_dbg = now_tick;
    usbx_ep81_last_submit_tick_dbg = 0UL;
    usbx_ep81_last_datain_tick_dbg = 0UL;
    usbx_ep81_last_done_tick_dbg = 0UL;
    usbx_ep81_last_isoinc_tick_dbg = 0UL;
    usbx_ep81_last_iiso_tick_dbg = 0UL;
    usbx_ep81_last_irec_tick_dbg = 0UL;
    usbx_ep81_last_payload_done_tick_dbg = 0UL;
    usbx_ep81_idle_elapsed_dbg = 0UL;
    usb_ll_iso_after_recovery_dbg = 1U;

    USBX_TraceLog(USBX_TRACE_EVT_EP81_HREC,
                  ep_addr,
                  (ULONG)abort_status,
                  (ULONG)flush_status,
                  (ULONG)close_status,
                  (ULONG)open_status,
                  (ULONG)ed_status,
                  usbx_ep81_hard_recovery_ed_status_after_dbg,
                  usbx_ep81_hard_recovery_diepctl_before_dbg,
                  usbx_ep81_hard_recovery_dieptsiz_before_dbg,
                  usbx_ep81_hard_recovery_diepint_before_dbg,
                  usbx_ep81_hard_recovery_diepctl_after_dbg,
                  usbx_ep81_hard_recovery_count_dbg);
}

static void USBX_FreezeCapture(ULONG reason, ULONG now_tick)
{
    UX_SLAVE_DCD *dcd = UX_NULL;
    UX_DCD_STM32 *dcd_stm32 = UX_NULL;
    UX_DCD_STM32_ED *ed = UX_NULL;
    UX_SLAVE_ENDPOINT *endpoint = UX_NULL;
    UX_SLAVE_TRANSFER *transfer_request = UX_NULL;
    USB_OTG_GlobalTypeDef *USBx = hpcd_USB_OTG_HS.Instance;
    USB_OTG_INEndpointTypeDef *in_ep_regs = UX_NULL;
    uint32_t USBx_BASE = 0U;
    USBX_FREEZE_SNAPSHOT *snapshot;

    if (usbx_freeze_frozen_dbg != 0UL)
    {
        return;
    }

    usbx_freeze_frozen_dbg = 1UL;
    usbx_freeze_count_dbg++;
    snapshot = (USBX_FREEZE_SNAPSHOT *)&usbx_freeze_snapshot_dbg;
    memset(snapshot, 0, sizeof(*snapshot));

    snapshot->reason = reason;
    snapshot->tick = now_tick;
    snapshot->elapsed = now_tick - usbx_freeze_progress_tick_dbg;
    snapshot->count = usbx_freeze_count_dbg;
    snapshot->trace_seq = usbx_trace_seq_dbg;
    snapshot->trace_wr_idx = usbx_trace_wr_idx_dbg;
    snapshot->trace_last_idx = usbx_trace_last_idx_dbg;
    snapshot->trace_last_event = usbx_trace_last_event_dbg;
    snapshot->progress_tick = usbx_freeze_progress_tick_dbg;
    snapshot->progress_seq = usbx_freeze_progress_seq_dbg;
    snapshot->progress_event = usbx_freeze_progress_event_dbg;

    snapshot->task_calls = usbx_task_calls_dbg;
    snapshot->video_last_alt = usbx_video_last_alt_dbg;
    snapshot->video_start_status = usbx_video_start_status_dbg;
    snapshot->video_stream_task_state = usbx_video_stream_task_state_dbg;
    snapshot->video_stream_task_status = usbx_video_stream_task_status_dbg;
    snapshot->video_payload_done = usbx_video_payload_done_dbg;
    snapshot->video_write_calls = usbx_video_write_calls_dbg;
    snapshot->video_last_state = usbx_video_last_state_dbg;
    snapshot->video_img_count = usbx_video_img_count_dbg;
    snapshot->video_packet_index = usbx_video_packet_index_dbg;
    snapshot->video_last_payload_len = usbx_video_last_payload_len_dbg;
    snapshot->video_last_frame_size = usbx_video_last_frame_size_dbg;
    snapshot->video_pace_hold_count = usbx_video_pace_hold_count_dbg;
    snapshot->video_pace_release_count = usbx_video_pace_release_count_dbg;
    snapshot->video_pace_wait_ms = usbx_video_pace_wait_ms_dbg;
    snapshot->video_pace_now = usbx_video_pace_now_dbg;
    snapshot->video_pace_target = usbx_video_pace_target_dbg;
    snapshot->video_resync_count = usbx_video_resync_count_dbg;
    snapshot->video_iso_recovery_pending = usbx_video_iso_recovery_pending_dbg;
    snapshot->ep81_stream_start_tick = usbx_ep81_stream_start_tick_dbg;
    snapshot->ep81_last_submit_tick = usbx_ep81_last_submit_tick_dbg;
    snapshot->ep81_last_datain_tick = usbx_ep81_last_datain_tick_dbg;
    snapshot->ep81_last_done_tick = usbx_ep81_last_done_tick_dbg;
    snapshot->ep81_last_isoinc_tick = usbx_ep81_last_isoinc_tick_dbg;
    snapshot->ep81_last_iiso_tick = usbx_ep81_last_iiso_tick_dbg;
    snapshot->ep81_last_irec_tick = usbx_ep81_last_irec_tick_dbg;
    snapshot->ep81_last_payload_done_tick = usbx_ep81_last_payload_done_tick_dbg;
    snapshot->ep81_idle_elapsed = usbx_ep81_idle_elapsed_dbg;
    snapshot->ep81_idle_count = usbx_ep81_idle_count_dbg;

    if (_ux_system_slave != UX_NULL)
    {
        snapshot->device_state =
            _ux_system_slave->ux_system_slave_device.ux_slave_device_state;
        snapshot->device_speed = _ux_system_slave->ux_system_slave_speed;

        dcd = &_ux_system_slave->ux_system_slave_dcd;
        snapshot->dcd_status = (ULONG)dcd->ux_slave_dcd_status;
        dcd_stm32 = (UX_DCD_STM32 *)dcd->ux_slave_dcd_controller_hardware;
    }

    if (USBx != UX_NULL)
    {
        USBx_BASE = (uint32_t)USBx;
        in_ep_regs = USBx_INEP(1U);
        snapshot->gintsts = USBx->GINTSTS;
        snapshot->gintmsk = USBx->GINTMSK;
        snapshot->gahbcfg = USBx->GAHBCFG;
        snapshot->gusbcfg = USBx->GUSBCFG;
        snapshot->dsts = USBx_DEVICE->DSTS;
        snapshot->daint = USBx_DEVICE->DAINT;
        snapshot->daintmsk = USBx_DEVICE->DAINTMSK;
        snapshot->diepempmsk = USBx_DEVICE->DIEPEMPMSK;
        snapshot->diepctl = in_ep_regs->DIEPCTL;
        snapshot->dieptsiz = in_ep_regs->DIEPTSIZ;
        snapshot->diepint = in_ep_regs->DIEPINT;
        snapshot->dtxfsts = in_ep_regs->DTXFSTS;
    }

    snapshot->pcd_ep_xfer_buff = (ULONG)hpcd_USB_OTG_HS.IN_ep[1].xfer_buff;
    snapshot->pcd_ep_xfer_len = hpcd_USB_OTG_HS.IN_ep[1].xfer_len;
    snapshot->pcd_ep_xfer_count = hpcd_USB_OTG_HS.IN_ep[1].xfer_count;
    snapshot->pcd_ep_maxpacket = hpcd_USB_OTG_HS.IN_ep[1].maxpacket;
    snapshot->pcd_ep_is_in = hpcd_USB_OTG_HS.IN_ep[1].is_in;
    snapshot->pcd_ep_is_iso_incomplete =
        hpcd_USB_OTG_HS.IN_ep[1].is_iso_incomplete;
    snapshot->pcd_ep_type = hpcd_USB_OTG_HS.IN_ep[1].type;

    if (dcd_stm32 != UX_NULL)
    {
#if defined(UX_DEVICE_BIDIRECTIONAL_ENDPOINT_SUPPORT)
        ed = &dcd_stm32->ux_dcd_stm32_ed_in[1U];
#else
        ed = &dcd_stm32->ux_dcd_stm32_ed[1U];
#endif
    }

    if (ed != UX_NULL)
    {
        snapshot->dcd_ed_status = ed->ux_dcd_stm32_ed_status;
        snapshot->dcd_ed_endpoint = (ULONG)ed->ux_dcd_stm32_ed_endpoint;
        endpoint = ed->ux_dcd_stm32_ed_endpoint;
    }

    if (endpoint != UX_NULL)
    {
        snapshot->endpoint_addr =
            endpoint->ux_slave_endpoint_descriptor.bEndpointAddress;
        snapshot->endpoint_attributes =
            endpoint->ux_slave_endpoint_descriptor.bmAttributes;
        snapshot->endpoint_mps =
            endpoint->ux_slave_endpoint_descriptor.wMaxPacketSize;

        transfer_request = &endpoint->ux_slave_endpoint_transfer_request;
        snapshot->transfer_data =
            (ULONG)transfer_request->ux_slave_transfer_request_data_pointer;
        snapshot->transfer_current_data =
            (ULONG)transfer_request->ux_slave_transfer_request_current_data_pointer;
        snapshot->transfer_requested_len =
            transfer_request->ux_slave_transfer_request_requested_length;
        snapshot->transfer_actual_len =
            transfer_request->ux_slave_transfer_request_actual_length;
        snapshot->transfer_in_len =
            transfer_request->ux_slave_transfer_request_in_transfer_length;
        snapshot->transfer_total_len =
            transfer_request->ux_slave_transfer_request_transfer_length;
        snapshot->transfer_status =
            transfer_request->ux_slave_transfer_request_status;
        snapshot->transfer_completion =
            transfer_request->ux_slave_transfer_request_completion_code;
        snapshot->transfer_phase =
            transfer_request->ux_slave_transfer_request_phase;
#if defined(UX_DEVICE_STANDALONE)
        snapshot->transfer_state =
            transfer_request->ux_slave_transfer_request_state;
#endif
    }

    USBX_TraceLog(USBX_TRACE_EVT_FREEZE,
                  reason,
                  snapshot->elapsed,
                  snapshot->progress_event,
                  snapshot->trace_last_event,
                  snapshot->video_last_alt,
                  snapshot->dcd_ed_status,
                  snapshot->dieptsiz,
                  snapshot->diepint,
                  snapshot->dsts,
                  snapshot->task_calls,
                  snapshot->video_pace_wait_ms,
                  snapshot->video_stream_task_state);
}

static void USBX_FreezePollAt(ULONG now_tick, ULONG reason)
{
    ULONG elapsed;

    USBX_Ep81WatchdogPollAt(now_tick);

    if ((usbx_freeze_enable_dbg == 0UL) ||
        (usbx_freeze_frozen_dbg != 0UL) ||
        (usbx_video_last_alt_dbg == 0UL) ||
        (usbx_freeze_progress_tick_dbg == 0UL))
    {
        return;
    }

    if ((now_tick - usbx_freeze_last_poll_tick_dbg) <
        usbx_freeze_poll_period_ms_dbg)
    {
        return;
    }

    usbx_freeze_last_poll_tick_dbg = now_tick;
    elapsed = now_tick - usbx_freeze_progress_tick_dbg;
    usbx_freeze_last_elapsed_dbg = elapsed;
    if (elapsed >= usbx_freeze_timeout_ms_dbg)
    {
        USBX_FreezeCapture(reason, now_tick);
    }
}

void USBX_FreezeCaptureNow(ULONG reason)
{
    USBX_FreezeCapture((reason == 0UL) ? USBX_FREEZE_REASON_MANUAL : reason,
                       (ULONG)HAL_GetTick());
}

void USBX_FreezePoll(void)
{
    USBX_FreezePollAt((ULONG)HAL_GetTick(), USBX_FREEZE_REASON_NO_PROGRESS);
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

static void USBX_CaptureMaskedIISOIXFR(uint32_t gintsts, uint32_t first)
{
    uint32_t USBx_BASE;
    USB_OTG_GlobalTypeDef *USBx = hpcd_USB_OTG_HS.Instance;
    USB_OTG_INEndpointTypeDef *in_ep_regs;
    static uint32_t last_diepctl;
    static uint32_t last_dieptsiz;
    static uint32_t last_diepint;
    static uint32_t last_dtxfsts;
    uint32_t changed;
    ULONG poll_number;

    if (USBx == UX_NULL)
    {
        return;
    }

    USBx_BASE = (uint32_t)USBx;
    in_ep_regs = USBx_INEP(1U);

    usbx_iisoixfr_poll_last_gintsts_dbg = gintsts;
    usbx_iisoixfr_poll_last_gintmsk_dbg = USBx->GINTMSK;
    usbx_iisoixfr_poll_last_dsts_dbg = USBx_DEVICE->DSTS;
    usbx_iisoixfr_poll_last_diepctl_dbg = in_ep_regs->DIEPCTL;
    usbx_iisoixfr_poll_last_dieptsiz_dbg = in_ep_regs->DIEPTSIZ;
    usbx_iisoixfr_poll_last_diepint_dbg = in_ep_regs->DIEPINT;
    usbx_iisoixfr_poll_last_dtxfsts_dbg = in_ep_regs->DTXFSTS;

    if (first != 0U)
    {
        usbx_iisoixfr_poll_first_gintsts_dbg = usbx_iisoixfr_poll_last_gintsts_dbg;
        usbx_iisoixfr_poll_first_gintmsk_dbg = usbx_iisoixfr_poll_last_gintmsk_dbg;
        usbx_iisoixfr_poll_first_dsts_dbg = usbx_iisoixfr_poll_last_dsts_dbg;
        usbx_iisoixfr_poll_first_diepctl_dbg = usbx_iisoixfr_poll_last_diepctl_dbg;
        usbx_iisoixfr_poll_first_dieptsiz_dbg = usbx_iisoixfr_poll_last_dieptsiz_dbg;
        usbx_iisoixfr_poll_first_diepint_dbg = usbx_iisoixfr_poll_last_diepint_dbg;
        usbx_iisoixfr_poll_first_dtxfsts_dbg = usbx_iisoixfr_poll_last_dtxfsts_dbg;
    }

    changed = (usbx_iisoixfr_poll_last_diepctl_dbg != last_diepctl) ||
              (usbx_iisoixfr_poll_last_dieptsiz_dbg != last_dieptsiz) ||
              (usbx_iisoixfr_poll_last_diepint_dbg != last_diepint) ||
              (usbx_iisoixfr_poll_last_dtxfsts_dbg != last_dtxfsts);
    poll_number = usbx_iisoixfr_poll_count_dbg + 1UL;

    if ((first != 0U) || (changed != 0U) || ((poll_number & 0xFFFUL) == 0UL))
    {
        USBX_TraceLog(USBX_TRACE_EVT_DCD_IISO,
                      poll_number,
                      usbx_iisoixfr_poll_last_gintsts_dbg,
                      usbx_iisoixfr_poll_last_gintmsk_dbg,
                      usbx_iisoixfr_poll_last_dsts_dbg,
                      usbx_iisoixfr_poll_last_diepctl_dbg,
                      usbx_iisoixfr_poll_last_dieptsiz_dbg,
                      usbx_iisoixfr_poll_last_diepint_dbg,
                      usbx_iisoixfr_poll_last_dtxfsts_dbg,
                      usbx_video_payload_done_dbg,
                      usbx_video_write_calls_dbg,
                      usbx_trace_iiso_suppressed_dbg,
                      0UL);

        last_diepctl = usbx_iisoixfr_poll_last_diepctl_dbg;
        last_dieptsiz = usbx_iisoixfr_poll_last_dieptsiz_dbg;
        last_diepint = usbx_iisoixfr_poll_last_diepint_dbg;
        last_dtxfsts = usbx_iisoixfr_poll_last_dtxfsts_dbg;
        usbx_trace_iiso_suppressed_dbg = 0UL;
    }
    else
    {
        usbx_trace_iiso_suppressed_dbg++;
    }
}

static void USBX_RecoverMaskedIISOIXFR(void)
{
    UX_INTERRUPT_SAVE_AREA
    UX_SLAVE_DCD *dcd;
    UX_DCD_STM32 *dcd_stm32;
    UX_DCD_STM32_ED *ed;
    UX_SLAVE_ENDPOINT *endpoint;
    UX_SLAVE_TRANSFER *transfer_request;
    USB_OTG_GlobalTypeDef *USBx = hpcd_USB_OTG_HS.Instance;
    uint32_t USBx_BASE;
    uint32_t ed_status;
    uint32_t should_recover;
    const uint8_t epnum = 1U;
    HAL_StatusTypeDef abort_status = HAL_OK;
    HAL_StatusTypeDef flush_status = HAL_OK;

    if ((usbx_iisoixfr_recovery_enable_dbg == 0UL) ||
        (USBx == UX_NULL) ||
        (_ux_system_slave == UX_NULL))
    {
        usbx_iisoixfr_recovery_skip_dbg++;
        return;
    }

    dcd = &_ux_system_slave->ux_system_slave_dcd;
    dcd_stm32 = (UX_DCD_STM32 *)dcd->ux_slave_dcd_controller_hardware;
    if (dcd_stm32 == UX_NULL)
    {
        usbx_iisoixfr_recovery_skip_dbg++;
        return;
    }

#if defined(UX_DEVICE_BIDIRECTIONAL_ENDPOINT_SUPPORT)
    ed = &dcd_stm32->ux_dcd_stm32_ed_in[epnum];
#else
    ed = &dcd_stm32->ux_dcd_stm32_ed[epnum];
#endif

    if ((ed->ux_dcd_stm32_ed_status & UX_DCD_STM32_ED_STATUS_USED) == 0U)
    {
        usbx_iisoixfr_recovery_skip_dbg++;
        return;
    }

    endpoint = ed->ux_dcd_stm32_ed_endpoint;
    if ((endpoint == UX_NULL) ||
        ((endpoint->ux_slave_endpoint_descriptor.bEndpointAddress & 0x8FU) != 0x81U))
    {
        usbx_iisoixfr_recovery_skip_dbg++;
        return;
    }

    transfer_request = &endpoint->ux_slave_endpoint_transfer_request;
    USBx_BASE = (uint32_t)USBx;

    usbx_iisoixfr_recovery_diepctl_dbg = USBx_INEP(epnum)->DIEPCTL;
    usbx_iisoixfr_recovery_dieptsiz_dbg = USBx_INEP(epnum)->DIEPTSIZ;
    usbx_iisoixfr_recovery_diepint_dbg = USBx_INEP(epnum)->DIEPINT;
    usbx_iisoixfr_recovery_dtxfsts_dbg = USBx_INEP(epnum)->DTXFSTS;
    usbx_iisoixfr_recovery_req_len_dbg =
        transfer_request->ux_slave_transfer_request_requested_length;
    usbx_iisoixfr_recovery_transfer_status_dbg =
        transfer_request->ux_slave_transfer_request_status;

    ed_status = ed->ux_dcd_stm32_ed_status;
    usbx_iisoixfr_recovery_ed_status_dbg = ed_status;
    should_recover =
        (((ed_status & UX_DCD_STM32_ED_STATUS_TRANSFER) != 0U) &&
         ((ed_status & UX_DCD_STM32_ED_STATUS_DONE) == 0U)) ? 1U : 0U;

    if (should_recover == 0U)
    {
        usbx_iisoixfr_recovery_skip_dbg++;
        return;
    }

    /*
     * IISOIXFR can be latched while the just-submitted IN transfer is already
     * accepted by the core. In that state aborting here fabricates a zero-length
     * completion and drops the rest of the video frame.
     */
    if (((usbx_iisoixfr_recovery_dieptsiz_dbg & USB_OTG_DIEPTSIZ_XFRSIZ) == 0U) ||
        ((usbx_iisoixfr_recovery_diepint_dbg & USB_OTG_DIEPINT_XFRC) != 0U))
    {
        usbx_iisoixfr_recovery_skip_dbg++;
        return;
    }

    usbx_iisoixfr_recovery_iso_flag_dbg =
        hpcd_USB_OTG_HS.IN_ep[epnum].is_iso_incomplete;
    abort_status = HAL_PCD_EP_Abort(&hpcd_USB_OTG_HS, 0x81U);
    flush_status = USB_FlushTxFifo(USBx, epnum);
    usbx_iisoixfr_recovery_diepint_after_abort_dbg = USBx_INEP(epnum)->DIEPINT;

    hpcd_USB_OTG_HS.IN_ep[epnum].is_iso_incomplete = 0U;
    USBx_DEVICE->DIEPEMPMSK &= ~(1UL << epnum);
    if (usbx_iisoixfr_recovery_clear_diepint_enable_dbg != 0UL)
    {
        USBx_INEP(epnum)->DIEPINT =
            USB_OTG_DIEPINT_EPDISD |
            USB_OTG_DIEPINT_TOC |
            USB_OTG_DIEPINT_ITTXFE |
            USB_OTG_DIEPINT_INEPNM |
            USB_OTG_DIEPINT_INEPNE |
            USB_OTG_DIEPINT_TXFE |
            USB_OTG_DIEPINT_PKTDRPSTS |
            USB_OTG_DIEPINT_NAK;
    }
    usbx_iisoixfr_recovery_diepint_after_clear_dbg = USBx_INEP(epnum)->DIEPINT;
    usbx_iisoixfr_recovery_retry_count_dbg = 0UL;
    usbx_iisoixfr_recovery_retry_status_dbg = 0UL;
    usbx_iisoixfr_recovery_retry_diepctl_after_dbg =
        USBx_INEP(epnum)->DIEPCTL;
    usbx_iisoixfr_recovery_retry_dieptsiz_after_dbg =
        USBx_INEP(epnum)->DIEPTSIZ;
    usbx_iisoixfr_recovery_retry_diepint_after_dbg =
        USBx_INEP(epnum)->DIEPINT;
    usbx_iisoixfr_recovery_retry_dtxfsts_after_dbg =
        USBx_INEP(epnum)->DTXFSTS;

    UX_DISABLE

    transfer_request->ux_slave_transfer_request_completion_code = UX_SUCCESS;
    transfer_request->ux_slave_transfer_request_status = UX_TRANSFER_STATUS_COMPLETED;
    transfer_request->ux_slave_transfer_request_actual_length = 0UL;
    ed->ux_dcd_stm32_ed_status |= UX_DCD_STM32_ED_STATUS_DONE;

    UX_RESTORE

    usbx_iisoixfr_recovery_calls_dbg++;
    usbx_video_iso_recovery_pending_dbg++;
    usb_ll_iso_after_recovery_dbg = 1U;
    usbx_iisoixfr_recovery_abort_status_dbg = (ULONG)abort_status;
    usbx_iisoixfr_recovery_flush_status_dbg = (ULONG)flush_status;

    USBX_TraceLog(USBX_TRACE_EVT_DCD_IREC,
                  0x81UL,
                  usbx_iisoixfr_recovery_req_len_dbg,
                  (ULONG)abort_status,
                  (ULONG)flush_status,
                  ed_status,
                  usbx_iisoixfr_recovery_transfer_status_dbg,
                  usbx_iisoixfr_recovery_diepctl_dbg,
                  usbx_iisoixfr_recovery_dieptsiz_dbg,
                  usbx_iisoixfr_recovery_diepint_dbg,
                  usbx_iisoixfr_recovery_dtxfsts_dbg,
                  usbx_iisoixfr_recovery_calls_dbg,
                  usbx_iisoixfr_poll_count_dbg);
}

static void USBX_PollMaskedIISOIXFR(void)
{
    USB_OTG_GlobalTypeDef *USBx = hpcd_USB_OTG_HS.Instance;
    uint32_t gintsts;

    if ((USBx == UX_NULL) || (usb_mask_iisoixfr_enable == 0U))
    {
        return;
    }

    gintsts = USBx->GINTSTS;
    if ((gintsts & USB_OTG_GINTSTS_IISOIXFR) == 0U)
    {
        return;
    }

    USBX_CaptureMaskedIISOIXFR(gintsts, (usbx_iisoixfr_poll_count_dbg == 0UL) ? 1U : 0U);
    USBX_RecoverMaskedIISOIXFR();
    usbx_iisoixfr_poll_count_dbg++;

    USBx->GINTSTS = USB_OTG_GINTSTS_IISOIXFR;
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

    USBX_TraceReset();
    usbx_iisoixfr_recovery_retry_calls_dbg += 0UL;
    usbx_iisoixfr_recovery_retry_giveup_dbg += 0UL;

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

    USBX_PollMaskedIISOIXFR();

    usbx_task_calls_dbg++;
    (void)_ux_system_tasks_run();

    USBX_PollMaskedIISOIXFR();
    USBX_Ep81HardRecoveryPoll();

    usbx_device_state_dbg = _ux_system_slave->ux_system_slave_device.ux_slave_device_state;
    usbx_device_speed_dbg = _ux_system_slave->ux_system_slave_speed;
    USBX_FreezePoll();
}
