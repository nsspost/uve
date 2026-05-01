#include "jpeg_live.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_jpeg.h"
#include "jpeg_utils.h"
#include "usbd_uvc.h"

extern JPEG_HandleTypeDef hjpeg;

/* ===== Настройки кадра ===== */
#define JPEG_SRC_W          UVC_FRAME_WIDTH
#define JPEG_SRC_H          UVC_FRAME_HEIGHT
#define JPEG_BPP            2U   /* RGB565 */

#define MAX_INPUT_LINES     8U

#define CHUNK_SIZE_IN       (JPEG_SRC_W * MAX_INPUT_LINES * JPEG_BPP)
#define CHUNK_SIZE_OUT      4096U

#define JPEG_BUFFER_EMPTY   0U
#define JPEG_BUFFER_FULL    1U

typedef struct
{
    uint32_t State;
    uint8_t *DataBuffer;
    uint32_t DataBufferSize;
} JPEG_Data_BufferTypeDef;

/* ===== Входной path ===== */

static JPEG_RGBToYCbCr_Convert_Function pRGBToYCbCr_Convert_Function;

static uint8_t MCU_Data_InBuffer0[CHUNK_SIZE_IN] __attribute__((aligned(32)));
static uint8_t MCU_Data_InBuffer1[CHUNK_SIZE_IN] __attribute__((aligned(32)));

static JPEG_Data_BufferTypeDef Jpeg_IN_BufferTab = {JPEG_BUFFER_EMPTY, MCU_Data_InBuffer0, 0U};

static JPEG_ConfTypeDef Conf;

/* ===== Выходной path ===== */

static uint8_t JPEG_Data_OutBuffer0[CHUNK_SIZE_OUT] __attribute__((aligned(32)));
static uint8_t JPEG_Data_OutBuffer1[CHUNK_SIZE_OUT] __attribute__((aligned(32)));
static uint8_t *g_current_out_buf = JPEG_Data_OutBuffer0;

/* ===== Глобальное состояние encode ===== */

static const uint8_t *g_rgb_input = NULL;
static uint32_t g_rgb_input_size = 0U;
static uint32_t g_rgb_input_index = 0U;

static uint8_t *g_jpeg_output = NULL;
static uint32_t g_jpeg_output_capacity = 0U;
static uint32_t g_jpeg_output_size = 0U;

static uint32_t MCU_TotalNb = 0U;
static uint32_t MCU_BlockIndex = 0U;

static __IO uint32_t Jpeg_HWEncodingEnd = 0U;
static __IO uint32_t Input_Is_Paused = 0U;
static __IO uint32_t Jpeg_Error = 0U;

/* ===== Debug ===== */

volatile uint32_t jpeg_encode_calls = 0;
volatile uint32_t jpeg_encode_ok = 0;
volatile uint32_t jpeg_encode_fail = 0;
volatile uint32_t jpeg_live_quality = 20U;
volatile uint32_t jpeg_live_quality_applied = 0U;
volatile uint32_t jpeg_live_src_w_dbg = JPEG_SRC_W;
volatile uint32_t jpeg_live_src_h_dbg = JPEG_SRC_H;

volatile uint32_t jpeg_cb_getdata_calls = 0;
volatile uint32_t jpeg_cb_dataready_calls = 0;
volatile uint32_t jpeg_cb_complete_calls = 0;
volatile uint32_t jpeg_cb_error_calls = 0;

volatile uint32_t jpeg_last_size = 0;
volatile uint8_t jpeg_soi_0 = 0;
volatile uint8_t jpeg_soi_1 = 0;
volatile uint8_t jpeg_eoi_0 = 0;
volatile uint8_t jpeg_eoi_1 = 0;

volatile HAL_StatusTypeDef jpeg_start_status = HAL_ERROR;

volatile uint32_t jpeg_dbg_mcu_total = 0;
volatile void *jpeg_dbg_conv_func = 0;

volatile uint32_t jpeg_dbg_first_input_size = 0;
volatile uint32_t jpeg_dbg_rgb_input_size = 0;
volatile uint32_t jpeg_dbg_rgb_input_index = 0;

