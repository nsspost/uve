#include "video_source.h"
#include "test_jpeg.h"
#include "usbd_uvc.h"

static video_frame_t current_frame;
static video_frame_t pending_frame;
static volatile uint8_t pending_valid = 0U;
static uint32_t max_frame_size = UVC_MAX_FRAME_SIZE;

/* debug */
volatile const uint8_t *dbg_current_frame_ptr = 0;
volatile const uint8_t *dbg_pending_frame_ptr = 0;
volatile uint32_t dbg_current_frame_size = 0;
volatile uint32_t dbg_pending_frame_size = 0;
volatile uint32_t dbg_commit_calls = 0;
volatile uint32_t dbg_pending_valid_before = 0;
volatile uint32_t dbg_pending_valid_after = 0;
volatile uint32_t dbg_prepare_next_calls = 0;
volatile uint32_t dbg_repeat_current_calls = 0;
volatile uint32_t dbg_submit_reject_bad_jpeg = 0;
volatile uint32_t dbg_submit_last_size = 0;
volatile uint32_t dbg_submit_head = 0;
volatile uint32_t dbg_submit_tail = 0;

static bool video_source_is_valid_mjpeg(const uint8_t *data, uint32_t size)
{
    if ((data == 0) || (size < 4U))
        return false;

    return ((data[0] == 0xFFU) &&
            (data[1] == 0xD8U) &&
            (data[size - 2U] == 0xFFU) &&
            (data[size - 1U] == 0xD9U));
}

void video_source_init(void)
{
    current_frame.data = jpeg_frame_1;
    current_frame.size = jpeg_frame_1_size;

    pending_frame.data = 0;
    pending_frame.size = 0;
    pending_valid = 0U;

    dbg_current_frame_ptr = current_frame.data;
    dbg_current_frame_size = current_frame.size;
    dbg_pending_frame_ptr = 0;
    dbg_pending_frame_size = 0;
    dbg_commit_calls = 0;
    dbg_pending_valid_before = 0;
    dbg_pending_valid_after = 0;
    dbg_prepare_next_calls = 0;
    dbg_repeat_current_calls = 0;
    dbg_submit_reject_bad_jpeg = 0;
    dbg_submit_last_size = 0;
    dbg_submit_head = 0;
    dbg_submit_tail = 0;
}

const video_frame_t *video_source_get_current_frame(void)
{
    dbg_current_frame_ptr = current_frame.data;
    dbg_current_frame_size = current_frame.size;
    return &current_frame;
}

bool video_source_can_accept_frame(void)
{
    return (pending_valid == 0U);
}

bool video_source_has_current_frame(void)
{
    return ((current_frame.data != 0) && (current_frame.size != 0U));
}

bool video_source_buffer_in_use(const uint8_t *data)
{
    if (data == 0)
        return false;

    if (current_frame.data == data)
        return true;

    return (pending_valid != 0U && pending_frame.data == data);
}

bool video_source_submit_frame(const uint8_t *data, uint32_t size)
{
    if (data == 0 || size == 0U || size > max_frame_size)
        return false;

    dbg_submit_last_size = size;
    if (size >= 4U)
    {
        dbg_submit_head =
            ((uint32_t)data[0] << 24) |
            ((uint32_t)data[1] << 16) |
            ((uint32_t)data[2] << 8) |
            data[3];
        dbg_submit_tail =
            ((uint32_t)data[size - 4U] << 24) |
            ((uint32_t)data[size - 3U] << 16) |
            ((uint32_t)data[size - 2U] << 8) |
            data[size - 1U];
    }
    else
    {
        dbg_submit_head = 0U;
        dbg_submit_tail = 0U;
    }

    if (!video_source_is_valid_mjpeg(data, size))
    {
        dbg_submit_reject_bad_jpeg++;
        return false;
    }

    pending_frame.data = data;
    pending_frame.size = size;
    pending_valid = 1U;

    dbg_pending_frame_ptr = data;
    dbg_pending_frame_size = size;

    return true;
}

bool video_source_commit_pending_if_any(void)
{
    bool committed = false;

    dbg_pending_valid_before = pending_valid;

    if (pending_valid != 0U)
    {
        current_frame = pending_frame;
        pending_valid = 0U;
        committed = true;

        dbg_current_frame_ptr = current_frame.data;
        dbg_current_frame_size = current_frame.size;
        dbg_commit_calls++;
    }

    dbg_pending_valid_after = pending_valid;
    return committed;
}

bool video_source_prepare_next_frame(bool *repeated)
{
    dbg_prepare_next_calls++;

    if (repeated != 0)
        *repeated = false;

    if (video_source_commit_pending_if_any())
        return true;

    if (video_source_has_current_frame())
    {
        dbg_repeat_current_calls++;
        if (repeated != 0)
            *repeated = true;
        return true;
    }

    return false;
}

uint32_t video_source_get_max_frame_size(void)
{
    return max_frame_size;
}
