/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    dcmi.h
  * @brief   This file contains all the function prototypes for
  *          the dcmi.c file
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __DCMI_H__
#define __DCMI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef hdma_dcmi;

extern volatile uint32_t dcmi_synchro_mode_dbg;
extern volatile uint32_t dcmi_pclk_polarity_dbg;
extern volatile uint32_t dcmi_vsync_polarity_dbg;
extern volatile uint32_t dcmi_hsync_polarity_dbg;
extern volatile uint32_t dcmi_init_status_dbg;
extern volatile uint32_t dcmi_sync_unmask_status_dbg;
extern volatile uint32_t dcmi_escr_dbg;
extern volatile uint32_t dcmi_esur_dbg;

void MX_DCMI_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __DCMI_H__ */