volatile uint32_t jpeg_dbg_jpeg_irq_enabled = 0;
volatile uint32_t jpeg_dbg_jpeg_irq_pending = 0;
volatile uint32_t jpeg_dbg_mdma_irq_enabled = 0;
volatile uint32_t jpeg_dbg_mdma_irq_pending = 0;

volatile uint32_t jpeg_dbg_hjpeg_state = 0;
volatile uint32_t jpeg_dbg_hjpeg_error = 0;
volatile uint32_t jpeg_dbg_in_length = 0;
volatile uint32_t jpeg_dbg_out_length = 0;
volatile uint32_t jpeg_dbg_in_count = 0;
volatile uint32_t jpeg_dbg_out_count = 0;

volatile uint32_t jpeg_dbg_hdmain_state = 0;
volatile uint32_t jpeg_dbg_hdmaout_state = 0;

volatile uint32_t jpeg_dbg_sr = 0;
volatile uint32_t jpeg_dbg_cr = 0;

volatile uint32_t jpeg_fail_bad_args = 0;
volatile uint32_t jpeg_fail_first_input = 0;
volatile uint32_t jpeg_fail_start_dma = 0;
volatile uint32_t jpeg_fail_timeout = 0;
volatile uint32_t jpeg_fail_hw_error = 0;
volatile uint32_t jpeg_fail_bad_markers = 0;

volatile uint32_t jpeg_total_dataready_bytes = 0;
volatile uint32_t jpeg_last_dataready_len = 0;
volatile uint32_t jpeg_complete_out_count = 0;

volatile uint8_t jpeg_tail_b0 = 0;
volatile uint8_t jpeg_tail_b1 = 0;
volatile uint8_t jpeg_tail_b2 = 0;
volatile uint8_t jpeg_tail_b3 = 0;

volatile uint8_t jpeg_head_b0 = 0;
volatile uint8_t jpeg_head_b1 = 0;
volatile uint8_t jpeg_head_b2 = 0;
volatile uint8_t jpeg_head_b3 = 0;

volatile uint32_t jpeg_dbg_has_dqt = 0;
volatile uint32_t jpeg_dbg_has_sof0 = 0;
volatile uint32_t jpeg_dbg_has_dht = 0;
volatile uint32_t jpeg_dbg_has_sos = 0;
volatile uint32_t jpeg_dbg_sos_offset = 0;
volatile uint32_t jpeg_dht_inserted = 0;
volatile uint32_t jpeg_fail_dht_insert = 0;
volatile uint32_t jpeg_dbg_sof0_offset = 0;
volatile uint32_t jpeg_dbg_sof0_width = 0;
volatile uint32_t jpeg_dbg_sof0_height = 0;
volatile uint32_t jpeg_dbg_sof0_components = 0;
volatile uint32_t jpeg_dbg_sof0_c1_sampling = 0;
volatile uint32_t jpeg_dbg_sof0_c2_sampling = 0;
volatile uint32_t jpeg_dbg_sof0_c3_sampling = 0;

/* ===== Внутренние функции ===== */

static void jpeg_reset_state(void)
{
    MCU_TotalNb = 0U;
    MCU_BlockIndex = 0U;

    Jpeg_HWEncodingEnd = 0U;
    Input_Is_Paused = 0U;
    Jpeg_Error = 0U;

    Jpeg_IN_BufferTab.State = JPEG_BUFFER_EMPTY;
    Jpeg_IN_BufferTab.DataBuffer = MCU_Data_InBuffer0;
    Jpeg_IN_BufferTab.DataBufferSize = 0U;

    g_current_out_buf = JPEG_Data_OutBuffer0;

    g_rgb_input = NULL;
    g_rgb_input_size = 0U;
    g_rgb_input_index = 0U;

    g_jpeg_output = NULL;
    g_jpeg_output_capacity = 0U;
    g_jpeg_output_size = 0U;

    jpeg_total_dataready_bytes = 0;
    jpeg_last_dataready_len = 0;
    jpeg_complete_out_count = 0;

    jpeg_tail_b0 = 0;
    jpeg_tail_b1 = 0;
    jpeg_tail_b2 = 0;
    jpeg_tail_b3 = 0;

    jpeg_head_b0 = 0;
    jpeg_head_b1 = 0;
    jpeg_head_b2 = 0;
    jpeg_head_b3 = 0;
}

