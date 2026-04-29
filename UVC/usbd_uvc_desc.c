#include "usbd_uvc_desc.h"
#include "usbd_uvc.h"

uint8_t UVC_ConfigDesc[] __attribute__((aligned(32))) =
{
    /* ================= CONFIGURATION ================= */
    0x09, 0x02,
    0x98, 0x00,             /* wTotalLength = 152 */
    0x02,                   /* bNumInterfaces = 2 */
    0x01,                   /* bConfigurationValue */
    0x00,                   /* iConfiguration */
    0x80,                   /* bmAttributes */
    0xFA,                   /* bMaxPower */

    /* ================= IAD ================= */
    0x08, 0x0B,
    0x00,                   /* bFirstInterface */
    0x02,                   /* bInterfaceCount */
    0x0E,                   /* CC_VIDEO */
    0x03,                   /* SC_VIDEO_INTERFACE_COLLECTION */
    0x00,                   /* PC_PROTOCOL_UNDEFINED */
    0x00,                   /* iFunction */

    /* ================= VC INTERFACE ================= */
    0x09, 0x04,
    0x00,                   /* bInterfaceNumber = 0 */
    0x00,                   /* bAlternateSetting */
    0x00,                   /* bNumEndpoints */
    0x0E,                   /* CC_VIDEO */
    0x01,                   /* SC_VIDEOCONTROL */
    0x00,                   /* PC_PROTOCOL_UNDEFINED */
    0x00,                   /* iInterface */

    /* VC HEADER */
    0x0D, 0x24, 0x01,
    0x00, 0x01,             /* UVC 1.0, 26-byte PROBE/COMMIT */
    0x28, 0x00,             /* wTotalLength = 40 (CS VC only) */
    0x00, 0x6C, 0xDC, 0x02, /* dwClockFrequency = 48000000 */
    0x01,                   /* bInCollection */
    0x01,                   /* baInterfaceNr(1) = VS interface 1 */

    /* INPUT TERMINAL (Camera) */
    0x12, 0x24, 0x02,
    0x01,                   /* bTerminalID */
    0x01, 0x02,             /* wTerminalType = ITT_CAMERA */
    0x00,                   /* bAssocTerminal */
    0x00,                   /* iTerminal */
    0x00, 0x00,             /* wObjectiveFocalLengthMin */
    0x00, 0x00,             /* wObjectiveFocalLengthMax */
    0x00, 0x00,             /* wOcularFocalLength */
    0x03,                   /* bControlSize = 3 */
    0x00, 0x00, 0x00,       /* bmControls[3] = none */

    /* OUTPUT TERMINAL */
    0x09, 0x24, 0x03,
    0x02,                   /* bTerminalID */
    0x01, 0x01,             /* wTerminalType = TT_STREAMING */
    0x00,                   /* bAssocTerminal */
    0x01,                   /* bSourceID = Input Terminal 1 */
    0x00,                   /* iTerminal */

    /* ================= VS INTERFACE ALT 0 ================= */
    0x09, 0x04,
    0x01,                   /* bInterfaceNumber = 1 */
    0x00,                   /* bAlternateSetting = 0 */
    0x00,                   /* bNumEndpoints = 0 */
    0x0E,                   /* CC_VIDEO */
    0x02,                   /* SC_VIDEOSTREAMING */
    0x00,                   /* PC_PROTOCOL_UNDEFINED */
    0x00,                   /* iInterface */

    /* VS INPUT HEADER */
    0x0E, 0x24, 0x01,
    0x01,                   /* bNumFormats = 1 */
    0x3D, 0x00,             /* wTotalLength = 61 (VS header + format + frame + color) */
    UVC_IN_EP,              /* bEndpointAddress */
    0x00,                   /* bmInfo */
    0x02,                   /* bTerminalLink = Output Terminal 2 */
    0x00,                   /* bStillCaptureMethod */
    0x00,                   /* bTriggerSupport */
    0x00,                   /* bTriggerUsage */
    0x01,                   /* bControlSize */
    0x00,                   /* bmaControls(1) */

    /* MJPEG FORMAT */
    0x0B, 0x24, 0x06,
    0x01,                   /* bFormatIndex */
    0x01,                   /* bNumFrameDescriptors */
    0x00,                   /* bmFlags */
    0x01,                   /* bDefaultFrameIndex */
    0x00,                   /* bAspectRatioX */
    0x00,                   /* bAspectRatioY */
    0x00,                   /* bmInterlaceFlags */
    0x00,                   /* bCopyProtect */

    /* MJPEG FRAME */
    0x1E, 0x24, 0x07,
    0x01,                   /* bFrameIndex */
    0x00,                   /* bmCapabilities */
    (uint8_t)(UVC_FRAME_WIDTH & 0xFFU),
    (uint8_t)((UVC_FRAME_WIDTH >> 8) & 0xFFU),
                            /* wWidth */
    (uint8_t)(UVC_FRAME_HEIGHT & 0xFFU),
    (uint8_t)((UVC_FRAME_HEIGHT >> 8) & 0xFFU),
                            /* wHeight */
    (uint8_t)(UVC_FRAME_BITRATE & 0xFFU),
    (uint8_t)((UVC_FRAME_BITRATE >> 8) & 0xFFU),
    (uint8_t)((UVC_FRAME_BITRATE >> 16) & 0xFFU),
    (uint8_t)((UVC_FRAME_BITRATE >> 24) & 0xFFU),
                            /* dwMinBitRate */
    (uint8_t)(UVC_FRAME_BITRATE & 0xFFU),
    (uint8_t)((UVC_FRAME_BITRATE >> 8) & 0xFFU),
    (uint8_t)((UVC_FRAME_BITRATE >> 16) & 0xFFU),
    (uint8_t)((UVC_FRAME_BITRATE >> 24) & 0xFFU),
                            /* dwMaxBitRate */
    (uint8_t)(UVC_MAX_FRAME_SIZE & 0xFFU),
    (uint8_t)((UVC_MAX_FRAME_SIZE >> 8) & 0xFFU),
    (uint8_t)((UVC_MAX_FRAME_SIZE >> 16) & 0xFFU),
    (uint8_t)((UVC_MAX_FRAME_SIZE >> 24) & 0xFFU),
                            /* dwMaxVideoFrameBufferSize */
    (uint8_t)(UVC_FRAME_INTERVAL_100NS & 0xFFU),
    (uint8_t)((UVC_FRAME_INTERVAL_100NS >> 8) & 0xFFU),
    (uint8_t)((UVC_FRAME_INTERVAL_100NS >> 16) & 0xFFU),
    (uint8_t)((UVC_FRAME_INTERVAL_100NS >> 24) & 0xFFU),
                            /* dwDefaultFrameInterval */
    0x01,                   /* bFrameIntervalType */
    (uint8_t)(UVC_FRAME_INTERVAL_100NS & 0xFFU),
    (uint8_t)((UVC_FRAME_INTERVAL_100NS >> 8) & 0xFFU),
    (uint8_t)((UVC_FRAME_INTERVAL_100NS >> 16) & 0xFFU),
    (uint8_t)((UVC_FRAME_INTERVAL_100NS >> 24) & 0xFFU),
                            /* dwFrameInterval[1] */

    /* COLOR MATCHING */
    0x06, 0x24, 0x0D,
    0x01,                   /* bColorPrimaries */
    0x01,                   /* bTransferCharacteristics */
    0x04,                   /* bMatrixCoefficients */

    /* ================= VS INTERFACE ALT 1 ================= */
    0x09, 0x04,
    0x01,                   /* bInterfaceNumber = 1 */
    0x01,                   /* bAlternateSetting = 1 */
    0x01,                   /* bNumEndpoints = 1 */
    0x0E,                   /* CC_VIDEO */
    0x02,                   /* SC_VIDEOSTREAMING */
    0x00,                   /* PC_PROTOCOL_UNDEFINED */
    0x00,                   /* iInterface */

    /* VS ISO ENDPOINT */
    0x07, 0x05,
    UVC_IN_EP,              /* EP 0x81 IN */
    0x05,                   /* Isochronous, asynchronous */
    (uint8_t)(UVC_IN_PACKET & 0xFFU),
    (uint8_t)((UVC_IN_PACKET >> 8) & 0xFFU),
                            /* wMaxPacketSize = UVC_IN_PACKET */
    UVC_HS_EP_INTERVAL      /* bInterval = every high-speed microframe */
};

