#include "tvp5150_capture.h"

#include "dcmi.h"
#include "dma2d.h"
#include "tvp5150.h"

#define TVP_CAPTURE_SKIP_NONE      0x00000000U
#define TVP_CAPTURE_SKIP_DISABLED  0x4F464620U
#define TVP_CAPTURE_SKIP_NO_TVP    0x50524F42U
#define TVP_CAPTURE_SKIP_I2C_BUS   0x4932433FU
#define TVP_CAPTURE_TEST_WORDS     4096U
#define TVP_CAPTURE_TEST_XSIZE     127U
#define TVP_CAPTURE_TEST_YSIZE     127U
#define TVP_CAPTURE_FIELD_HEIGHT_MIN     120U
#define TVP_CAPTURE_FIELD_HEIGHT_MAX     288U
#define TVP_CAPTURE_FIELD_HEIGHT_DEFAULT 288U
#define TVP_CAPTURE_RAW_WIDTH_DEFAULT    864U
#define TVP_CAPTURE_RAW_WIDTH_MAX        864U
#define TVP_CAPTURE_LINE_SEGMENT_LINES_DEFAULT 4U
#define TVP_CAPTURE_LINE_SEGMENT_LINES_MAX     8U
#define TVP_CAPTURE_RENDER_SRC_X_DEFAULT 72U
#define TVP_CAPTURE_HW_CROP_SRC_X_DEFAULT 68U
#define TVP_CAPTURE_HW_CROP_SRC_Y_DEFAULT 18U
#define TVP_CAPTURE_HW_CROP_HEIGHT_DEFAULT 284U
#define TVP_CAPTURE_RENDER_FAST_W_MAX 320U
#define TVP_CAPTURE_RENDER_FAST_H_MAX 240U
#define TVP_CAPTURE_DMA2D_MCU422_W 16U
#define TVP_CAPTURE_DMA2D_MCU422_H 8U
#define TVP_CAPTURE_DMA2D_MCU422_BYTES 256U
#define TVP_CAPTURE_RAW_LINE_BYTES_MAX   (TVP_CAPTURE_RAW_WIDTH_MAX * TVP5150_CAPTURE_BPP)
#define TVP_CAPTURE_RAW_LINE_WORDS_MAX   (TVP_CAPTURE_RAW_LINE_BYTES_MAX / 4U)
#define TVP_CAPTURE_LINE_SEGMENT_WORDS_MAX \
    (TVP_CAPTURE_RAW_LINE_WORDS_MAX * TVP_CAPTURE_LINE_SEGMENT_LINES_MAX)

__attribute__((section(".xsdram"), aligned(32)))
uint32_t tvp5150_ycbcr422_buf0[TVP5150_CAPTURE_FRAME_WORDS];

__attribute__((section(".xsdram"), aligned(32)))
uint32_t tvp5150_ycbcr422_buf1[TVP5150_CAPTURE_FRAME_WORDS];

__attribute__((aligned(32)))
static uint32_t tvp5150_ycbcr422_test_buf[TVP_CAPTURE_TEST_WORDS];

__attribute__((aligned(32)))
static uint32_t tvp5150_line_buf0[TVP_CAPTURE_LINE_SEGMENT_WORDS_MAX];

__attribute__((aligned(32)))
static uint32_t tvp5150_line_buf1[TVP_CAPTURE_LINE_SEGMENT_WORDS_MAX];

__attribute__((section(".xsdram"), aligned(32)))
static uint8_t tvp5150_render_ycbcr422_buf[
    TVP_CAPTURE_RENDER_FAST_W_MAX * TVP_CAPTURE_RENDER_FAST_H_MAX * TVP5150_CAPTURE_BPP];

MDMA_HandleTypeDef hmdma_tvp_capture;

volatile uint32_t tvp_capture_enable_dbg = 1;
volatile uint32_t tvp_capture_line_mdma_enable_dbg = 1;
volatile uint32_t tvp_capture_crop_test_enable_dbg = 0;
volatile uint32_t tvp_capture_continuous_test_enable_dbg = 0;
volatile uint32_t tvp_capture_allow_i2c1_conflict_dbg = 0;
volatile uint32_t tvp_capture_init_done_dbg = 0;
volatile uint32_t tvp_capture_start_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_skip_reason_dbg = TVP_CAPTURE_SKIP_NONE;
volatile uint32_t tvp_capture_frame_count_dbg = 0;
volatile uint32_t tvp_capture_restart_count_dbg = 0;
volatile uint32_t tvp_capture_error_count_dbg = 0;
volatile uint32_t tvp_capture_line_count_dbg = 0;
volatile uint32_t tvp_capture_vsync_count_dbg = 0;
volatile uint32_t tvp_capture_active_buf_dbg = 0;
volatile uint32_t tvp_capture_ready_buf_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_frame_pending_dbg = 0;
volatile uint32_t tvp_capture_first_word0_dbg = 0;
volatile uint32_t tvp_capture_first_word1_dbg = 0;
volatile uint32_t tvp_capture_first_word2_dbg = 0;
volatile uint32_t tvp_capture_first_word3_dbg = 0;
volatile uint32_t tvp_capture_head_checksum_dbg = 0;
volatile uint32_t tvp_capture_dma_target_dbg = 0;
volatile uint32_t tvp_capture_dma_length_dbg = 0;
volatile uint32_t tvp_capture_dcmi_mode_dbg = 0;
volatile uint32_t tvp_capture_dma_segment_length_dbg = 0;
volatile uint32_t tvp_capture_dma_words_done_dbg = 0;
volatile uint32_t tvp_capture_live_sample_count_dbg = 0;
volatile uint32_t tvp_capture_crop_config_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_crop_enable_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_dcmi_cwstrtr_dbg = 0;
volatile uint32_t tvp_capture_dcmi_cwsizer_dbg = 0;
volatile uint32_t tvp_capture_dcmi_cr_dbg = 0;
volatile uint32_t tvp_capture_dcmi_sr_dbg = 0;
volatile uint32_t tvp_capture_dcmi_risr_dbg = 0;
volatile uint32_t tvp_capture_dcmi_misr_dbg = 0;
volatile uint32_t tvp_capture_dma_cr_dbg = 0;
volatile uint32_t tvp_capture_dma_fcr_dbg = 0;
volatile uint32_t tvp_capture_dma_ndtr_dbg = 0;
volatile uint32_t tvp_capture_dma_par_dbg = 0;
volatile uint32_t tvp_capture_dma_m0ar_dbg = 0;
volatile uint32_t tvp_capture_dma_m1ar_dbg = 0;
volatile uint32_t tvp_capture_dma_error_dbg = 0;
volatile uint32_t tvp_capture_dcmi_error_dbg = 0;
volatile uint32_t tvp_capture_xfer_size_dbg = 0;
volatile uint32_t tvp_capture_xfer_count_dbg = 0;
volatile uint32_t tvp_capture_xfer_transfer_number_dbg = 0;
volatile uint32_t tvp_capture_pbuff_ptr_dbg = 0;
volatile uint32_t tvp_capture_pin_sample_count_dbg = 0;
volatile uint32_t tvp_capture_pin_sample_tick_dbg = 0;
volatile uint32_t tvp_capture_pin_levels_dbg = 0;
volatile uint32_t tvp_capture_pclk_edges_dbg = 0;
volatile uint32_t tvp_capture_hsync_edges_dbg = 0;
volatile uint32_t tvp_capture_vsync_edges_dbg = 0;
volatile uint32_t tvp_capture_data_or_dbg = 0;
volatile uint32_t tvp_capture_data_and_dbg = 0;
volatile uint32_t tvp_capture_data_change_mask_dbg = 0;
volatile uint32_t tvp_capture_raw_width_dbg = TVP_CAPTURE_RAW_WIDTH_DEFAULT;
volatile uint32_t tvp_capture_reinit_request_dbg = 0;
volatile uint32_t tvp_capture_hw_crop_enable_dbg = 1;
volatile uint32_t tvp_capture_hw_crop_src_x_dbg = TVP_CAPTURE_HW_CROP_SRC_X_DEFAULT;
volatile uint32_t tvp_capture_hw_crop_src_y_dbg = TVP_CAPTURE_HW_CROP_SRC_Y_DEFAULT;
volatile uint32_t tvp_capture_hw_crop_width_dbg = TVP5150_CAPTURE_WIDTH;
volatile uint32_t tvp_capture_hw_crop_height_dbg = TVP_CAPTURE_HW_CROP_HEIGHT_DEFAULT;
volatile uint32_t tvp_capture_dma_segment_lines_dbg = TVP_CAPTURE_LINE_SEGMENT_LINES_DEFAULT;
volatile uint32_t tvp_capture_dma_segment_bytes_dbg = 0;
volatile uint32_t tvp_capture_line_bytes_dbg = 0;
volatile uint32_t tvp_capture_line_words_dbg = 0;
volatile uint32_t tvp_capture_line_write_index_dbg = 0;
volatile uint32_t tvp_capture_dma_line0_count_dbg = 0;
volatile uint32_t tvp_capture_dma_line1_count_dbg = 0;
volatile uint32_t tvp_capture_last_frame_line_count_dbg = 0;
volatile uint32_t tvp_capture_min_frame_line_count_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_max_frame_line_count_dbg = 0;
volatile uint32_t tvp_capture_short_frame_count_dbg = 0;
volatile uint32_t tvp_capture_short_frame_drop_enable_dbg = 1;
volatile uint32_t tvp_capture_short_frame_drop_count_dbg = 0;
volatile uint32_t tvp_capture_short_frame_last_lines_dbg = 0;
volatile uint32_t tvp_capture_mdma_init_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_mdma_start_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_mdma_complete_count_dbg = 0;
volatile uint32_t tvp_capture_mdma_busy_count_dbg = 0;
volatile uint32_t tvp_capture_mdma_error_count_dbg = 0;
volatile uint32_t tvp_capture_mdma_error_dbg = 0;
volatile uint32_t tvp_capture_mdma_cisr_dbg = 0;
volatile uint32_t tvp_capture_mdma_cesr_dbg = 0;
volatile uint32_t tvp_capture_mdma_ccr_dbg = 0;
volatile uint32_t tvp_capture_mdma_ctcr_dbg = 0;
volatile uint32_t tvp_capture_mdma_cbndtr_dbg = 0;
volatile uint32_t tvp_capture_mdma_csar_dbg = 0;
volatile uint32_t tvp_capture_mdma_cdar_dbg = 0;
volatile uint32_t tvp_capture_line_buf0_addr_dbg = 0;
volatile uint32_t tvp_capture_line_buf1_addr_dbg = 0;
volatile uint32_t tvp_capture_field_height_dbg = TVP_CAPTURE_HW_CROP_HEIGHT_DEFAULT;
volatile uint32_t tvp_capture_vsync_resync_enable_dbg = 1;
volatile uint32_t tvp_capture_vsync_line_count_dbg = 0;
volatile uint32_t tvp_capture_vsync_resync_count_dbg = 0;
volatile uint32_t tvp_capture_vsync_drop_count_dbg = 0;
volatile uint32_t tvp_capture_line_overrun_count_dbg = 0;
volatile uint32_t tvp_capture_synthetic_frame_count_dbg = 0;
volatile uint32_t tvp_capture_field_sequence_dbg = 0;
volatile uint32_t tvp_capture_publish_field_div_dbg = 2;
volatile uint32_t tvp_capture_publish_field_phase_dbg = 0;
volatile uint32_t tvp_capture_field_publish_count_dbg = 0;
volatile uint32_t tvp_capture_field_phase_drop_count_dbg = 0;
volatile uint32_t tvp_capture_render_enable_dbg = 1;
volatile uint32_t tvp_capture_render_period_ms_dbg = 40;
volatile uint32_t tvp_capture_render_x_dbg = 0;
volatile uint32_t tvp_capture_render_y_dbg = 120;
volatile uint32_t tvp_capture_render_w_dbg = 320;
volatile uint32_t tvp_capture_render_h_dbg = 240;
volatile uint32_t tvp_capture_render_byte_order_dbg = 0;
volatile uint32_t tvp_capture_render_method_dbg = 2;
volatile uint32_t tvp_capture_render_src_x_dbg = TVP_CAPTURE_RENDER_SRC_X_DEFAULT;
volatile uint32_t tvp_capture_render_src_y_dbg = 0;
volatile uint32_t tvp_capture_render_dma2d_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_render_dma2d_error_dbg = 0;
volatile uint32_t tvp_capture_render_dma2d_count_dbg = 0;
volatile uint32_t tvp_capture_render_linear_dma2d_enable_dbg = 0;
volatile uint32_t tvp_capture_render_fast_count_dbg = 0;
volatile uint32_t tvp_capture_render_fast_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_render_fast_build_ms_dbg = 0;
volatile uint32_t tvp_capture_render_cpu_count_dbg = 0;
volatile uint32_t tvp_capture_render_count_dbg = 0;
volatile uint32_t tvp_capture_render_skip_period_dbg = 0;
volatile uint32_t tvp_capture_render_skip_no_target_dbg = 0;
volatile uint32_t tvp_capture_render_source_buf_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_render_last_tick_dbg = 0;
volatile uint32_t tvp_capture_render_last_ms_dbg = 0;
volatile uint32_t tvp_capture_render_max_ms_dbg = 0;
volatile uint32_t tvp_capture_render_checksum_dbg = 0;
volatile uint32_t tvp_capture_render_locked_buf_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp_capture_render_lock_drop_count_dbg = 0;
volatile uint32_t tvp_capture_ready_overwrite_count_dbg = 0;