static void jpeg_cache_clean(const void *addr, uint32_t len)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) == 0U || addr == NULL || len == 0U)
        return;

    uintptr_t start = (uintptr_t)addr & ~(uintptr_t)31U;
    uintptr_t end = ((uintptr_t)addr + len + 31U) & ~(uintptr_t)31U;

    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static void jpeg_cache_invalidate(const void *addr, uint32_t len)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) == 0U || addr == NULL || len == 0U)
        return;

    uintptr_t start = (uintptr_t)addr & ~(uintptr_t)31U;
    uintptr_t end = ((uintptr_t)addr + len + 31U) & ~(uintptr_t)31U;

    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static bool jpeg_copy_out_chunk(const uint8_t *src, uint32_t len)
{
    if (g_jpeg_output == NULL)
        return false;

    if ((g_jpeg_output_size + len) > g_jpeg_output_capacity)
        return false;

    memcpy(&g_jpeg_output[g_jpeg_output_size], src, len);
    g_jpeg_output_size += len;
    return true;
}

static bool jpeg_trim_to_markers(uint8_t *buf, uint32_t *size)
{
    uint32_t soi = 0xFFFFFFFFU;
    uint32_t eoi = 0xFFFFFFFFU;

    if (buf == NULL || size == NULL || *size < 4U)
        return false;

    for (uint32_t i = 0; i + 1U < *size; i++)
    {
        if (buf[i] == 0xFFU && buf[i + 1U] == 0xD8U)
        {
            soi = i;
            break;
        }
    }

    if (soi == 0xFFFFFFFFU)
        return false;

    for (uint32_t i = *size - 2U; i > soi; i--)
    {
        if (buf[i] == 0xFFU && buf[i + 1U] == 0xD9U)
        {
            eoi = i + 2U;
            break;
        }
    }

    if (eoi == 0xFFFFFFFFU || eoi <= soi)
        return false;

    if (soi != 0U)
    {
        memmove(buf, &buf[soi], eoi - soi);
    }

    *size = eoi - soi;
    return true;
}

static uint32_t jpeg_find_marker(const uint8_t *buf, uint32_t size, uint8_t marker)
{
    if (buf == NULL || size < 4U)
        return 0xFFFFFFFFU;

    for (uint32_t i = 0; i + 1U < size; i++)
    {
        if (buf[i] == 0xFFU && buf[i + 1U] == marker)
            return i;
    }

    return 0xFFFFFFFFU;
}

