#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    const uint8_t *data;
    uint32_t size;
} video_frame_t;

void video_source_init(void);
const video_frame_t *video_source_get_current_frame(void);
uint32_t video_source_get_max_frame_size(void);

bool video_source_submit_frame(const uint8_t *data, uint32_t size);
bool video_source_commit_pending_if_any(void);
bool video_source_prepare_next_frame(bool *repeated);
bool video_source_can_accept_frame(void);
bool video_source_has_current_frame(void);
bool video_source_buffer_in_use(const uint8_t *data);

/* debug */
extern volatile const uint8_t *dbg_current_frame_ptr;
extern volatile const uint8_t *dbg_pending_frame_ptr;
extern volatile uint32_t dbg_current_frame_size;
extern volatile uint32_t dbg_pending_frame_size;
extern volatile uint32_t dbg_commit_calls;
extern volatile uint32_t dbg_pending_valid_before;
extern volatile uint32_t dbg_pending_valid_after;
extern volatile uint32_t dbg_prepare_next_calls;
extern volatile uint32_t dbg_repeat_current_calls;
extern volatile uint32_t dbg_submit_reject_bad_jpeg;
extern volatile uint32_t dbg_submit_last_size;
extern volatile uint32_t dbg_submit_head;
extern volatile uint32_t dbg_submit_tail;