static uint32_t tvp_capture_last_pin_sample_tick;
static volatile uint32_t tvp_capture_line_write_index;
static volatile uint32_t tvp_capture_mdma_line_pending;
static uint32_t tvp_capture_dma_segment_lines_active = TVP_CAPTURE_LINE_SEGMENT_LINES_DEFAULT;
static uint32_t tvp_capture_i2c_bus_active;
static uint16_t *tvp_capture_rgb565_target;
static uint32_t tvp_capture_rgb565_target_w;
static uint32_t tvp_capture_rgb565_target_h;

static void tvp_capture_dma_line0_cplt(DMA_HandleTypeDef *hdma);
static void tvp_capture_dma_line1_cplt(DMA_HandleTypeDef *hdma);
static void tvp_capture_dma_error(DMA_HandleTypeDef *hdma);
static void tvp_capture_mdma_cplt(MDMA_HandleTypeDef *hmdma);
static void tvp_capture_mdma_error(MDMA_HandleTypeDef *hmdma);
static uint32_t *tvp_capture_buffer(uint32_t index);

static uint32_t tvp_capture_active_field_height(void)
{
    uint32_t height = tvp_capture_field_height_dbg;

    if (height < TVP_CAPTURE_FIELD_HEIGHT_MIN)
    {
        height = TVP_CAPTURE_FIELD_HEIGHT_DEFAULT;
    }
    if (height > TVP_CAPTURE_FIELD_HEIGHT_MAX)
    {
        height = TVP_CAPTURE_FIELD_HEIGHT_MAX;
    }

    return height;
}

static uint32_t tvp_capture_active_raw_width(void)
{
    uint32_t width = tvp_capture_raw_width_dbg;

    if ((tvp_capture_hw_crop_enable_dbg != 0U) &&
        (tvp_capture_crop_test_enable_dbg == 0U))
    {
        width = tvp_capture_hw_crop_width_dbg;
    }

    if (width < TVP5150_CAPTURE_WIDTH)
    {
        width = TVP5150_CAPTURE_WIDTH;
    }
    if (width > TVP_CAPTURE_RAW_WIDTH_MAX)
    {
        width = TVP_CAPTURE_RAW_WIDTH_MAX;
    }

    width = (width + 7U) & ~7U;
    if (width > TVP_CAPTURE_RAW_WIDTH_MAX)
    {
        width = TVP_CAPTURE_RAW_WIDTH_MAX;
    }
    tvp_capture_raw_width_dbg = width;
    return width;
}

static uint32_t tvp_capture_raw_line_bytes(void)
{
    return tvp_capture_active_raw_width() * TVP5150_CAPTURE_BPP;
}

static uint32_t tvp_capture_active_hw_crop_x(void)
{
    uint32_t x = tvp_capture_hw_crop_src_x_dbg;

    if (x > (TVP_CAPTURE_RAW_WIDTH_MAX - TVP5150_CAPTURE_WIDTH))
    {
        x = 0U;
    }

    return x & ~1U;
}

static uint32_t tvp_capture_active_hw_crop_y(void)
{
    uint32_t y = tvp_capture_hw_crop_src_y_dbg;

    if (y >= TVP_CAPTURE_FIELD_HEIGHT_MAX)
    {
        y = 0U;
    }

    return y;
}

static uint32_t tvp_capture_active_hw_crop_width(void)
{
    uint32_t width = tvp_capture_hw_crop_width_dbg;

    if (width < 2U)
    {
        width = TVP5150_CAPTURE_WIDTH;
    }
    if (width > TVP5150_CAPTURE_WIDTH)
    {
        width = TVP5150_CAPTURE_WIDTH;
    }

    return width & ~1U;
}

static uint32_t tvp_capture_active_hw_crop_height(void)
{
    uint32_t height = tvp_capture_hw_crop_height_dbg;

    if (height < TVP_CAPTURE_FIELD_HEIGHT_MIN)
    {
        height = tvp_capture_active_field_height();
    }
    if (height > TVP_CAPTURE_FIELD_HEIGHT_MAX)
    {
        height = TVP_CAPTURE_FIELD_HEIGHT_MAX;
    }

    return height;
}

static uint32_t tvp_capture_active_segment_lines(void)
{
    uint32_t lines = tvp_capture_dma_segment_lines_dbg;

    if (lines == 0U)
    {
        lines = TVP_CAPTURE_LINE_SEGMENT_LINES_DEFAULT;
    }
    if (lines > TVP_CAPTURE_LINE_SEGMENT_LINES_MAX)
    {
        lines = TVP_CAPTURE_LINE_SEGMENT_LINES_MAX;
    }

    return lines;
}

static uint32_t tvp_capture_current_line_bytes(void)
{
    uint32_t bytes = tvp_capture_line_bytes_dbg;

    if ((bytes < (TVP5150_CAPTURE_WIDTH * TVP5150_CAPTURE_BPP)) ||
        (bytes > TVP_CAPTURE_RAW_LINE_BYTES_MAX) ||
        ((bytes & 3U) != 0U))
    {
        bytes = tvp_capture_raw_line_bytes();
    }

    return bytes;
}

static void tvp_capture_finish_line_frame(uint32_t frame_lines)
{
    uint32_t completed = tvp_capture_active_buf_dbg;
    uint32_t next = completed ^ 1U;
    uint32_t locked = tvp_capture_render_locked_buf_dbg;
    uint32_t field_height = tvp_capture_active_field_height();
    uint32_t field_seq = tvp_capture_field_sequence_dbg + 1U;
    uint32_t publish = 1U;
    uint32_t publish_div = tvp_capture_publish_field_div_dbg;
    uint32_t min_lines = (field_height * 3U) / 4U;

    tvp_capture_field_sequence_dbg = field_seq;
    if (publish_div > 1U)
    {
        if ((field_seq % publish_div) != (tvp_capture_publish_field_phase_dbg % publish_div))
        {
            publish = 0U;
        }
    }

    tvp_capture_frame_count_dbg++;
    tvp_capture_last_frame_line_count_dbg = frame_lines;

    if (frame_lines < tvp_capture_min_frame_line_count_dbg)
    {
        tvp_capture_min_frame_line_count_dbg = frame_lines;
    }
    if (frame_lines > tvp_capture_max_frame_line_count_dbg)
    {
        tvp_capture_max_frame_line_count_dbg = frame_lines;
    }
    if (frame_lines < min_lines)
    {
        tvp_capture_short_frame_count_dbg++;
        tvp_capture_short_frame_last_lines_dbg = frame_lines;
        if (tvp_capture_short_frame_drop_enable_dbg != 0U)
        {
            tvp_capture_short_frame_drop_count_dbg++;
            tvp_capture_line_write_index = 0U;
            tvp_capture_line_write_index_dbg = 0U;
            return;
        }
    }

    if (next == locked)
    {
        if ((tvp_capture_frame_pending_dbg != 0U) &&
            (tvp_capture_ready_buf_dbg == completed))
        {
            tvp_capture_frame_pending_dbg = 0U;
            tvp_capture_ready_buf_dbg = 0xFFFFFFFFU;
            tvp_capture_ready_overwrite_count_dbg++;
        }
        tvp_capture_render_lock_drop_count_dbg++;
        tvp_capture_active_buf_dbg = completed;
    }
    else
    {
        if (publish != 0U)
        {
            if ((tvp_capture_frame_pending_dbg != 0U) &&
                (tvp_capture_ready_buf_dbg == next))
            {
                tvp_capture_ready_overwrite_count_dbg++;
            }

            tvp_capture_ready_buf_dbg = completed;
            tvp_capture_frame_pending_dbg = 1U;
            tvp_capture_field_publish_count_dbg++;
        }
        else
        {
            if ((tvp_capture_frame_pending_dbg != 0U) &&
                ((tvp_capture_ready_buf_dbg == next) ||
                 (tvp_capture_ready_buf_dbg == completed)))
            {
                tvp_capture_frame_pending_dbg = 0U;
                tvp_capture_ready_buf_dbg = 0xFFFFFFFFU;
                tvp_capture_ready_overwrite_count_dbg++;
            }
            tvp_capture_field_phase_drop_count_dbg++;
        }

        tvp_capture_active_buf_dbg = next;
    }

    tvp_capture_dma_target_dbg = (uint32_t)tvp_capture_buffer(tvp_capture_active_buf_dbg);
    hdcmi.pBuffPtr = tvp_capture_dma_target_dbg;
    tvp_capture_line_write_index = 0U;
    tvp_capture_line_write_index_dbg = 0U;
}