static bool jpeg_insert_standard_dht_if_missing(uint8_t *buf, uint32_t *size, uint32_t capacity)
{
    static const uint8_t standard_dht[] =
    {
        0xFF, 0xC4, 0x00, 0x1F, 0x00,
        0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0A, 0x0B,

        0xFF, 0xC4, 0x00, 0xB5, 0x10,
        0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04,
        0x04, 0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11,
        0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22,
        0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1,
        0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A,
        0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
        0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46,
        0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74,
        0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
        0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2,
        0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4,
        0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
        0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
        0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
        0xF9, 0xFA,

        0xFF, 0xC4, 0x00, 0x1F, 0x01,
        0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,

        0xFF, 0xC4, 0x00, 0xB5, 0x11,
        0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04,
        0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04,
        0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
        0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09,
        0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24,
        0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28,
        0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
        0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
        0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73,
        0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85,
        0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9,
        0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2,
        0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
        0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
        0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
        0xF9, 0xFA
    };
    uint32_t dqt = jpeg_find_marker(buf, *size, 0xDBU);
    uint32_t sof0 = jpeg_find_marker(buf, *size, 0xC0U);
    uint32_t dht = jpeg_find_marker(buf, *size, 0xC4U);
    uint32_t sos = jpeg_find_marker(buf, *size, 0xDAU);

    jpeg_dbg_has_dqt = (dqt != 0xFFFFFFFFU) ? 1U : 0U;
    jpeg_dbg_has_sof0 = (sof0 != 0xFFFFFFFFU) ? 1U : 0U;
    jpeg_dbg_has_dht = (dht != 0xFFFFFFFFU) ? 1U : 0U;
    jpeg_dbg_has_sos = (sos != 0xFFFFFFFFU) ? 1U : 0U;
    jpeg_dbg_sos_offset = (sos != 0xFFFFFFFFU) ? sos : 0U;

    jpeg_dbg_sof0_offset = (sof0 != 0xFFFFFFFFU) ? sof0 : 0U;
    jpeg_dbg_sof0_width = 0U;
    jpeg_dbg_sof0_height = 0U;
    jpeg_dbg_sof0_components = 0U;
    jpeg_dbg_sof0_c1_sampling = 0U;
    jpeg_dbg_sof0_c2_sampling = 0U;
    jpeg_dbg_sof0_c3_sampling = 0U;

    if (sof0 != 0xFFFFFFFFU && (sof0 + 18U) < *size)
    {
        jpeg_dbg_sof0_height = ((uint32_t)buf[sof0 + 5U] << 8) | buf[sof0 + 6U];
        jpeg_dbg_sof0_width = ((uint32_t)buf[sof0 + 7U] << 8) | buf[sof0 + 8U];
        jpeg_dbg_sof0_components = buf[sof0 + 9U];
        jpeg_dbg_sof0_c1_sampling = buf[sof0 + 11U];
        jpeg_dbg_sof0_c2_sampling = buf[sof0 + 14U];
        jpeg_dbg_sof0_c3_sampling = buf[sof0 + 17U];
    }

    if (dht != 0xFFFFFFFFU)
        return true;

    if (sos == 0xFFFFFFFFU || (*size + sizeof(standard_dht)) > capacity)
        return false;

    memmove(&buf[sos + sizeof(standard_dht)], &buf[sos], *size - sos);
    memcpy(&buf[sos], standard_dht, sizeof(standard_dht));
    *size += sizeof(standard_dht);

    jpeg_dbg_has_dht = 1U;
    jpeg_dbg_sos_offset = sos + sizeof(standard_dht);
    jpeg_dht_inserted++;

    return true;
}

static void jpeg_fill_conf(void)
{
    uint32_t quality = jpeg_live_quality;

    if (quality < 10U)
        quality = 10U;
    if (quality > 95U)
        quality = 95U;

    memset(&Conf, 0, sizeof(Conf));

    Conf.ImageWidth = JPEG_SRC_W;
    Conf.ImageHeight = JPEG_SRC_H;
    Conf.ColorSpace = JPEG_YCBCR_COLORSPACE;
    Conf.ChromaSubsampling = JPEG_422_SUBSAMPLING;
    Conf.ImageQuality = quality;
    jpeg_live_quality_applied = quality;
}

static bool jpeg_prepare_first_input_block(void)
{
    uint32_t DataBufferSize;

    jpeg_fill_conf();

    JPEG_GetEncodeColorConvertFunc(&Conf, &pRGBToYCbCr_Convert_Function, &MCU_TotalNb);

    jpeg_dbg_mcu_total = MCU_TotalNb;
    jpeg_dbg_conv_func = (void *)pRGBToYCbCr_Convert_Function;

    DataBufferSize = Conf.ImageWidth * MAX_INPUT_LINES * JPEG_BPP;
    jpeg_dbg_first_input_size = DataBufferSize;

    if (g_rgb_input_index >= g_rgb_input_size)
        return false;

    jpeg_dbg_rgb_input_size = g_rgb_input_size;
    jpeg_dbg_rgb_input_index = g_rgb_input_index;

    MCU_BlockIndex += pRGBToYCbCr_Convert_Function(
        (uint8_t *)(g_rgb_input + g_rgb_input_index),
        Jpeg_IN_BufferTab.DataBuffer,
        0U,
        DataBufferSize,
        (uint32_t *)&Jpeg_IN_BufferTab.DataBufferSize);

    Jpeg_IN_BufferTab.State = JPEG_BUFFER_FULL;
    g_rgb_input_index += DataBufferSize;
    jpeg_cache_clean(Jpeg_IN_BufferTab.DataBuffer, Jpeg_IN_BufferTab.DataBufferSize);

    return true;
}

