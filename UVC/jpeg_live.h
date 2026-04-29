#pragma once
#include <stdint.h>
#include <stdbool.h>

bool jpeg_live_init(void);

/* RGB565 framebuffer snapshot -> JPEG bitstream in RAM */
bool jpeg_live_encode_rgb565_160x120(
    const uint16_t *src_rgb565,
    uint8_t *dst_jpeg,
    uint32_t dst_capacity,
    uint32_t *dst_size);

extern volatile uint32_t jpeg_live_quality;
extern volatile uint32_t jpeg_live_quality_applied;