const uint16_t UVC_ConfigDescSize = sizeof(UVC_ConfigDesc);

volatile uint32_t uvc_desc_size_dbg = 152U;
volatile uint32_t uvc_desc_wtotal_dbg = 152U;
volatile uint32_t uvc_desc_vc_total_dbg = 40U;
volatile uint32_t uvc_desc_vs_total_dbg = 61U;
volatile uint32_t uvc_desc_frame_width_dbg = UVC_FRAME_WIDTH;
volatile uint32_t uvc_desc_frame_height_dbg = UVC_FRAME_HEIGHT;
volatile uint32_t uvc_desc_frame_max_dbg = UVC_MAX_FRAME_SIZE;
volatile uint32_t uvc_desc_ep_attr_dbg = 0x05U;
volatile uint32_t uvc_desc_ep_mps_dbg = UVC_IN_PACKET;
volatile uint32_t uvc_desc_ep_interval_dbg = UVC_HS_EP_INTERVAL;

void UVC_DescDebugUpdate(void)
{
    uvc_desc_size_dbg = sizeof(UVC_ConfigDesc);
    uvc_desc_wtotal_dbg = ((uint32_t)UVC_ConfigDesc[3] << 8) | UVC_ConfigDesc[2];
    uvc_desc_vc_total_dbg = ((uint32_t)UVC_ConfigDesc[32] << 8) | UVC_ConfigDesc[31];
    uvc_desc_vs_total_dbg = ((uint32_t)UVC_ConfigDesc[80] << 8) | UVC_ConfigDesc[79];
    uvc_desc_frame_width_dbg = ((uint32_t)UVC_ConfigDesc[106] << 8) | UVC_ConfigDesc[105];
    uvc_desc_frame_height_dbg = ((uint32_t)UVC_ConfigDesc[108] << 8) | UVC_ConfigDesc[107];
    uvc_desc_frame_max_dbg =
        ((uint32_t)UVC_ConfigDesc[120] << 24) |
        ((uint32_t)UVC_ConfigDesc[119] << 16) |
        ((uint32_t)UVC_ConfigDesc[118] << 8) |
        UVC_ConfigDesc[117];
    uvc_desc_ep_attr_dbg = UVC_ConfigDesc[148];
    uvc_desc_ep_mps_dbg = ((uint32_t)UVC_ConfigDesc[150] << 8) | UVC_ConfigDesc[149];
    uvc_desc_ep_interval_dbg = UVC_ConfigDesc[151];
}

const uint8_t UVC_DeviceQualifierDesc[10] __attribute__((aligned(32))) =
{
    0x0A,                   /* bLength */
    0x06,                   /* bDescriptorType: Device Qualifier */
    0x00, 0x02,             /* bcdUSB */
    0xEF,                   /* bDeviceClass: Miscellaneous */
    0x02,                   /* bDeviceSubClass: Common Class */
    0x01,                   /* bDeviceProtocol: IAD */
    0x40,                   /* bMaxPacketSize0 */
    0x01,                   /* bNumConfigurations */
    0x00                    /* bReserved */
};