static void JPEG_EncodeInputHandler(JPEG_HandleTypeDef *hjpeg_local)
{
    uint32_t DataBufferSize = Conf.ImageWidth * MAX_INPUT_LINES * JPEG_BPP;

    if ((Jpeg_IN_BufferTab.State == JPEG_BUFFER_EMPTY) && (MCU_BlockIndex < MCU_TotalNb))
    {
        if (g_rgb_input_index < g_rgb_input_size)
        {
            if (Jpeg_IN_BufferTab.DataBuffer == MCU_Data_InBuffer0)
                Jpeg_IN_BufferTab.DataBuffer = MCU_Data_InBuffer1;
            else
                Jpeg_IN_BufferTab.DataBuffer = MCU_Data_InBuffer0;

            MCU_BlockIndex += pRGBToYCbCr_Convert_Function(
                (uint8_t *)(g_rgb_input + g_rgb_input_index),
                Jpeg_IN_BufferTab.DataBuffer,
                0U,
                DataBufferSize,
                (uint32_t *)&Jpeg_IN_BufferTab.DataBufferSize);

            Jpeg_IN_BufferTab.State = JPEG_BUFFER_FULL;
            g_rgb_input_index += DataBufferSize;
            jpeg_cache_clean(Jpeg_IN_BufferTab.DataBuffer, Jpeg_IN_BufferTab.DataBufferSize);

            if (Input_Is_Paused == 1U)
            {
                Input_Is_Paused = 0U;
                HAL_JPEG_ConfigInputBuffer(
                    hjpeg_local,
                    Jpeg_IN_BufferTab.DataBuffer,
                    Jpeg_IN_BufferTab.DataBufferSize);
                HAL_JPEG_Resume(hjpeg_local, JPEG_PAUSE_RESUME_INPUT);
            }
        }
        else
        {
            /* докармливаем нулями до полного MCU */
            memset(Jpeg_IN_BufferTab.DataBuffer, 0, DataBufferSize);

            Jpeg_IN_BufferTab.DataBufferSize = DataBufferSize;
            Jpeg_IN_BufferTab.State = JPEG_BUFFER_FULL;
            jpeg_cache_clean(Jpeg_IN_BufferTab.DataBuffer, Jpeg_IN_BufferTab.DataBufferSize);

            if (Input_Is_Paused == 1U)
            {
                Input_Is_Paused = 0U;
                HAL_JPEG_ConfigInputBuffer(
                    hjpeg_local,
                    Jpeg_IN_BufferTab.DataBuffer,
                    Jpeg_IN_BufferTab.DataBufferSize);
                HAL_JPEG_Resume(hjpeg_local, JPEG_PAUSE_RESUME_INPUT);
            }

            MCU_BlockIndex++;
        }
    }
}

/* ===== Public API ===== */

bool jpeg_live_init(void)
{
    JPEG_InitColorTables();
    return true;
}

