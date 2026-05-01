#include "camera_pipeline.h"

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"
#include "jpeg_live.h"
#include "test_jpeg.h"
#include "usbd_uvc.h"
#include "video_source.h"

extern JPEG_HandleTypeDef hjpeg;

/* ===== Размеры LTDC framebuffer ===== */
#define LCD_FB_W      320U
#define LCD_FB_H      480U

/* ===== Размер кадра для UVC/JPEG ===== */
#define UVC_W         UVC_FRAME_WIDTH
#define UVC_H         UVC_FRAME_HEIGHT

#define JPEG_BUF_SIZE UVC_MAX_FRAME_SIZE

/*
 * ВАЖНО:
 * Здесь должен быть именно тот framebuffer, который реально показывает LTDC.
 * Если у тебя надёжнее работать по фиксированному адресу SDRAM, замени extern
 * на define с адресом.
 */
extern uint16_t fb[LCD_FB_W * LCD_FB_H];

/* ===== Буферы ===== */

/* downsampled RGB565 frame for JPEG encoder */
static uint16_t uvc_rgb565[UVC_W * UVC_H]
    __attribute__((section(".xsdram"), aligned(32)));

/* JPEG pool: current, pending and encoder fill must not alias. */
#define JPEG_POOL_COUNT 3U
static uint8_t uvc_jpeg_pool[JPEG_POOL_COUNT][JPEG_BUF_SIZE]
    __attribute__((section(".xsdram"), aligned(32)));
static uint8_t jpeg_fill_index = 0U;

/* ===== Runtime state ===== */

volatile uint32_t camera_pipeline_updates = 0;
volatile uint32_t camera_pipeline_before_encode = 0;
volatile uint32_t camera_pipeline_encode_ok = 0;
volatile uint32_t camera_pipeline_encode_fail = 0;
volatile uint32_t camera_pipeline_last_jpeg_size = 0;

volatile uint32_t camera_pipeline_submit_ok = 0;
volatile uint32_t camera_pipeline_skip_jpeg_busy = 0;
volatile uint32_t camera_pipeline_skip_pending = 0;
volatile uint32_t camera_pipeline_skip_no_free_jpeg_buf = 0;
volatile uint32_t camera_pipeline_skip_oversize_jpeg = 0;
volatile uint32_t camera_pipeline_max_submit_jpeg_size = UVC_MAX_FRAME_SIZE;
volatile uint32_t camera_pipeline_force_test_jpeg = 0;
volatile uint32_t camera_pipeline_test_submit_ok = 0;
volatile uint32_t camera_pipeline_test_frame_mode = 2U;
volatile uint32_t camera_pipeline_test_next_frame = 0U;
volatile uint32_t camera_pipeline_test_last_frame = 0U;
volatile uint32_t camera_pipeline_period_ms = UVC_PRODUCER_INTERVAL_MS;
volatile uint32_t camera_pipeline_last_update_tick = 0;
volatile uint32_t camera_pipeline_last_encode_start_tick = 0;
volatile uint32_t camera_pipeline_last_encode_ms = 0;
volatile uint32_t camera_pipeline_max_encode_ms = 0;
volatile uint32_t camera_pipeline_last_encode_ok_tick = 0;
volatile uint32_t camera_pipeline_last_encode_fail_tick = 0;
volatile uint32_t camera_pipeline_last_submit_ok_tick = 0;
volatile uint32_t camera_pipeline_last_submit_fail_tick = 0;
volatile uint32_t camera_pipeline_last_oversize_jpeg_size = 0;
volatile uint32_t camera_pipeline_last_oversize_tick = 0;

/* Frame sequence for fallback/test mode. */
volatile uint32_t camera_pipeline_frame_id = 0;

/* ===== Внутренние функции ===== */

static void uvc_frame_prepare_from_ltdc(void)
{
    /*
     * Downsample LCD framebuffer to the UVC/JPEG frame size.
     */
    for (uint32_t y = 0; y < UVC_H; y++)
    {
        uint32_t src_y = (y * LCD_FB_H) / UVC_H;
        uint32_t src_row = src_y * LCD_FB_W;
        uint32_t dst_row = y * UVC_W;

        for (uint32_t x = 0; x < UVC_W; x++)
        {
            uint32_t src_x = (x * LCD_FB_W) / UVC_W;
            uint16_t px = fb[src_row + src_x];

            uvc_rgb565[dst_row + x] = px;
        }
    }
}

void camera_pipeline_init(void)
{
    video_source_init();
    (void)jpeg_live_init();
}

static uint8_t *camera_pipeline_get_free_jpeg_buf(void)
{
    for (uint32_t n = 0U; n < JPEG_POOL_COUNT; n++)
    {
        uint32_t idx = (uint32_t)jpeg_fill_index + n;
        if (idx >= JPEG_POOL_COUNT)
            idx -= JPEG_POOL_COUNT;

        if (!video_source_buffer_in_use(uvc_jpeg_pool[idx]))
        {
            jpeg_fill_index = (uint8_t)idx;
            return uvc_jpeg_pool[idx];
        }
    }

    camera_pipeline_skip_no_free_jpeg_buf++;
    return 0;
}