static uint32_t tvp_capture_read_sync_pins(void)
{
    uint32_t pins = 0U;

    pins |= ((GPIOA->IDR & GPIO_PIN_6) != 0U) ? 0x01U : 0U; /* PIXCLK PA6 */
    pins |= ((GPIOA->IDR & GPIO_PIN_4) != 0U) ? 0x02U : 0U; /* HSYNC PA4 */
    pins |= ((GPIOG->IDR & GPIO_PIN_9) != 0U) ? 0x04U : 0U; /* VSYNC PG9 */

    return pins;
}

static uint32_t tvp_capture_read_data_pins(void)
{
    uint32_t data = 0U;

    data |= ((GPIOC->IDR & GPIO_PIN_6) != 0U) ? 0x01U : 0U;  /* D0 */
    data |= ((GPIOC->IDR & GPIO_PIN_7) != 0U) ? 0x02U : 0U;  /* D1 */
    data |= ((GPIOH->IDR & GPIO_PIN_11) != 0U) ? 0x04U : 0U; /* D2 */
    data |= ((GPIOH->IDR & GPIO_PIN_12) != 0U) ? 0x08U : 0U; /* D3 */
    data |= ((GPIOH->IDR & GPIO_PIN_14) != 0U) ? 0x10U : 0U; /* D4 */
    data |= ((GPIOD->IDR & GPIO_PIN_3) != 0U) ? 0x20U : 0U;  /* D5 */
    data |= ((GPIOB->IDR & GPIO_PIN_8) != 0U) ? 0x40U : 0U;  /* D6 */
    data |= ((GPIOB->IDR & GPIO_PIN_9) != 0U) ? 0x80U : 0U;  /* D7 */

    return data;
}

static void tvp_capture_sample_pins(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t prev_sync;
    uint32_t sync;
    uint32_t data;
    uint32_t prev_data;
    uint32_t data_or;
    uint32_t data_and;
    uint32_t data_change;
    uint32_t pclk_edges = 0U;
    uint32_t hsync_edges = 0U;
    uint32_t vsync_edges = 0U;

    if ((uint32_t)(now - tvp_capture_last_pin_sample_tick) < 100U)
    {
        return;
    }

    tvp_capture_last_pin_sample_tick = now;
    prev_sync = tvp_capture_read_sync_pins();
    data = tvp_capture_read_data_pins();
    prev_data = data;
    data_or = data;
    data_and = data;
    data_change = 0U;

    for (uint32_t i = 0U; i < 8192U; i++)
    {
        sync = tvp_capture_read_sync_pins();
        data = tvp_capture_read_data_pins();

        pclk_edges += (((sync ^ prev_sync) & 0x01U) != 0U) ? 1U : 0U;
        hsync_edges += (((sync ^ prev_sync) & 0x02U) != 0U) ? 1U : 0U;
        vsync_edges += (((sync ^ prev_sync) & 0x04U) != 0U) ? 1U : 0U;
        data_or |= data;
        data_and &= data;
        data_change |= (data ^ prev_data);
        prev_sync = sync;
        prev_data = data;
    }

    tvp_capture_pin_levels_dbg = (prev_sync & 0x07U) | (data << 8);
    tvp_capture_pclk_edges_dbg = pclk_edges;
    tvp_capture_hsync_edges_dbg = hsync_edges;
    tvp_capture_vsync_edges_dbg = vsync_edges;
    tvp_capture_data_or_dbg = data_or;
    tvp_capture_data_and_dbg = data_and;
    tvp_capture_data_change_mask_dbg = data_change;
    tvp_capture_pin_sample_tick_dbg = now;
    tvp_capture_pin_sample_count_dbg++;
}

static uint32_t *tvp_capture_buffer(uint32_t index)
{
    if (tvp_capture_crop_test_enable_dbg != 0U)
    {
        return tvp5150_ycbcr422_test_buf;
    }

    return (index == 0U) ? tvp5150_ycbcr422_buf0 : tvp5150_ycbcr422_buf1;
}

static void tvp_capture_sample_buffer(uint32_t index)
{
    uint32_t *buf = tvp_capture_buffer(index);
    uint32_t checksum = 0U;

    tvp_capture_first_word0_dbg = buf[0];
    tvp_capture_first_word1_dbg = buf[1];
    tvp_capture_first_word2_dbg = buf[2];
    tvp_capture_first_word3_dbg = buf[3];

    for (uint32_t i = 0; i < 64U; i++)
    {
        checksum += buf[i];
        checksum = (checksum << 1) | (checksum >> 31);
    }
    tvp_capture_head_checksum_dbg = checksum;
}

static uint8_t tvp_capture_clip_u8(int32_t v)
{
    if (v < 0)
    {
        return 0U;
    }
    if (v > 255)
    {
        return 255U;
    }
    return (uint8_t)v;
}

static uint16_t tvp_capture_ycbcr_to_rgb565(uint8_t y, uint8_t cb, uint8_t cr)
{
    int32_t c = (int32_t)y - 16;
    int32_t d = (int32_t)cb - 128;
    int32_t e = (int32_t)cr - 128;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (c < 0)
    {
        c = 0;
    }

    r = tvp_capture_clip_u8((298 * c + 409 * e + 128) >> 8);
    g = tvp_capture_clip_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    b = tvp_capture_clip_u8((298 * c + 516 * d + 128) >> 8);

    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                      ((uint16_t)(g & 0xFCU) << 3) |
                      ((uint16_t)b >> 3));
}

static inline __attribute__((always_inline)) uint8_t tvp_capture_clip_u8_inline(int32_t v)
{
    if (v < 0)
    {
        return 0U;
    }
    if (v > 255)
    {
        return 255U;
    }
    return (uint8_t)v;
}

static inline __attribute__((always_inline)) uint16_t tvp_capture_ycbcr_to_rgb565_inline(uint8_t y,
                                                                                         uint8_t cb,
                                                                                         uint8_t cr)
{
    int32_t c = (int32_t)y - 16;
    int32_t d = (int32_t)cb - 128;
    int32_t e = (int32_t)cr - 128;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (c < 0)
    {
        c = 0;
    }

    r = tvp_capture_clip_u8_inline((298 * c + 409 * e + 128) >> 8);
    g = tvp_capture_clip_u8_inline((298 * c - 100 * d - 208 * e + 128) >> 8);
    b = tvp_capture_clip_u8_inline((298 * c + 516 * d + 128) >> 8);

    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                      ((uint16_t)(g & 0xFCU) << 3) |
                      ((uint16_t)b >> 3));
}

static void tvp_capture_read_ycbcr_pair(const uint8_t *pair,
                                        uint8_t *y0,
                                        uint8_t *y1,
                                        uint8_t *cb,
                                        uint8_t *cr)
{
    uint8_t b0 = pair[0];
    uint8_t b1 = pair[1];
    uint8_t b2 = pair[2];
    uint8_t b3 = pair[3];

    switch (tvp_capture_render_byte_order_dbg & 3U)
    {
    default:
    case 0U: /* UYVY: Cb Y0 Cr Y1 */
        *cb = b0;
        *y0 = b1;
        *cr = b2;
        *y1 = b3;
        break;

    case 1U: /* YUYV: Y0 Cb Y1 Cr */
        *y0 = b0;
        *cb = b1;
        *y1 = b2;
        *cr = b3;
        break;

    case 2U: /* VYUY: Cr Y0 Cb Y1 */
        *cr = b0;
        *y0 = b1;
        *cb = b2;
        *y1 = b3;
        break;

    case 3U: /* YVYU: Y0 Cr Y1 Cb */
        *y0 = b0;
        *cr = b1;
        *y1 = b2;
        *cb = b3;
        break;
    }
}

static uint16_t tvp_capture_read_rgb565_pixel(const uint8_t *pair, uint32_t odd_pixel)
{
    uint8_t y0;
    uint8_t y1;
    uint8_t cb;
    uint8_t cr;

    tvp_capture_read_ycbcr_pair(pair, &y0, &y1, &cb, &cr);

    return tvp_capture_ycbcr_to_rgb565((odd_pixel == 0U) ? y0 : y1, cb, cr);
}

static uint32_t tvp_capture_checksum_rgb565_area(uint16_t *dst,
                                                 uint32_t stride,
                                                 uint32_t width,
                                                 uint32_t height)
{
    uint32_t checksum = 0U;
    uint32_t samples = 0U;

    for (uint32_t y = 0U; (y < height) && (samples < 128U); y++)
    {
        for (uint32_t x = 0U; (x < width) && (samples < 128U); x++)
        {
            checksum += dst[(y * stride) + x];
            checksum = (checksum << 1) | (checksum >> 31);
            samples++;
        }
    }

    return checksum;
}