bool jpeg_live_encode_rgb565_160x120(
    const uint16_t *src_rgb565,
    uint8_t *dst_jpeg,
    uint32_t dst_capacity,
    uint32_t *dst_size)
{
    uint32_t timeout_start;

    jpeg_encode_calls++;

    if (src_rgb565 == NULL || dst_jpeg == NULL || dst_size == NULL)
    {
        jpeg_fail_bad_args++;
        jpeg_encode_fail++;
        return false;
    }

    jpeg_reset_state();

    g_rgb_input = (const uint8_t *)src_rgb565;
    g_rgb_input_size = JPEG_SRC_W * JPEG_SRC_H * JPEG_BPP;
    g_rgb_input_index = 0U;

    g_jpeg_output = dst_jpeg;
    g_jpeg_output_capacity = dst_capacity;
    g_jpeg_output_size = 0U;

    jpeg_dbg_rgb_input_size = g_rgb_input_size;
    jpeg_dbg_rgb_input_index = g_rgb_input_index;

    if (!jpeg_prepare_first_input_block())
    {
        jpeg_fail_first_input++;
        jpeg_encode_fail++;
        return false;
    }

    HAL_JPEG_ConfigEncoding(&hjpeg, &Conf);
    HAL_JPEG_ConfigOutputBuffer(&hjpeg, g_current_out_buf, CHUNK_SIZE_OUT);
    jpeg_start_status = HAL_JPEG_Encode_DMA(
        &hjpeg,
        Jpeg_IN_BufferTab.DataBuffer,
        Jpeg_IN_BufferTab.DataBufferSize,
        g_current_out_buf,
        CHUNK_SIZE_OUT);

    if (jpeg_start_status != HAL_OK)
    {
        jpeg_fail_start_dma++;
        jpeg_encode_fail++;
        return false;
    }

    timeout_start = HAL_GetTick();

    while (1)
    {
        JPEG_EncodeInputHandler(&hjpeg);

        jpeg_dbg_jpeg_irq_enabled = NVIC_GetEnableIRQ(JPEG_IRQn);
        jpeg_dbg_jpeg_irq_pending = NVIC_GetPendingIRQ(JPEG_IRQn);
        jpeg_dbg_mdma_irq_enabled = NVIC_GetEnableIRQ(MDMA_IRQn);
        jpeg_dbg_mdma_irq_pending = NVIC_GetPendingIRQ(MDMA_IRQn);

        jpeg_dbg_hjpeg_state = (uint32_t)hjpeg.State;
        jpeg_dbg_hjpeg_error = (uint32_t)hjpeg.ErrorCode;
        jpeg_dbg_in_length = hjpeg.InDataLength;
        jpeg_dbg_out_length = hjpeg.OutDataLength;
        jpeg_dbg_in_count = hjpeg.JpegInCount;
        jpeg_dbg_out_count = hjpeg.JpegOutCount;

        jpeg_dbg_hdmain_state = (uint32_t)hjpeg.hdmain->State;
        jpeg_dbg_hdmaout_state = (uint32_t)hjpeg.hdmaout->State;

        jpeg_dbg_sr = hjpeg.Instance->SR;
        jpeg_dbg_cr = hjpeg.Instance->CR;

        jpeg_dbg_rgb_input_index = g_rgb_input_index;

        if (Jpeg_Error != 0U)
            break;

        if (Jpeg_HWEncodingEnd != 0U)
        {
            uint32_t tail = hjpeg.JpegOutCount;

            if (tail > 0)
            {
                jpeg_cache_invalidate(g_current_out_buf, tail);
                jpeg_copy_out_chunk(g_current_out_buf, tail);
            }

            break;
        }

        if ((HAL_GetTick() - timeout_start) > 3000U)
        {
            HAL_JPEG_Abort(&hjpeg);
            Jpeg_Error = 1U;
            jpeg_fail_timeout++;
            break;
        }
    }

    if (Jpeg_Error != 0U || g_jpeg_output_size < 4U)
    {
        jpeg_fail_hw_error++;
        jpeg_encode_fail++;
        return false;
    }

    if (!jpeg_trim_to_markers(dst_jpeg, &g_jpeg_output_size))
    {
        if (g_jpeg_output_size >= 4U)
        {
            jpeg_head_b0 = dst_jpeg[0];
            jpeg_head_b1 = dst_jpeg[1];
            jpeg_head_b2 = dst_jpeg[2];
            jpeg_head_b3 = dst_jpeg[3];

            jpeg_tail_b0 = dst_jpeg[g_jpeg_output_size - 4U];
            jpeg_tail_b1 = dst_jpeg[g_jpeg_output_size - 3U];
            jpeg_tail_b2 = dst_jpeg[g_jpeg_output_size - 2U];
            jpeg_tail_b3 = dst_jpeg[g_jpeg_output_size - 1U];
        }
        jpeg_fail_bad_markers++;
        jpeg_encode_fail++;
        return false;
    }

    if (!jpeg_insert_standard_dht_if_missing(dst_jpeg, &g_jpeg_output_size, g_jpeg_output_capacity))
    {
        jpeg_fail_dht_insert++;
        jpeg_encode_fail++;
        return false;
    }

    *dst_size = g_jpeg_output_size;
    jpeg_last_size = g_jpeg_output_size;

    if (g_jpeg_output_size >= 4U)
    {
        jpeg_head_b0 = dst_jpeg[0];
        jpeg_head_b1 = dst_jpeg[1];
        jpeg_head_b2 = dst_jpeg[2];
        jpeg_head_b3 = dst_jpeg[3];

        jpeg_tail_b0 = dst_jpeg[g_jpeg_output_size - 4U];
        jpeg_tail_b1 = dst_jpeg[g_jpeg_output_size - 3U];
        jpeg_tail_b2 = dst_jpeg[g_jpeg_output_size - 2U];
        jpeg_tail_b3 = dst_jpeg[g_jpeg_output_size - 1U];
    }


    jpeg_soi_0 = dst_jpeg[0];
    jpeg_soi_1 = dst_jpeg[1];
    jpeg_eoi_0 = dst_jpeg[g_jpeg_output_size - 2U];
    jpeg_eoi_1 = dst_jpeg[g_jpeg_output_size - 1U];

    jpeg_encode_ok++;
    return true;
}

