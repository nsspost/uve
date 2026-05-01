#include "video_source.h"
#include "test_jpeg.h"
#include "usbd_uvc.h"

static video_frame_t current_frame;
static video_frame_t pending_frame;
static volatile uint8_t pending_valid = 0U;
static uint32_t max_frame_size = UVC_MAX_FRAME_SIZE;

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
}

const video_frame_t *video_source_get_current_frame(void)
{
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

    if (!video_source_is_valid_mjpeg(data, size))
    {
        return false;
    }

    pending_frame.data = data;
    pending_frame.size = size;
    pending_valid = 1U;

    return true;
}

bool video_source_commit_pending_if_any(void)
{
    bool committed = false;

    if (pending_valid != 0U)
    {
        current_frame = pending_frame;
        pending_valid = 0U;
        committed = true;
    }

    return committed;
}

bool video_source_prepare_next_frame(bool *repeated)
{
    if (repeated != 0)
        *repeated = false;

    if (video_source_commit_pending_if_any())
        return true;

    if (video_source_has_current_frame())
    {
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
