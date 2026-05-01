#pragma once

#include <stdint.h>

void camera_pipeline_init(void);
void camera_pipeline_update(void);

extern volatile uint32_t camera_pipeline_force_test_jpeg;
extern volatile uint32_t camera_pipeline_use_tvp_render_rect_dbg;
extern volatile uint32_t camera_pipeline_src_x_dbg;
extern volatile uint32_t camera_pipeline_src_y_dbg;
extern volatile uint32_t camera_pipeline_src_w_dbg;
extern volatile uint32_t camera_pipeline_src_h_dbg;