static HAL_StatusTypeDef tvp_capture_render_to_rgb565_dma2d(uint32_t index,
                                                            uint32_t dst_x,
                                                            uint32_t dst_y,
                                                            uint32_t dst_w,
                                                            uint32_t dst_h)
{
    uint32_t field_height = tvp_capture_active_field_height();
    uint32_t raw_line_bytes = tvp_capture_current_line_bytes();
    uint32_t raw_width = raw_line_bytes / TVP5150_CAPTURE_BPP;
    uint32_t src_x = tvp_capture_render_src_x_dbg;
    uint32_t src_w = TVP5150_CAPTURE_WIDTH;
    uint32_t src_y = tvp_capture_render_src_y_dbg;
    uint8_t *src;
    uint16_t *dst;
    HAL_StatusTypeDef status;

    if (dst_w < 2U || dst_h == 0U)
    {
        return HAL_ERROR;
    }

    if ((tvp_capture_hw_crop_enable_dbg != 0U) &&
        (tvp_capture_crop_test_enable_dbg == 0U))
    {
        src_x = 0U;
    }
    if (src_x >= raw_width)
    {
        src_x = 0U;
    }
    if (src_y >= field_height)
    {
        src_y = 0U;
    }

    src_x &= ~1U;
    dst_w &= ~1U;
    if ((src_x + src_w) > raw_width)
    {
        src_w = (raw_width - src_x) & ~1U;
    }
    if (dst_w > src_w)
    {
        dst_w = src_w;
    }
    if ((src_y + dst_h) > field_height)
    {
        dst_h = field_height - src_y;
    }
    if (dst_w < 2U || dst_h == 0U)
    {
        return HAL_ERROR;
    }

    src = ((uint8_t *)tvp_capture_buffer(index)) +
          (src_y * raw_line_bytes) +
          (src_x * TVP5150_CAPTURE_BPP);
    dst = &tvp_capture_rgb565_target[(dst_y * tvp_capture_rgb565_target_w) + dst_x];

    if (HAL_DMA2D_GetState(&hdma2d) == HAL_DMA2D_STATE_BUSY)
    {
        tvp_capture_render_dma2d_error_dbg = hdma2d.ErrorCode;
        return HAL_BUSY;
    }

    hdma2d.Init.Mode = DMA2D_M2M_PFC;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = tvp_capture_rgb565_target_w - dst_w;
    hdma2d.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;
    hdma2d.Init.RedBlueSwap = DMA2D_RB_REGULAR;
    hdma2d.Init.BytesSwap = DMA2D_BYTES_REGULAR;
    hdma2d.Init.LineOffsetMode = DMA2D_LOM_PIXELS;

    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].InputOffset = raw_width - dst_w;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].InputColorMode = DMA2D_INPUT_YCBCR;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].AlphaMode = DMA2D_NO_MODIF_ALPHA;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].InputAlpha = 0xFFU;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].AlphaInverted = DMA2D_REGULAR_ALPHA;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].RedBlueSwap = DMA2D_RB_REGULAR;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].ChromaSubSampling = DMA2D_CSS_422;

    status = HAL_DMA2D_Init(&hdma2d);
    if (status == HAL_OK)
    {
        status = HAL_DMA2D_ConfigLayer(&hdma2d, DMA2D_FOREGROUND_LAYER);
    }
    if (status == HAL_OK)
    {
        status = HAL_DMA2D_Start(&hdma2d,
                                 (uint32_t)src,
                                 (uint32_t)dst,
                                 dst_w,
                                 dst_h);
    }
    if (status == HAL_OK)
    {
        status = HAL_DMA2D_PollForTransfer(&hdma2d, 50U);
    }

    tvp_capture_render_dma2d_status_dbg = status;
    tvp_capture_render_dma2d_error_dbg = hdma2d.ErrorCode;
    if (status == HAL_OK)
    {
        tvp_capture_render_dma2d_count_dbg++;
        tvp_capture_render_checksum_dbg =
            tvp_capture_checksum_rgb565_area(dst,
                                             tvp_capture_rgb565_target_w,
                                             dst_w,
                                             dst_h);
    }

    return status;
}

static HAL_StatusTypeDef tvp_capture_render_to_rgb565_fast_cpu(uint32_t index,
                                                               uint32_t dst_x,
                                                               uint32_t dst_y,
                                                               uint32_t dst_w,
                                                               uint32_t dst_h)
{
    uint32_t build_start = HAL_GetTick();
    uint32_t field_height = tvp_capture_active_field_height();
    uint32_t raw_line_bytes = tvp_capture_current_line_bytes();
    uint32_t raw_width = raw_line_bytes / TVP5150_CAPTURE_BPP;
    uint32_t src_base_x = tvp_capture_render_src_x_dbg;
    uint32_t src_base_y = tvp_capture_render_src_y_dbg;
    uint32_t src_w = TVP5150_CAPTURE_WIDTH;
    uint32_t src_h;
    uint32_t x_step;
    uint32_t y_step;
    uint32_t y_acc = 0U;
    uint32_t checksum = 0U;
    uint32_t order = tvp_capture_render_byte_order_dbg & 3U;
    const uint8_t *src;

    tvp_capture_render_fast_status_dbg = HAL_ERROR;

    if (dst_w == 0U || dst_h == 0U)
    {
        return HAL_ERROR;
    }

    if ((tvp_capture_hw_crop_enable_dbg != 0U) &&
        (tvp_capture_crop_test_enable_dbg == 0U))
    {
        src_base_x = 0U;
    }
    if (src_base_x >= raw_width)
    {
        src_base_x = 0U;
    }
    src_base_x &= ~1U;

    if ((src_base_x + src_w) > raw_width)
    {
        src_w = raw_width - src_base_x;
    }
    src_w &= ~1U;
    if (src_w < 2U)
    {
        return HAL_ERROR;
    }

    if (src_base_y >= field_height)
    {
        src_base_y = 0U;
    }
    src_h = field_height - src_base_y;
    if ((tvp_capture_last_frame_line_count_dbg > src_base_y) &&
        (tvp_capture_last_frame_line_count_dbg <= field_height))
    {
        src_h = tvp_capture_last_frame_line_count_dbg - src_base_y;
    }
    if (src_h == 0U)
    {
        return HAL_ERROR;
    }

    src = (const uint8_t *)tvp_capture_buffer(index);
    x_step = (src_w << 16) / dst_w;
    y_step = (src_h << 16) / dst_h;

    for (uint32_t y = 0U; y < dst_h; y++)
    {
        uint32_t src_y = src_base_y + (y_acc >> 16);
        uint32_t x_acc = 0U;
        const uint8_t *src_line;
        uint16_t *dst = &tvp_capture_rgb565_target[(dst_y + y) * tvp_capture_rgb565_target_w + dst_x];

        if (src_y >= field_height)
        {
            src_y = field_height - 1U;
        }
        src_line = src + (src_y * raw_line_bytes);

        for (uint32_t x = 0U; x < dst_w; x++)
        {
            uint32_t src_x = x_acc >> 16;
            uint32_t pair_x;
            const uint8_t *pair;
            uint8_t yv;
            uint8_t cb;
            uint8_t cr;
            uint16_t rgb;

            if (src_x >= src_w)
            {
                src_x = src_w - 1U;
            }

            pair_x = src_base_x + (src_x & ~1U);
            pair = src_line + (pair_x * TVP5150_CAPTURE_BPP);

            switch (order)
            {
            default:
            case 0U: /* UYVY */
                cb = pair[0];
                yv = ((src_x & 1U) == 0U) ? pair[1] : pair[3];
                cr = pair[2];
                break;

            case 1U: /* YUYV */
                yv = ((src_x & 1U) == 0U) ? pair[0] : pair[2];
                cb = pair[1];
                cr = pair[3];
                break;

            case 2U: /* VYUY */
                cr = pair[0];
                yv = ((src_x & 1U) == 0U) ? pair[1] : pair[3];
                cb = pair[2];
                break;

            case 3U: /* YVYU */
                yv = ((src_x & 1U) == 0U) ? pair[0] : pair[2];
                cr = pair[1];
                cb = pair[3];
                break;
            }

            rgb = tvp_capture_ycbcr_to_rgb565_inline(yv, cb, cr);
            dst[x] = rgb;
            checksum += rgb;
            checksum = (checksum << 1) | (checksum >> 31);
            x_acc += x_step;
        }

        y_acc += y_step;
    }

    tvp_capture_render_checksum_dbg = checksum;
    tvp_capture_render_fast_build_ms_dbg = HAL_GetTick() - build_start;
    tvp_capture_render_fast_count_dbg++;
    tvp_capture_render_fast_status_dbg = HAL_OK;
    return HAL_OK;
}

