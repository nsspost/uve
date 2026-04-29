#pragma once

#include <stdint.h>

extern const uint8_t UVC_DeviceQualifierDesc[10];
extern uint8_t UVC_ConfigDesc[];
extern const uint16_t UVC_ConfigDescSize;

void UVC_DescDebugUpdate(void);