void camera_pipeline_update(void)
{
    static uint32_t last_tick = 0xFFFFFFFFU;
    uint32_t now = HAL_GetTick();

    uint8_t *dst_buf;
    uint32_t jpeg_size = 0U;
    bool ok;

    camera_pipeline_updates++;
    camera_pipeline_last_update_tick = now;

    /*
     * Не генерируем новый кадр, пока предыдущий pending ещё не был
     * подхвачен UVC на EOF.
     */
    if (!video_source_can_accept_frame())
    {
        camera_pipeline_skip_pending++;
        return;
    }

    /*
     * Ограничиваем частоту подготовки JPEG.
     * 33 мс = примерно 30 fps max.
     */
    if (last_tick != 0xFFFFFFFFU && (now - last_tick) < camera_pipeline_period_ms)
        return;

    /*
     * Runtime isolation switch: set camera_pipeline_force_test_jpeg = 1
     * in the debugger to stream a known-good bundled JPEG instead of live HW JPEG.
     */
    if (camera_pipeline_force_test_jpeg != 0U)
    {
        const uint8_t *test_data = jpeg_frame_0;
        uint32_t test_size = jpeg_frame_0_size;
        uint32_t test_frame = 0U;

        if (camera_pipeline_test_frame_mode == 1U)
        {
            test_data = jpeg_frame_1;
            test_size = jpeg_frame_1_size;
            test_frame = 1U;
        }
        else if (camera_pipeline_test_frame_mode >= 2U)
        {
            test_frame = camera_pipeline_test_next_frame & 1U;
            if (test_frame != 0U)
            {
                test_data = jpeg_frame_1;
                test_size = jpeg_frame_1_size;
            }
        }

        if (video_source_submit_frame(test_data, test_size))
        {
            camera_pipeline_submit_ok++;
            camera_pipeline_test_submit_ok++;
            camera_pipeline_test_last_frame = test_frame;
            camera_pipeline_last_jpeg_size = test_size;
            if (camera_pipeline_test_frame_mode >= 2U)
            {
                camera_pipeline_test_next_frame ^= 1U;
            }
            last_tick = now;
        }
        else
        {
            camera_pipeline_last_submit_fail_tick = HAL_GetTick();
        }

        return;
    }

    /*
     * JPEG engine должен быть ready.
     * Это защитит от повторного запуска encode слишком рано.
     */
    if (HAL_JPEG_GetState(&hjpeg) != HAL_JPEG_STATE_READY)
    {
        camera_pipeline_skip_jpeg_busy++;
        return;
    }

    camera_pipeline_before_encode++;
    camera_pipeline_frame_id++;

    uvc_frame_prepare_from_ltdc();

    dst_buf = camera_pipeline_get_free_jpeg_buf();
    if (dst_buf == 0)
        return;

    camera_pipeline_last_encode_start_tick = HAL_GetTick();
    ok = jpeg_live_encode_rgb565_160x120(
        uvc_rgb565,
        dst_buf,
        JPEG_BUF_SIZE,
        &jpeg_size);

    camera_pipeline_last_encode_ms = HAL_GetTick() - camera_pipeline_last_encode_start_tick;
    if (camera_pipeline_last_encode_ms > camera_pipeline_max_encode_ms)
    {
        camera_pipeline_max_encode_ms = camera_pipeline_last_encode_ms;
    }

    if (!ok)
    {
        camera_pipeline_encode_fail++;
        camera_pipeline_last_encode_fail_tick = HAL_GetTick();
        return;
    }

    camera_pipeline_encode_ok++;
    camera_pipeline_last_encode_ok_tick = HAL_GetTick();
    camera_pipeline_last_jpeg_size = jpeg_size;

    if (camera_pipeline_max_submit_jpeg_size != 0U &&
        jpeg_size > camera_pipeline_max_submit_jpeg_size)
    {
        camera_pipeline_skip_oversize_jpeg++;
        camera_pipeline_last_oversize_jpeg_size = jpeg_size;
        camera_pipeline_last_oversize_tick = HAL_GetTick();
        last_tick = now;
        return;
    }

    if (video_source_submit_frame(dst_buf, jpeg_size))
    {
        camera_pipeline_submit_ok++;
        camera_pipeline_last_submit_ok_tick = HAL_GetTick();

        jpeg_fill_index++;
        if (jpeg_fill_index >= JPEG_POOL_COUNT)
            jpeg_fill_index = 0U;
        last_tick = now;
    }
    else
    {
        camera_pipeline_last_submit_fail_tick = HAL_GetTick();
    }
}