static HAL_StatusTypeDef tvp_capture_render_to_rgb565_fast_dma2d(uint32_t index,
                                                                 uint32_t dst_x,
                                                                 uint32_t dst_y,
                                                                 uint32_t dst_w,
                                                                 uint32_t dst_h)
{
    uint32_t build_start = HAL_GetTick();
    uint32_t field_height = tvp_capture_active_field_height();
    uint32_t raw_line_bytes = tvp_capture_current_line_bytes();
    uint32_t raw_width = raw_line_bytes / TVP5150_CAPTURE_BPP;
    uint32_t src_base_x = tvp_capture_render_src_x_dbg;
    uint32_t src_base_y = tvp_capture_render_src_y_dbg;
    uint32_t src_w = TVP5150_CAPTURE_WIDTH;
    uint32_t src_h;
    uint32_t dma_w;
    uint32_t dma_h;
    uint32_t x_step;
    uint32_t y_step;
    uint32_t mcu_index = 0U;
    const uint8_t *src;
    uint16_t *dst;
    HAL_StatusTypeDef status;

    tvp_capture_render_fast_status_dbg = HAL_ERROR;
    tvp_capture_render_dma2d_status_dbg = HAL_ERROR;

    if ((dst_w > TVP_CAPTURE_RENDER_FAST_W_MAX) ||
        (dst_h > TVP_CAPTURE_RENDER_FAST_H_MAX))
    {
        return HAL_ERROR;
    }

    dma_w = dst_w & ~(TVP_CAPTURE_DMA2D_MCU422_W - 1U);
    dma_h = dst_h & ~(TVP_CAPTURE_DMA2D_MCU422_H - 1U);
    if ((dma_w < TVP_CAPTURE_DMA2D_MCU422_W) ||
        (dma_h < TVP_CAPTURE_DMA2D_MCU422_H))
    {
        return HAL_ERROR;
    }

    if ((tvp_capture_hw_crop_enable_dbg != 0U) &&
        (tvp_capture_crop_test_enable_dbg == 0U))
    {
        src_base_x = 0U;
    }
    if (src_base_x >= raw_width)
    {
        src_base_x = 0U;
    }
    src_base_x &= ~1U;

    if ((src_base_x + src_w) > raw_width)
    {
        src_w = raw_width - src_base_x;
    }
    src_w &= ~1U;
    if (src_w < 2U)
    {
        return HAL_ERROR;
    }

    if (src_base_y >= field_height)
    {
        src_base_y = 0U;
    }
    src_h = field_height - src_base_y;
    if ((tvp_capture_last_frame_line_count_dbg > src_base_y) &&
        (tvp_capture_last_frame_line_count_dbg <= field_height))
    {
        src_h = tvp_capture_last_frame_line_count_dbg - src_base_y;
    }
    if (src_h == 0U)
    {
        return HAL_ERROR;
    }

    src = (const uint8_t *)tvp_capture_buffer(index);
    x_step = (src_w << 16) / dma_w;
    y_step = (src_h << 16) / dma_h;

    for (uint32_t by = 0U; by < dma_h; by += TVP_CAPTURE_DMA2D_MCU422_H)
    {
        for (uint32_t bx = 0U; bx < dma_w; bx += TVP_CAPTURE_DMA2D_MCU422_W)
        {
            uint8_t *mcu = &tvp5150_render_ycbcr422_buf[mcu_index * TVP_CAPTURE_DMA2D_MCU422_BYTES];
            uint8_t *y_left = mcu;
            uint8_t *y_right = mcu + 64U;
            uint8_t *cb_block = mcu + 128U;
            uint8_t *cr_block = mcu + 192U;

            for (uint32_t my = 0U; my < TVP_CAPTURE_DMA2D_MCU422_H; my++)
            {
                uint32_t out_y = by + my;
                uint32_t src_y = src_base_y + ((out_y * y_step) >> 16);
                const uint8_t *src_line;

                if (src_y >= field_height)
                {
                    src_y = field_height - 1U;
                }
                src_line = src + (src_y * raw_line_bytes);

                for (uint32_t mx = 0U; mx < (TVP_CAPTURE_DMA2D_MCU422_W / 2U); mx++)
                {
                    uint32_t out_x0 = bx + (mx * 2U);
                    uint32_t out_x1 = out_x0 + 1U;
                    uint32_t src_x0 = (out_x0 * x_step) >> 16;
                    uint32_t src_x1 = (out_x1 * x_step) >> 16;
                    uint32_t pair_x0;
                    uint32_t pair_x1;
                    uint32_t y_offset;
                    uint8_t y00;
                    uint8_t y01;
                    uint8_t cb0;
                    uint8_t cr0;
                    uint8_t y10;
                    uint8_t y11;
                    uint8_t cb1;
                    uint8_t cr1;
                    uint8_t y0;
                    uint8_t y1;
                    uint8_t cb;
                    uint8_t cr;

                    if (src_x0 >= src_w)
                    {
                        src_x0 = src_w - 1U;
                    }
                    if (src_x1 >= src_w)
                    {
                        src_x1 = src_w - 1U;
                    }

                    pair_x0 = src_base_x + (src_x0 & ~1U);
                    pair_x1 = src_base_x + (src_x1 & ~1U);
                    tvp_capture_read_ycbcr_pair(src_line + (pair_x0 * TVP5150_CAPTURE_BPP),
                                                &y00, &y01, &cb0, &cr0);
                    tvp_capture_read_ycbcr_pair(src_line + (pair_x1 * TVP5150_CAPTURE_BPP),
                                                &y10, &y11, &cb1, &cr1);

                    y0 = ((src_x0 & 1U) == 0U) ? y00 : y01;
                    y1 = ((src_x1 & 1U) == 0U) ? y10 : y11;
                    cb = (uint8_t)(((uint16_t)cb0 + (uint16_t)cb1) >> 1);
                    cr = (uint8_t)(((uint16_t)cr0 + (uint16_t)cr1) >> 1);

                    if (mx < 4U)
                    {
                        y_offset = (my * 8U) + (mx * 2U);
                        y_left[y_offset] = y0;
                        y_left[y_offset + 1U] = y1;
                    }
                    else
                    {
                        y_offset = (my * 8U) + ((mx - 4U) * 2U);
                        y_right[y_offset] = y0;
                        y_right[y_offset + 1U] = y1;
                    }

                    cb_block[(my * 8U) + mx] = cb;
                    cr_block[(my * 8U) + mx] = cr;
                }
            }

            mcu_index++;
        }
    }

    tvp_capture_render_fast_build_ms_dbg = HAL_GetTick() - build_start;
    dst = &tvp_capture_rgb565_target[(dst_y * tvp_capture_rgb565_target_w) + dst_x];

    if (HAL_DMA2D_GetState(&hdma2d) == HAL_DMA2D_STATE_BUSY)
    {
        tvp_capture_render_dma2d_error_dbg = hdma2d.ErrorCode;
        tvp_capture_render_fast_status_dbg = HAL_BUSY;
        return HAL_BUSY;
    }

    hdma2d.Init.Mode = DMA2D_M2M_PFC;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = tvp_capture_rgb565_target_w - dma_w;
    hdma2d.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;
    hdma2d.Init.RedBlueSwap = DMA2D_RB_REGULAR;
    hdma2d.Init.BytesSwap = DMA2D_BYTES_REGULAR;
    hdma2d.Init.LineOffsetMode = DMA2D_LOM_PIXELS;

    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].InputOffset = 0U;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].InputColorMode = DMA2D_INPUT_YCBCR;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].AlphaMode = DMA2D_NO_MODIF_ALPHA;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].InputAlpha = 0xFFU;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].AlphaInverted = DMA2D_REGULAR_ALPHA;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].RedBlueSwap = DMA2D_RB_REGULAR;
    hdma2d.LayerCfg[DMA2D_FOREGROUND_LAYER].ChromaSubSampling = DMA2D_CSS_422;

    status = HAL_DMA2D_Init(&hdma2d);
    if (status == HAL_OK)
    {
        status = HAL_DMA2D_ConfigLayer(&hdma2d, DMA2D_FOREGROUND_LAYER);
    }
    if (status == HAL_OK)
    {
        status = HAL_DMA2D_Start(&hdma2d,
                                 (uint32_t)tvp5150_render_ycbcr422_buf,
                                 (uint32_t)dst,
                                 dma_w,
                                 dma_h);
    }
    if (status == HAL_OK)
    {
        status = HAL_DMA2D_PollForTransfer(&hdma2d, 50U);
    }

    tvp_capture_render_fast_status_dbg = status;
    tvp_capture_render_dma2d_status_dbg = status;
    tvp_capture_render_dma2d_error_dbg = hdma2d.ErrorCode;
    if (status == HAL_OK)
    {
        tvp_capture_render_fast_count_dbg++;
        tvp_capture_render_dma2d_count_dbg++;
        tvp_capture_render_checksum_dbg =
            tvp_capture_checksum_rgb565_area(dst,
                                             tvp_capture_rgb565_target_w,
                                             dma_w,
                                             dma_h);
    }

    return status;
}

static void tvp_capture_render_to_rgb565(uint32_t index)
{
    uint32_t now = HAL_GetTick();
    uint32_t start;
    uint32_t elapsed;
    uint32_t dst_x = tvp_capture_render_x_dbg;
    uint32_t dst_y = tvp_capture_render_y_dbg;
    uint32_t dst_w = tvp_capture_render_w_dbg;
    uint32_t dst_h = tvp_capture_render_h_dbg;
    uint32_t x_step;
    uint32_t y_step;
    uint32_t y_acc;
    uint32_t field_height;
    uint32_t raw_line_bytes;
    uint32_t raw_width;
    uint32_t src_base_x;
    uint32_t src_base_y;
    uint32_t src_w;
    uint32_t src_h;
    uint32_t checksum = 0U;
    const uint8_t *src;

    if (tvp_capture_render_enable_dbg == 0U)
    {
        return;
    }

    if ((tvp_capture_rgb565_target == NULL) ||
        (tvp_capture_rgb565_target_w == 0U) ||
        (tvp_capture_rgb565_target_h == 0U))
    {
        tvp_capture_render_skip_no_target_dbg++;
        return;
    }

    if ((tvp_capture_render_count_dbg != 0U) &&
        (tvp_capture_render_period_ms_dbg != 0U) &&
        ((uint32_t)(now - tvp_capture_render_last_tick_dbg) < tvp_capture_render_period_ms_dbg))
    {
        tvp_capture_render_skip_period_dbg++;
        return;
    }

    if (dst_x >= tvp_capture_rgb565_target_w || dst_y >= tvp_capture_rgb565_target_h)
    {
        tvp_capture_render_skip_no_target_dbg++;
        return;
    }
    if (dst_w > (tvp_capture_rgb565_target_w - dst_x))
    {
        dst_w = tvp_capture_rgb565_target_w - dst_x;
    }
    if (dst_h > (tvp_capture_rgb565_target_h - dst_y))
    {
        dst_h = tvp_capture_rgb565_target_h - dst_y;
    }
    if (dst_w == 0U || dst_h == 0U)
    {
        tvp_capture_render_skip_no_target_dbg++;
        return;
    }

    start = now;
    if (tvp_capture_render_method_dbg == 2U)
    {
        if (tvp_capture_render_to_rgb565_fast_dma2d(index, dst_x, dst_y, dst_w, dst_h) == HAL_OK)
        {
            tvp_capture_render_source_buf_dbg = index;
            tvp_capture_render_count_dbg++;
            tvp_capture_render_last_tick_dbg = HAL_GetTick();
            elapsed = tvp_capture_render_last_tick_dbg - start;
            tvp_capture_render_last_ms_dbg = elapsed;
            if (elapsed > tvp_capture_render_max_ms_dbg)
            {
                tvp_capture_render_max_ms_dbg = elapsed;
            }
            return;
        }

        if (tvp_capture_render_to_rgb565_fast_cpu(index, dst_x, dst_y, dst_w, dst_h) == HAL_OK)
        {
            tvp_capture_render_source_buf_dbg = index;
            tvp_capture_render_count_dbg++;
            tvp_capture_render_last_tick_dbg = HAL_GetTick();
            elapsed = tvp_capture_render_last_tick_dbg - start;
            tvp_capture_render_last_ms_dbg = elapsed;
            if (elapsed > tvp_capture_render_max_ms_dbg)
            {
                tvp_capture_render_max_ms_dbg = elapsed;
            }
            return;
        }
    }
    else if ((tvp_capture_render_method_dbg == 1U) &&
             ((tvp_capture_render_byte_order_dbg & 3U) == 0U))
    {
        if (tvp_capture_render_to_rgb565_dma2d(index, dst_x, dst_y, dst_w, dst_h) == HAL_OK)
        {
            tvp_capture_render_source_buf_dbg = index;
            tvp_capture_render_count_dbg++;
            tvp_capture_render_last_tick_dbg = HAL_GetTick();
            elapsed = tvp_capture_render_last_tick_dbg - start;
            tvp_capture_render_last_ms_dbg = elapsed;
            if (elapsed > tvp_capture_render_max_ms_dbg)
            {
                tvp_capture_render_max_ms_dbg = elapsed;
            }
            return;
        }

        return;
    }
    else if (tvp_capture_render_method_dbg == 1U)
    {
        return;
    }

    src = (const uint8_t *)tvp_capture_buffer(index);
    field_height = tvp_capture_active_field_height();
    raw_line_bytes = tvp_capture_current_line_bytes();
    raw_width = raw_line_bytes / TVP5150_CAPTURE_BPP;
    src_base_x = tvp_capture_render_src_x_dbg;
    if ((tvp_capture_hw_crop_enable_dbg != 0U) &&
        (tvp_capture_crop_test_enable_dbg == 0U))
    {
        src_base_x = 0U;
    }
    if (src_base_x >= raw_width)
    {
        src_base_x = 0U;
    }
    src_base_x &= ~1U;
    src_w = TVP5150_CAPTURE_WIDTH;
    if ((src_base_x + src_w) > raw_width)
    {
        src_w = raw_width - src_base_x;
    }
    src_w &= ~1U;
    if (src_w < 2U)
    {
        tvp_capture_render_skip_no_target_dbg++;
        return;
    }

    src_base_y = tvp_capture_render_src_y_dbg;
    if (src_base_y >= field_height)
    {
        src_base_y = 0U;
    }
    src_h = field_height - src_base_y;
    if ((tvp_capture_last_frame_line_count_dbg > src_base_y) &&
        (tvp_capture_last_frame_line_count_dbg <= field_height))
    {
        src_h = tvp_capture_last_frame_line_count_dbg - src_base_y;
    }
    if (src_h == 0U)
    {
        tvp_capture_render_skip_no_target_dbg++;
        return;
    }

    x_step = (src_w << 16) / dst_w;
    y_step = (src_h << 16) / dst_h;
    y_acc = 0U;

    for (uint32_t y = 0U; y < dst_h; y++)
    {
        uint32_t src_y = src_base_y + (y_acc >> 16);
        uint32_t x_acc = 0U;
        uint16_t *dst = &tvp_capture_rgb565_target[(dst_y + y) * tvp_capture_rgb565_target_w + dst_x];
        const uint8_t *src_line;

        if (src_y >= field_height)
        {
            src_y = field_height - 1U;
        }
        src_line = src + (src_y * raw_line_bytes);

        for (uint32_t x = 0U; x < dst_w; x++)
        {
            uint32_t src_x = x_acc >> 16;
            uint32_t pair_x;
            uint16_t rgb;

            if (src_x >= src_w)
            {
                src_x = src_w - 1U;
            }

            pair_x = src_base_x + (src_x & ~1U);
            rgb = tvp_capture_read_rgb565_pixel(src_line + (pair_x * TVP5150_CAPTURE_BPP),
                                                src_x & 1U);
            dst[x] = rgb;
            checksum += rgb;
            checksum = (checksum << 1) | (checksum >> 31);
            x_acc += x_step;
        }

        y_acc += y_step;
    }

    tvp_capture_render_source_buf_dbg = index;
    tvp_capture_render_cpu_count_dbg++;
    tvp_capture_render_count_dbg++;
    tvp_capture_render_last_tick_dbg = HAL_GetTick();
    elapsed = tvp_capture_render_last_tick_dbg - start;
    tvp_capture_render_last_ms_dbg = elapsed;
    if (elapsed > tvp_capture_render_max_ms_dbg)
    {
        tvp_capture_render_max_ms_dbg = elapsed;
    }
    tvp_capture_render_checksum_dbg = checksum;
}

