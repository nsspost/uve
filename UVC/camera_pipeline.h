#pragma once

#include <stdint.h>

void camera_pipeline_init(void);
void camera_pipeline_update(void);

extern volatile uint32_t camera_pipeline_force_test_jpeg;