/* ===== HAL JPEG callbacks ===== */

void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg_local, uint32_t NbEncodedData)
{
    (void)hjpeg_local;
    jpeg_cb_getdata_calls++;

    jpeg_dbg_in_length = hjpeg.InDataLength;
    jpeg_dbg_in_count = hjpeg.JpegInCount;

    if (NbEncodedData == Jpeg_IN_BufferTab.DataBufferSize)
    {
        Jpeg_IN_BufferTab.State = JPEG_BUFFER_EMPTY;
        Jpeg_IN_BufferTab.DataBufferSize = 0U;

        HAL_JPEG_Pause(&hjpeg, JPEG_PAUSE_RESUME_INPUT);
        Input_Is_Paused = 1U;
    }
    else
    {
        HAL_JPEG_ConfigInputBuffer(
            &hjpeg,
            Jpeg_IN_BufferTab.DataBuffer + NbEncodedData,
            Jpeg_IN_BufferTab.DataBufferSize - NbEncodedData);
    }
}

void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hjpeg_local,
                                uint8_t *pDataOut,
                                uint32_t OutDataLength)
{
    jpeg_cb_dataready_calls++;
    jpeg_last_dataready_len = OutDataLength;
    jpeg_total_dataready_bytes += OutDataLength;

    if (OutDataLength == 0U)
        return;

    jpeg_cache_invalidate(pDataOut, OutDataLength);

    if (!jpeg_copy_out_chunk(pDataOut, OutDataLength))
    {
        Jpeg_Error = 1U;
        HAL_JPEG_Abort(hjpeg_local);
        return;
    }

    if (g_current_out_buf == JPEG_Data_OutBuffer0)
        g_current_out_buf = JPEG_Data_OutBuffer1;
    else
        g_current_out_buf = JPEG_Data_OutBuffer0;

    HAL_JPEG_ConfigOutputBuffer(hjpeg_local, g_current_out_buf, CHUNK_SIZE_OUT);
}

void HAL_JPEG_EncodeCpltCallback(JPEG_HandleTypeDef *hjpeg_local)
{
    (void)hjpeg_local;
    jpeg_cb_complete_calls++;
    jpeg_complete_out_count = hjpeg.JpegOutCount;
    Jpeg_HWEncodingEnd = 1U;
}

void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hjpeg_local)
{
    (void)hjpeg_local;

    jpeg_dbg_hjpeg_state = (uint32_t)hjpeg.State;
    jpeg_dbg_hjpeg_error = (uint32_t)hjpeg.ErrorCode;
    jpeg_dbg_sr = hjpeg.Instance->SR;
    jpeg_dbg_cr = hjpeg.Instance->CR;

    jpeg_cb_error_calls++;
    Jpeg_Error = 1U;
}