static void tvp_capture_sample_regs(void)
{
    if (hdcmi.Instance != 0)
    {
        tvp_capture_dcmi_cr_dbg = hdcmi.Instance->CR;
        tvp_capture_dcmi_sr_dbg = hdcmi.Instance->SR;
        tvp_capture_dcmi_risr_dbg = hdcmi.Instance->RISR;
        tvp_capture_dcmi_misr_dbg = hdcmi.Instance->MISR;
        tvp_capture_dcmi_cwstrtr_dbg = hdcmi.Instance->CWSTRTR;
        tvp_capture_dcmi_cwsizer_dbg = hdcmi.Instance->CWSIZER;
        tvp_capture_dcmi_error_dbg = hdcmi.ErrorCode;
        tvp_capture_xfer_size_dbg = hdcmi.XferSize;
        tvp_capture_xfer_count_dbg = hdcmi.XferCount;
        tvp_capture_xfer_transfer_number_dbg = hdcmi.XferTransferNumber;
        tvp_capture_pbuff_ptr_dbg = hdcmi.pBuffPtr;
    }

    if (hdma_dcmi.Instance != 0)
    {
        DMA_Stream_TypeDef *dma = (DMA_Stream_TypeDef *)hdma_dcmi.Instance;
        uint32_t segment_length;

        tvp_capture_dma_cr_dbg = dma->CR;
        tvp_capture_dma_fcr_dbg = dma->FCR;
        tvp_capture_dma_ndtr_dbg = dma->NDTR;
        tvp_capture_dma_par_dbg = dma->PAR;
        tvp_capture_dma_m0ar_dbg = dma->M0AR;
        tvp_capture_dma_m1ar_dbg = dma->M1AR;
        tvp_capture_dma_error_dbg = hdma_dcmi.ErrorCode;

        segment_length = (hdcmi.XferSize != 0U) ? hdcmi.XferSize : tvp_capture_dma_length_dbg;
        tvp_capture_dma_segment_length_dbg = segment_length;
        tvp_capture_dma_words_done_dbg =
            (segment_length >= tvp_capture_dma_ndtr_dbg) ?
            (segment_length - tvp_capture_dma_ndtr_dbg) : 0U;
    }

    if (hmdma_tvp_capture.Instance != 0)
    {
        MDMA_Channel_TypeDef *mdma = (MDMA_Channel_TypeDef *)hmdma_tvp_capture.Instance;

        tvp_capture_mdma_error_dbg = hmdma_tvp_capture.ErrorCode;
        tvp_capture_mdma_cisr_dbg = mdma->CISR;
        tvp_capture_mdma_cesr_dbg = mdma->CESR;
        tvp_capture_mdma_ccr_dbg = mdma->CCR;
        tvp_capture_mdma_ctcr_dbg = mdma->CTCR;
        tvp_capture_mdma_cbndtr_dbg = mdma->CBNDTR;
        tvp_capture_mdma_csar_dbg = mdma->CSAR;
        tvp_capture_mdma_cdar_dbg = mdma->CDAR;
    }
}

