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
