#ifndef APP_USBX_DEVICE_H
#define APP_USBX_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ux_api.h"

UINT MX_USBX_Device_Init(void);
void MX_USBX_Device_Process(void);

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
extern volatile ULONG usbx_video_last_alt_dbg;
extern volatile ULONG usbx_video_start_status_dbg;
extern volatile ULONG usbx_video_write_calls_dbg;
extern volatile ULONG usbx_video_get_status_dbg;
extern volatile ULONG usbx_video_commit_status_dbg;
extern volatile ULONG usbx_video_last_done_len_dbg;
extern volatile ULONG usbx_video_last_payload_len_dbg;
extern volatile ULONG usbx_video_last_buffer_len_dbg;
extern volatile ULONG usbx_video_last_state_dbg;

#ifdef __cplusplus
}
#endif

#endif /* APP_USBX_DEVICE_H */
