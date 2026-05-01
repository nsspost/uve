#ifndef UX_USER_H
#define UX_USER_H

#define UX_STANDALONE
#define UX_DEVICE_STANDALONE
#define UX_DEVICE_SIDE_ONLY

#define UX_PERIODIC_RATE                         1000U
#define UX_MAX_SLAVE_CLASS_DRIVER                1U
#define UX_MAX_SLAVE_INTERFACES                  4U
#define UX_MAX_SLAVE_ENDPOINTS                   4U
#define UX_SLAVE_REQUEST_CONTROL_MAX_LENGTH      256U
/* Must cover the largest isochronous IN packet advertised by the UVC descriptor. */
#define UX_SLAVE_REQUEST_DATA_MAX_LENGTH         512U
#define UX_DEVICE_CLASS_VIDEO_MAX_STREAMS        1U
#define UX_DEVICE_CLASS_VIDEO_MAX_PAYLOAD_SIZE   512U

unsigned long _ux_utility_interrupt_disable(void);
void _ux_utility_interrupt_restore(unsigned long flags);
unsigned long _ux_utility_time_get(void);
unsigned long _ux_utility_time_elapsed(unsigned long start, unsigned long now);

#endif /* UX_USER_H */
