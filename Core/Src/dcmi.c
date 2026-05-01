/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    dcmi.c
  * @brief   This file provides code for the configuration
  *          of the DCMI instance.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "dcmi.h"

DCMI_HandleTypeDef hdcmi;
DMA_HandleTypeDef hdma_dcmi;

volatile uint32_t dcmi_synchro_mode_dbg = DCMI_SYNCHRO_HARDWARE;
volatile uint32_t dcmi_pclk_polarity_dbg = DCMI_PCKPOLARITY_RISING;
volatile uint32_t dcmi_vsync_polarity_dbg = DCMI_VSPOLARITY_HIGH;
volatile uint32_t dcmi_hsync_polarity_dbg = DCMI_HSPOLARITY_HIGH;
volatile uint32_t dcmi_init_status_dbg = 0xFFFFFFFFU;
volatile uint32_t dcmi_sync_unmask_status_dbg = 0xFFFFFFFFU;
volatile uint32_t dcmi_escr_dbg = 0;
volatile uint32_t dcmi_esur_dbg = 0;

void MX_DCMI_Init(void)
{
  DCMI_SyncUnmaskTypeDef SyncUnmask = {0};

  hdcmi.Instance = DCMI;
  hdcmi.Init.SynchroMode = dcmi_synchro_mode_dbg;
  hdcmi.Init.PCKPolarity = dcmi_pclk_polarity_dbg;
  hdcmi.Init.VSPolarity = dcmi_vsync_polarity_dbg;
  hdcmi.Init.HSPolarity = dcmi_hsync_polarity_dbg;
  hdcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
  hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
  hdcmi.Init.SyncroCode.FrameStartCode = 0xAB;
  hdcmi.Init.SyncroCode.LineStartCode = 0x80;
  hdcmi.Init.SyncroCode.LineEndCode = 0x9D;
  hdcmi.Init.SyncroCode.FrameEndCode = 0xB6;
  hdcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
  hdcmi.Init.ByteSelectMode = DCMI_BSM_ALL;
  hdcmi.Init.ByteSelectStart = DCMI_OEBS_ODD;
  hdcmi.Init.LineSelectMode = DCMI_LSM_ALL;
  hdcmi.Init.LineSelectStart = DCMI_OELS_ODD;

  dcmi_init_status_dbg = HAL_DCMI_Init(&hdcmi);
  if (dcmi_init_status_dbg != HAL_OK)
  {
    Error_Handler();
  }

  if (hdcmi.Init.SynchroMode == DCMI_SYNCHRO_EMBEDDED)
  {
    SyncUnmask.FrameStartUnmask = 0xB0;
    SyncUnmask.LineStartUnmask = 0xB0;
    SyncUnmask.LineEndUnmask = 0xB0;
    SyncUnmask.FrameEndUnmask = 0xB0;
    dcmi_sync_unmask_status_dbg = HAL_DCMI_ConfigSyncUnmask(&hdcmi, &SyncUnmask);
  }
  else
  {
    dcmi_sync_unmask_status_dbg = 0xFFFFFFFFU;
  }

  dcmi_escr_dbg = hdcmi.Instance->ESCR;
  dcmi_esur_dbg = hdcmi.Instance->ESUR;

  __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_LINE);
}

void HAL_DCMI_MspInit(DCMI_HandleTypeDef* dcmiHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if(dcmiHandle->Instance==DCMI)
  {
    __HAL_RCC_DCMI_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /**DCMI GPIO Configuration
    PA4     ------> DCMI_HSYNC
    PA6     ------> DCMI_PIXCLK
    PB8     ------> DCMI_D6
    PB9     ------> DCMI_D7
    PC6     ------> DCMI_D0
    PC7     ------> DCMI_D1
    PD3     ------> DCMI_D5
    PG9     ------> DCMI_VSYNC
    PH11    ------> DCMI_D2
    PH12    ------> DCMI_D3
    PH14    ------> DCMI_D4
    */
    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_14;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMI;
    HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

    hdma_dcmi.Instance = DMA1_Stream0;
    hdma_dcmi.Init.Request = DMA_REQUEST_DCMI;
    hdma_dcmi.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_dcmi.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_dcmi.Init.MemInc = DMA_MINC_ENABLE;
    hdma_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_dcmi.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_dcmi.Init.Mode = DMA_CIRCULAR;
    hdma_dcmi.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_dcmi.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma_dcmi.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma_dcmi.Init.MemBurst = DMA_MBURST_INC4;
    hdma_dcmi.Init.PeriphBurst = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&hdma_dcmi) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(dcmiHandle,DMA_Handle,hdma_dcmi);

    HAL_NVIC_SetPriority(DCMI_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DCMI_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  }
}

void HAL_DCMI_MspDeInit(DCMI_HandleTypeDef* dcmiHandle)
{
  if(dcmiHandle->Instance==DCMI)
  {
    __HAL_RCC_DCMI_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4|GPIO_PIN_6);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8|GPIO_PIN_9);
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_6|GPIO_PIN_7);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_3);
    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_9);
    HAL_GPIO_DeInit(GPIOH, GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_14);

    HAL_DMA_DeInit(dcmiHandle->DMA_Handle);

    HAL_NVIC_DisableIRQ(DCMI_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
  }
}