static HAL_StatusTypeDef tvp_capture_mdma_init(void)
{
    __HAL_RCC_MDMA_CLK_ENABLE();

    hmdma_tvp_capture.Instance = MDMA_Channel2;
    hmdma_tvp_capture.Init.Request = MDMA_REQUEST_SW;
    hmdma_tvp_capture.Init.TransferTriggerMode = MDMA_BLOCK_TRANSFER;
    hmdma_tvp_capture.Init.Priority = MDMA_PRIORITY_VERY_HIGH;
    hmdma_tvp_capture.Init.Endianness = MDMA_LITTLE_ENDIANNESS_PRESERVE;
    hmdma_tvp_capture.Init.SourceInc = MDMA_SRC_INC_WORD;
    hmdma_tvp_capture.Init.DestinationInc = MDMA_DEST_INC_WORD;
    hmdma_tvp_capture.Init.SourceDataSize = MDMA_SRC_DATASIZE_WORD;
    hmdma_tvp_capture.Init.DestDataSize = MDMA_DEST_DATASIZE_WORD;
    hmdma_tvp_capture.Init.DataAlignment = MDMA_DATAALIGN_PACKENABLE;
    hmdma_tvp_capture.Init.SourceBurst = MDMA_SOURCE_BURST_8BEATS;
    hmdma_tvp_capture.Init.DestBurst = MDMA_DEST_BURST_8BEATS;
    hmdma_tvp_capture.Init.BufferTransferLength = 128U;
    hmdma_tvp_capture.Init.SourceBlockAddressOffset = 0;
    hmdma_tvp_capture.Init.DestBlockAddressOffset = 0;

    tvp_capture_mdma_init_status_dbg = HAL_MDMA_Init(&hmdma_tvp_capture);
    if (tvp_capture_mdma_init_status_dbg == HAL_OK)
    {
        hmdma_tvp_capture.XferCpltCallback = tvp_capture_mdma_cplt;
        hmdma_tvp_capture.XferErrorCallback = tvp_capture_mdma_error;
        HAL_NVIC_SetPriority(MDMA_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(MDMA_IRQn);
    }

    return (HAL_StatusTypeDef)tvp_capture_mdma_init_status_dbg;
}

static void tvp_capture_start_mdma_for_line(uint32_t *line_buf)
{
    uint32_t field_height = tvp_capture_active_field_height();
    uint32_t fallback_limit = field_height + (field_height / 4U);
    uint32_t line_index = tvp_capture_line_write_index;
    uint32_t raw_line_bytes = tvp_capture_current_line_bytes();
    uint32_t raw_line_words = raw_line_bytes / 4U;
    uint32_t segment_lines = tvp_capture_dma_segment_lines_active;
    uint32_t copy_lines = segment_lines;
    uint32_t segment_bytes;
    uint32_t segment_words;
    uint32_t *frame_buf = tvp_capture_buffer(tvp_capture_active_buf_dbg);
    uint8_t *dst = ((uint8_t *)frame_buf) + (line_index * raw_line_bytes);

    if (line_index >= field_height)
    {
        tvp_capture_line_overrun_count_dbg++;
        if ((hdcmi.Init.SynchroMode == DCMI_SYNCHRO_EMBEDDED) &&
            (tvp_capture_vsync_resync_enable_dbg == 0U ||
             tvp_capture_line_write_index >= fallback_limit))
        {
            tvp_capture_synthetic_frame_count_dbg++;
            tvp_capture_finish_line_frame(tvp_capture_line_write_index);
        }
        return;
    }

    if ((line_index + copy_lines) > field_height)
    {
        copy_lines = field_height - line_index;
    }
    segment_bytes = raw_line_bytes * copy_lines;
    segment_words = raw_line_words * segment_lines;

    tvp_capture_line_write_index = line_index + copy_lines;
    tvp_capture_line_write_index_dbg = tvp_capture_line_write_index;
    tvp_capture_dma_words_done_dbg = segment_words;
    tvp_capture_dma_segment_bytes_dbg = segment_bytes;

    if (tvp_capture_mdma_line_pending != 0U)
    {
        tvp_capture_mdma_busy_count_dbg++;
        tvp_capture_line_overrun_count_dbg += copy_lines;
        return;
    }

    tvp_capture_mdma_line_pending = 1U;
    tvp_capture_mdma_start_status_dbg =
        HAL_MDMA_Start_IT(&hmdma_tvp_capture,
                          (uint32_t)line_buf,
                          (uint32_t)dst,
                          segment_bytes,
                          1U);

    if (tvp_capture_mdma_start_status_dbg != HAL_OK)
    {
        tvp_capture_mdma_line_pending = 0U;
        tvp_capture_mdma_busy_count_dbg++;
        tvp_capture_mdma_error_dbg = hmdma_tvp_capture.ErrorCode;
    }

    if ((hdcmi.Init.SynchroMode == DCMI_SYNCHRO_EMBEDDED) &&
        (tvp_capture_vsync_resync_enable_dbg == 0U) &&
        (tvp_capture_line_write_index >= field_height))
    {
        tvp_capture_synthetic_frame_count_dbg++;
        tvp_capture_finish_line_frame(tvp_capture_line_write_index);
    }
}

static void tvp_capture_dma_line0_cplt(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    tvp_capture_dma_line0_count_dbg++;
    tvp_capture_start_mdma_for_line(tvp5150_line_buf0);
}

static void tvp_capture_dma_line1_cplt(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    tvp_capture_dma_line1_count_dbg++;
    tvp_capture_start_mdma_for_line(tvp5150_line_buf1);
}

static void tvp_capture_dma_error(DMA_HandleTypeDef *hdma)
{
    DCMI_HandleTypeDef *dcmi = (DCMI_HandleTypeDef *)hdma->Parent;

    if (dcmi != NULL)
    {
        dcmi->ErrorCode |= HAL_DCMI_ERROR_DMA;
        dcmi->State = HAL_DCMI_STATE_ERROR;
    }

    tvp_capture_error_count_dbg++;
    tvp_capture_sample_regs();
}

static void tvp_capture_mdma_cplt(MDMA_HandleTypeDef *hmdma)
{
    __HAL_MDMA_DISABLE(hmdma);
    tvp_capture_mdma_line_pending = 0U;
    tvp_capture_mdma_complete_count_dbg++;
}

static void tvp_capture_mdma_error(MDMA_HandleTypeDef *hmdma)
{
    __HAL_MDMA_DISABLE(hmdma);
    tvp_capture_mdma_line_pending = 0U;
    tvp_capture_mdma_error_count_dbg++;
    tvp_capture_mdma_error_dbg = hmdma->ErrorCode;
    tvp_capture_sample_regs();
}

static HAL_StatusTypeDef tvp_capture_start_line_mdma(uint32_t index)
{
    uint32_t *buf = tvp_capture_buffer(index);
    uint32_t field_height = tvp_capture_active_field_height();
    uint32_t raw_line_bytes = tvp_capture_raw_line_bytes();
    uint32_t raw_line_words = raw_line_bytes / 4U;
    uint32_t segment_lines = tvp_capture_active_segment_lines();
    uint32_t segment_words = raw_line_words * segment_lines;
    HAL_StatusTypeDef status;

    tvp_capture_dma_segment_lines_active = segment_lines;
    tvp_capture_dma_segment_lines_dbg = segment_lines;

    tvp_capture_active_buf_dbg = index;
    tvp_capture_ready_buf_dbg = 0xFFFFFFFFU;
    tvp_capture_frame_pending_dbg = 0U;
    tvp_capture_line_write_index = 0U;
    tvp_capture_line_write_index_dbg = 0U;
    tvp_capture_mdma_line_pending = 0U;
    tvp_capture_dma_target_dbg = (uint32_t)buf;
    tvp_capture_dma_length_dbg = raw_line_words * field_height;
    tvp_capture_dma_segment_length_dbg = segment_words;
    tvp_capture_dma_segment_bytes_dbg = raw_line_bytes * segment_lines;
    tvp_capture_line_bytes_dbg = raw_line_bytes;
    tvp_capture_line_words_dbg = raw_line_words;
    tvp_capture_line_buf0_addr_dbg = (uint32_t)tvp5150_line_buf0;
    tvp_capture_line_buf1_addr_dbg = (uint32_t)tvp5150_line_buf1;
    tvp_capture_dcmi_mode_dbg = DCMI_MODE_CONTINUOUS;
    tvp_capture_restart_count_dbg++;

    if (tvp_capture_mdma_init() != HAL_OK)
    {
        tvp_capture_start_status_dbg = HAL_ERROR;
        return HAL_ERROR;
    }

    __HAL_LOCK(&hdcmi);
    hdcmi.State = HAL_DCMI_STATE_BUSY;
    hdcmi.ErrorCode = HAL_DCMI_ERROR_NONE;
    __HAL_DCMI_ENABLE(&hdcmi);
    hdcmi.Instance->CR &= ~(DCMI_CR_CM);
    hdcmi.Instance->CR |= DCMI_MODE_CONTINUOUS;

    hdcmi.DMA_Handle->XferCpltCallback = tvp_capture_dma_line0_cplt;
    hdcmi.DMA_Handle->XferM1CpltCallback = tvp_capture_dma_line1_cplt;
    hdcmi.DMA_Handle->XferHalfCpltCallback = NULL;
    hdcmi.DMA_Handle->XferM1HalfCpltCallback = NULL;
    hdcmi.DMA_Handle->XferErrorCallback = tvp_capture_dma_error;
    hdcmi.DMA_Handle->XferAbortCallback = tvp_capture_dma_error;
    hdcmi.XferCount = 0U;
    hdcmi.XferTransferNumber = field_height / segment_lines;
    hdcmi.XferSize = segment_words;
    hdcmi.pBuffPtr = (uint32_t)buf;

    __HAL_DCMI_CLEAR_FLAG(&hdcmi,
                          DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI |
                          DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI |
                          DCMI_FLAG_LINERI);

    status = HAL_DMAEx_MultiBufferStart_IT(hdcmi.DMA_Handle,
                                           (uint32_t)&hdcmi.Instance->DR,
                                           (uint32_t)tvp5150_line_buf0,
                                           (uint32_t)tvp5150_line_buf1,
                                           segment_words);

    if (status != HAL_OK)
    {
        hdcmi.ErrorCode = HAL_DCMI_ERROR_DMA;
        hdcmi.State = HAL_DCMI_STATE_READY;
        __HAL_UNLOCK(&hdcmi);
        tvp_capture_start_status_dbg = status;
        tvp_capture_sample_regs();
        return status;
    }

    __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_FRAME | DCMI_IT_VSYNC | DCMI_IT_ERR | DCMI_IT_OVR);
    hdcmi.Instance->CR |= DCMI_CR_CAPTURE;
    __HAL_UNLOCK(&hdcmi);

    tvp_capture_start_status_dbg = status;
    tvp_capture_sample_regs();
    return status;
}

static HAL_StatusTypeDef tvp_capture_start_snapshot(uint32_t index)
{
    uint32_t *buf = tvp_capture_buffer(index);
    uint32_t field_height = tvp_capture_active_field_height();
    uint32_t length =
        (TVP5150_CAPTURE_WIDTH * field_height * TVP5150_CAPTURE_BPP) / 4U;
    uint32_t mode = DCMI_MODE_SNAPSHOT;

    if ((tvp_capture_line_mdma_enable_dbg != 0U) &&
        (tvp_capture_crop_test_enable_dbg == 0U))
    {
        return tvp_capture_start_line_mdma(index);
    }

    if (tvp_capture_crop_test_enable_dbg != 0U)
    {
        length = TVP_CAPTURE_TEST_WORDS;
        if (tvp_capture_continuous_test_enable_dbg != 0U)
        {
            mode = DCMI_MODE_CONTINUOUS;
        }
        for (uint32_t i = 0U; i < TVP_CAPTURE_TEST_WORDS; i++)
        {
            tvp5150_ycbcr422_test_buf[i] = 0xA5A50000U | i;
        }
    }

    tvp_capture_active_buf_dbg = index;
    tvp_capture_restart_count_dbg++;
    tvp_capture_dma_target_dbg = (uint32_t)buf;
    tvp_capture_dma_length_dbg = length;
    tvp_capture_dcmi_mode_dbg = mode;
    tvp_capture_start_status_dbg =
        HAL_DCMI_Start_DMA(&hdcmi, mode, (uint32_t)buf, length);
    tvp_capture_sample_regs();
    return (HAL_StatusTypeDef)tvp_capture_start_status_dbg;
}

void TVP5150_Capture_Init(uint32_t tvp_i2c_bus)
{
    tvp_capture_i2c_bus_active = tvp_i2c_bus;
    tvp_capture_init_done_dbg = 0;
    tvp_capture_skip_reason_dbg = TVP_CAPTURE_SKIP_NONE;
    tvp_capture_start_status_dbg = 0xFFFFFFFFU;
    tvp_capture_frame_count_dbg = 0;
    tvp_capture_restart_count_dbg = 0;
    tvp_capture_error_count_dbg = 0;
    tvp_capture_line_count_dbg = 0;
    tvp_capture_vsync_count_dbg = 0;
    tvp_capture_ready_buf_dbg = 0xFFFFFFFFU;
    tvp_capture_frame_pending_dbg = 0;
    tvp_capture_dma_target_dbg = 0;
    tvp_capture_dma_length_dbg = 0;
    tvp_capture_dcmi_mode_dbg = 0;
    tvp_capture_dma_segment_length_dbg = 0;
    tvp_capture_dma_words_done_dbg = 0;
    tvp_capture_live_sample_count_dbg = 0;
    tvp_capture_crop_config_status_dbg = 0xFFFFFFFFU;
    tvp_capture_crop_enable_status_dbg = 0xFFFFFFFFU;
    tvp_capture_dcmi_cwstrtr_dbg = 0;
    tvp_capture_dcmi_cwsizer_dbg = 0;
    tvp_capture_dma_fcr_dbg = 0;
    tvp_capture_dma_par_dbg = 0;
    tvp_capture_dma_m0ar_dbg = 0;
    tvp_capture_dma_m1ar_dbg = 0;
    tvp_capture_dma_error_dbg = 0;
    tvp_capture_dcmi_error_dbg = 0;
    tvp_capture_xfer_size_dbg = 0;
    tvp_capture_xfer_count_dbg = 0;
    tvp_capture_xfer_transfer_number_dbg = 0;
    tvp_capture_pbuff_ptr_dbg = 0;
    tvp_capture_pin_sample_count_dbg = 0;
    tvp_capture_pin_sample_tick_dbg = 0;
    tvp_capture_pin_levels_dbg = 0;
    tvp_capture_pclk_edges_dbg = 0;
    tvp_capture_hsync_edges_dbg = 0;
    tvp_capture_vsync_edges_dbg = 0;
    tvp_capture_data_or_dbg = 0;
    tvp_capture_data_and_dbg = 0;
    tvp_capture_data_change_mask_dbg = 0;
    tvp_capture_line_bytes_dbg = 0;
    tvp_capture_line_words_dbg = 0;
    tvp_capture_dma_segment_bytes_dbg = 0;
    tvp_capture_line_write_index_dbg = 0;
    tvp_capture_dma_line0_count_dbg = 0;
    tvp_capture_dma_line1_count_dbg = 0;
    tvp_capture_last_frame_line_count_dbg = 0;
    tvp_capture_min_frame_line_count_dbg = 0xFFFFFFFFU;
    tvp_capture_max_frame_line_count_dbg = 0;
    tvp_capture_short_frame_count_dbg = 0;
    tvp_capture_short_frame_drop_count_dbg = 0;
    tvp_capture_short_frame_last_lines_dbg = 0;
    tvp_capture_mdma_init_status_dbg = 0xFFFFFFFFU;
    tvp_capture_mdma_start_status_dbg = 0xFFFFFFFFU;
    tvp_capture_mdma_complete_count_dbg = 0;
    tvp_capture_mdma_busy_count_dbg = 0;
    tvp_capture_mdma_error_count_dbg = 0;
    tvp_capture_mdma_error_dbg = 0;
    tvp_capture_mdma_cisr_dbg = 0;
    tvp_capture_mdma_cesr_dbg = 0;
    tvp_capture_mdma_ccr_dbg = 0;
    tvp_capture_mdma_ctcr_dbg = 0;
    tvp_capture_mdma_cbndtr_dbg = 0;
    tvp_capture_mdma_csar_dbg = 0;
    tvp_capture_mdma_cdar_dbg = 0;
    tvp_capture_line_buf0_addr_dbg = 0;
    tvp_capture_line_buf1_addr_dbg = 0;
    tvp_capture_vsync_line_count_dbg = 0;
    tvp_capture_vsync_resync_count_dbg = 0;
    tvp_capture_vsync_drop_count_dbg = 0;
    tvp_capture_line_overrun_count_dbg = 0;
    tvp_capture_synthetic_frame_count_dbg = 0;
    tvp_capture_field_sequence_dbg = 0;
    tvp_capture_field_publish_count_dbg = 0;
    tvp_capture_field_phase_drop_count_dbg = 0;
    tvp_capture_render_count_dbg = 0;
    tvp_capture_render_skip_period_dbg = 0;
    tvp_capture_render_skip_no_target_dbg = 0;
    tvp_capture_render_source_buf_dbg = 0xFFFFFFFFU;
    tvp_capture_render_last_tick_dbg = 0;
    tvp_capture_render_last_ms_dbg = 0;
    tvp_capture_render_max_ms_dbg = 0;
    tvp_capture_render_checksum_dbg = 0;
    tvp_capture_render_dma2d_status_dbg = 0xFFFFFFFFU;
    tvp_capture_render_dma2d_error_dbg = 0;
    tvp_capture_render_dma2d_count_dbg = 0;
    tvp_capture_render_linear_dma2d_enable_dbg = 0;
    tvp_capture_render_fast_status_dbg = 0xFFFFFFFFU;
    tvp_capture_render_fast_count_dbg = 0;
    tvp_capture_render_fast_build_ms_dbg = 0;
    tvp_capture_render_cpu_count_dbg = 0;
    tvp_capture_render_locked_buf_dbg = 0xFFFFFFFFU;
    tvp_capture_render_lock_drop_count_dbg = 0;
    tvp_capture_ready_overwrite_count_dbg = 0;
    tvp_capture_last_pin_sample_tick = 0;
    tvp_capture_line_write_index = 0;
    tvp_capture_mdma_line_pending = 0;

    if (tvp_capture_enable_dbg == 0U)
    {
        tvp_capture_skip_reason_dbg = TVP_CAPTURE_SKIP_DISABLED;
        return;
    }

    if (tvp5150_probe_ok_dbg == 0U)
    {
        tvp_capture_skip_reason_dbg = TVP_CAPTURE_SKIP_NO_TVP;
        return;
    }

    if ((tvp_i2c_bus != 2U) && (tvp_i2c_bus != 4U) && (tvp_capture_allow_i2c1_conflict_dbg == 0U))
    {
        tvp_capture_skip_reason_dbg = TVP_CAPTURE_SKIP_I2C_BUS;
        return;
    }

    MX_DCMI_Init();
    if (tvp_capture_crop_test_enable_dbg != 0U)
    {
        tvp_capture_crop_config_status_dbg =
            HAL_DCMI_ConfigCrop(&hdcmi, 0U, 0U, TVP_CAPTURE_TEST_XSIZE, TVP_CAPTURE_TEST_YSIZE);
        tvp_capture_crop_enable_status_dbg = HAL_DCMI_EnableCrop(&hdcmi);
        __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_LINE);
    }
    else if (tvp_capture_hw_crop_enable_dbg != 0U)
    {
        uint32_t crop_x = tvp_capture_active_hw_crop_x();
        uint32_t crop_y = tvp_capture_active_hw_crop_y();
        uint32_t crop_w = tvp_capture_active_hw_crop_width();
        uint32_t crop_h = tvp_capture_active_hw_crop_height();
        uint32_t crop_x_bytes = crop_x * TVP5150_CAPTURE_BPP;
        uint32_t crop_w_bytes = crop_w * TVP5150_CAPTURE_BPP;

        tvp_capture_hw_crop_src_x_dbg = crop_x;
        tvp_capture_hw_crop_src_y_dbg = crop_y;
        tvp_capture_hw_crop_width_dbg = crop_w;
        tvp_capture_hw_crop_height_dbg = crop_h;
        tvp_capture_raw_width_dbg = crop_w;
        if (tvp_capture_render_src_x_dbg == TVP_CAPTURE_RENDER_SRC_X_DEFAULT)
        {
            tvp_capture_render_src_x_dbg = 0U;
        }

        tvp_capture_crop_config_status_dbg =
            HAL_DCMI_ConfigCrop(&hdcmi,
                                crop_x_bytes,
                                crop_y,
                                crop_w_bytes - 1U,
                                crop_h - 1U);
        tvp_capture_crop_enable_status_dbg =
            (tvp_capture_crop_config_status_dbg == HAL_OK) ?
            HAL_DCMI_EnableCrop(&hdcmi) : tvp_capture_crop_config_status_dbg;
    }
    else
    {
        (void)HAL_DCMI_DisableCrop(&hdcmi);
        tvp_capture_crop_config_status_dbg = 0xFFFFFFFFU;
        tvp_capture_crop_enable_status_dbg = 0xFFFFFFFFU;
    }
    tvp_capture_init_done_dbg = 1U;
    (void)tvp_capture_start_snapshot(0U);
}

void TVP5150_Capture_SetRGB565Target(uint16_t *target, uint32_t width, uint32_t height)
{
    tvp_capture_rgb565_target = target;
    tvp_capture_rgb565_target_w = width;
    tvp_capture_rgb565_target_h = height;
}

void TVP5150_Capture_Poll(void)
{
    uint32_t ready;

    if (tvp_capture_init_done_dbg == 0U)
    {
        return;
    }

    if (tvp_capture_reinit_request_dbg != 0U)
    {
        tvp_capture_reinit_request_dbg = 0U;
        (void)HAL_DCMI_Stop(&hdcmi);
        tvp_capture_mdma_line_pending = 0U;
        TVP5150_Capture_Init(tvp_capture_i2c_bus_active);
        return;
    }

    tvp_capture_sample_regs();
    tvp_capture_sample_pins();

    if ((tvp_capture_crop_test_enable_dbg != 0U) &&
        (tvp_capture_dma_words_done_dbg != 0U))
    {
        tvp_capture_sample_buffer(tvp_capture_active_buf_dbg);
        tvp_capture_live_sample_count_dbg++;
    }

    if (tvp_capture_frame_pending_dbg == 0U)
    {
        return;
    }

    __disable_irq();
    ready = tvp_capture_ready_buf_dbg;
    tvp_capture_frame_pending_dbg = 0U;
    if (ready <= 1U)
    {
        tvp_capture_render_locked_buf_dbg = ready;
    }
    __enable_irq();

    if (ready > 1U)
    {
        return;
    }

    tvp_capture_sample_buffer(ready);
    tvp_capture_render_to_rgb565(ready);
    __disable_irq();
    if (tvp_capture_render_locked_buf_dbg == ready)
    {
        tvp_capture_render_locked_buf_dbg = 0xFFFFFFFFU;
    }
    __enable_irq();

    if (tvp_capture_line_mdma_enable_dbg == 0U)
    {
        (void)tvp_capture_start_snapshot(ready ^ 1U);
    }
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi_cb)
{
    if (hdcmi_cb->Instance == DCMI)
    {
        if ((tvp_capture_line_mdma_enable_dbg != 0U) &&
            (hdcmi_cb->Init.SynchroMode != DCMI_SYNCHRO_EMBEDDED))
        {
            tvp_capture_finish_line_frame(tvp_capture_line_write_index);
            __HAL_DCMI_ENABLE_IT(hdcmi_cb, DCMI_IT_FRAME);
        }
        else if (tvp_capture_line_mdma_enable_dbg == 0U)
        {
            tvp_capture_ready_buf_dbg = tvp_capture_active_buf_dbg;
            tvp_capture_frame_pending_dbg = 1U;
            tvp_capture_frame_count_dbg++;
        }
        tvp_capture_sample_regs();
    }
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi_cb)
{
    if (hdcmi_cb->Instance == DCMI)
    {
        tvp_capture_error_count_dbg++;
        tvp_capture_sample_regs();
        if (tvp_capture_dma_words_done_dbg != 0U)
        {
            tvp_capture_sample_buffer(tvp_capture_active_buf_dbg);
        }
    }
}

void HAL_DCMI_LineEventCallback(DCMI_HandleTypeDef *hdcmi_cb)
{
    if (hdcmi_cb->Instance == DCMI)
    {
        tvp_capture_line_count_dbg++;
    }
}

void HAL_DCMI_VsyncEventCallback(DCMI_HandleTypeDef *hdcmi_cb)
{
    if (hdcmi_cb->Instance == DCMI)
    {
        uint32_t line_count;
        uint32_t min_lines;

        tvp_capture_vsync_count_dbg++;
        if ((tvp_capture_line_mdma_enable_dbg == 0U) ||
            (hdcmi_cb->Init.SynchroMode != DCMI_SYNCHRO_EMBEDDED) ||
            (tvp_capture_vsync_resync_enable_dbg == 0U))
        {
            return;
        }

        line_count = tvp_capture_line_write_index;
        min_lines = (tvp_capture_active_field_height() * 3U) / 4U;
        tvp_capture_vsync_line_count_dbg = line_count;

        if (line_count >= min_lines)
        {
            tvp_capture_vsync_resync_count_dbg++;
            tvp_capture_finish_line_frame(line_count);
        }
        else if (line_count != 0U)
        {
            tvp_capture_vsync_drop_count_dbg++;
            tvp_capture_line_write_index = 0U;
            tvp_capture_line_write_index_dbg = 0U;
        }
    }
}
