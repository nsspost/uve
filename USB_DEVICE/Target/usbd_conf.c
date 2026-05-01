/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : Target/usbd_conf.c
  * @version        : v1.0_Cube
  * @brief          : This file implements the board support package for the USB device library
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "usbd_def.h"
#include "usbd_core.h"
#include "uvc_stream.h"
#include "usbd_uvc.h"
#include "usb_stack_select.h"
/* USER CODE BEGIN Includes */
#include <string.h>

/* USER CODE END Includes */

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O2")
#endif

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

PCD_HandleTypeDef hpcd_USB_OTG_HS;
void Error_Handler(void);

/* External functions --------------------------------------------------------*/

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/
USBD_StatusTypeDef USBD_Get_USB_Status(HAL_StatusTypeDef hal_status);

/* USER CODE END PFP */

/* Private functions ---------------------------------------------------------*/

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */

/*******************************************************************************
                       LL Driver Callbacks (PCD -> USB Device Library)
*******************************************************************************/
/* MSP Init */
volatile uint32_t usb_irq_calls = 0;
volatile uint32_t usb_msp_init_calls = 0;
volatile uint32_t usb_msp_init_hs_calls = 0;
volatile uint32_t usb_msp_clock_config_status = 0xFFFFFFFFU;
volatile uint32_t usb_msp_gpio_done = 0;
volatile uint32_t usb_msp_clk_enable_done = 0;
volatile uint32_t usb_msp_nvic_done = 0;
volatile uint32_t usb_ll_init_calls = 0;
volatile uint32_t usb_ll_init_hs_calls = 0;
volatile uint32_t usb_hal_pcd_init_status = 0xFFFFFFFFU;
volatile uint32_t usb_dma_enable_dbg = 0;
volatile uint32_t usb_reg_snapshot_enable = 0;
volatile uint32_t usb_ll_start_calls = 0;
volatile uint32_t usb_hal_pcd_start_status = 0xFFFFFFFFU;
volatile uint32_t usb_ll_start_usb_status = 0xFFFFFFFFU;
volatile uint32_t usb_reset_cb_calls = 0;
volatile uint32_t usb_reset_cb_speed = 0xFFFFFFFFU;
volatile uint32_t usb_suspend_cb_calls = 0;
volatile uint32_t usb_resume_cb_calls = 0;
volatile uint32_t usb_connect_cb_calls = 0;
volatile uint32_t usb_disconnect_cb_calls = 0;
volatile uint32_t usb_gccfg_after_start = 0;
volatile uint32_t usb_gintsts_after_start = 0;
volatile uint32_t usb_gintmsk_after_start = 0;
volatile uint32_t usb_gusbcfg_after_start = 0;
volatile uint32_t usb_grstctl_after_start = 0;
volatile uint32_t usb_dctl_after_start = 0;
volatile uint32_t usb_dsts_after_start = 0;
volatile uint32_t usb_pcgcctl_after_start = 0;
volatile uint32_t usb_pcgcctl_after_suspend = 0;
volatile uint32_t usb_suspend_phy_gate_skips = 0;
volatile uint32_t usb_gotgctl_after_start = 0;
volatile uint32_t usb_gahbcfg_after_start = 0;
volatile uint32_t usb_dcfg_after_start = 0;
volatile uint32_t usb_force_device_mode_before_start = 0;
volatile uint32_t usb_gintsts_before_start = 0;
volatile uint32_t usb_gotgctl_before_start = 0;
volatile uint32_t usb_dctl_before_start = 0;
volatile uint32_t usb_dsts_before_start = 0;
volatile uint32_t usb_attach_cycle_enable = 1;
volatile uint32_t usb_attach_cycle_delay_ms = 200;
volatile uint32_t usb_dctl_after_forced_disconnect = 0;
volatile uint32_t usb_dctl_after_forced_connect = 0;
volatile uint32_t usb_gintmsk_before_iisoixfr_mask = 0;
volatile uint32_t usb_gintmsk_after_iisoixfr_mask = 0;
volatile uint32_t usb_mask_iisoixfr_enable = 0;
volatile uint32_t usb_fifo_rx_words_dbg = 0;
volatile uint32_t usb_fifo_tx0_words_dbg = 0;
volatile uint32_t usb_fifo_tx1_words_dbg = 0;
volatile uint32_t usb_fifo_tx1_packet_slots_dbg = 0;
volatile uint32_t usb_fifo_total_words_dbg = 0;
volatile uint32_t usb_grxfsiz_dbg = 0;
volatile uint32_t usb_dieptxf0_dbg = 0;
volatile uint32_t usb_dieptxf1_dbg = 0;
volatile uint32_t usb_dthrctl_dbg = 0;
volatile uint32_t usb_dwt_cycle_enable = 1U;
volatile uint32_t usb_ll_transmit_cycles_last = 0U;
volatile uint32_t usb_ll_transmit_cycles_max = 0U;
static uint8_t usb_dma_ep0_tx_buf[USB_MAX_EP0_SIZE] __attribute__((aligned(32)));

static uint32_t USB_DWT_Begin(void)
{
  if (usb_dwt_cycle_enable == 0U)
  {
    return 0U;
  }

  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
  {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  }

  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
  {
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  }

  return DWT->CYCCNT;
}

static void USB_DWT_Record(volatile uint32_t *last, volatile uint32_t *max, uint32_t start)
{
  uint32_t elapsed;

  if ((usb_dwt_cycle_enable == 0U) ||
      ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U))
  {
    return;
  }

  elapsed = DWT->CYCCNT - start;
  *last = elapsed;
  if (elapsed > *max)
  {
    *max = elapsed;
  }
}

static void USB_MaskIISOIXFRIfEnabled(void)
{
  usb_gintmsk_before_iisoixfr_mask = USB_OTG_HS->GINTMSK;
  if (usb_mask_iisoixfr_enable != 0U)
  {
    USB_OTG_HS->GINTMSK &= ~USB_OTG_GINTMSK_IISOIXFRM;
    USB_OTG_HS->GINTSTS &= USB_OTG_GINTSTS_IISOIXFR;
  }
  usb_gintmsk_after_iisoixfr_mask = USB_OTG_HS->GINTMSK;
}
void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  RCC_CRSInitTypeDef CRSInitStruct = {0};
  HAL_StatusTypeDef clock_status;

  usb_msp_init_calls++;
  if(pcdHandle->Instance==USB_OTG_HS)
  {
    usb_msp_init_hs_calls++;
  /* USER CODE BEGIN USB_OTG_HS_MspInit 0 */

  /* USER CODE END USB_OTG_HS_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
    clock_status = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
    usb_msp_clock_config_status = (uint32_t)clock_status;
    if (clock_status != HAL_OK)
    {
      Error_Handler();
    }

  /** Enable USB Voltage detector
  */
    HAL_PWREx_EnableUSBVoltageDetector();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**USB_OTG_HS GPIO Configuration
    PC0     ------> USB_OTG_HS_ULPI_STP
    PC2_C     ------> USB_OTG_HS_ULPI_DIR
    PC3_C     ------> USB_OTG_HS_ULPI_NXT
    PA3     ------> USB_OTG_HS_ULPI_D0
    PA5     ------> USB_OTG_HS_ULPI_CK
    PB0     ------> USB_OTG_HS_ULPI_D1
    PB1     ------> USB_OTG_HS_ULPI_D2
    PB10     ------> USB_OTG_HS_ULPI_D3
    PB11     ------> USB_OTG_HS_ULPI_D4
    PB12     ------> USB_OTG_HS_ULPI_D5
    PB13     ------> USB_OTG_HS_ULPI_D6
    PB5     ------> USB_OTG_HS_ULPI_D7
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_2|GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    usb_msp_gpio_done = 1U;

    /* Peripheral clock enable */
    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE();
    usb_msp_clk_enable_done = 1U;

    /* Peripheral interrupt init */
    HAL_NVIC_SetPriority(OTG_HS_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
    usb_msp_nvic_done = 1U;
  /* USER CODE BEGIN USB_OTG_HS_MspInit 1 */

  /* USER CODE END USB_OTG_HS_MspInit 1 */
  }
  else if(pcdHandle->Instance==USB_OTG_FS)
  {
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
    clock_status = HAL_RCC_OscConfig(&RCC_OscInitStruct);
    usb_msp_clock_config_status = (uint32_t)clock_status;
    if (clock_status != HAL_OK)
    {
      Error_Handler();
    }

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    clock_status = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
    usb_msp_clock_config_status = (uint32_t)clock_status;
    if (clock_status != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_CRS_CLK_ENABLE();
    CRSInitStruct.Prescaler = RCC_CRS_SYNC_DIV1;
    CRSInitStruct.Source = RCC_CRS_SYNC_SOURCE_USB2;
    CRSInitStruct.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
    CRSInitStruct.ReloadValue = RCC_CRS_RELOADVALUE_DEFAULT;
    CRSInitStruct.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;
    CRSInitStruct.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
    HAL_RCCEx_CRSConfig(&CRSInitStruct);

    HAL_PWREx_EnableUSBVoltageDetector();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USB_OTG_FS GPIO Configuration
    PA11     ------> USB_OTG_FS_DM
    PA12     ------> USB_OTG_FS_DP
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG1_FS;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    usb_msp_gpio_done = 1U;

    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    usb_msp_clk_enable_done = 1U;

    HAL_NVIC_SetPriority(OTG_FS_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    usb_msp_nvic_done = 1U;
  }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef* pcdHandle)
{
  if(pcdHandle->Instance==USB_OTG_HS)
  {
  /* USER CODE BEGIN USB_OTG_HS_MspDeInit 0 */

  /* USER CODE END USB_OTG_HS_MspDeInit 0 */
    /* Disable Peripheral clock */
    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
    __HAL_RCC_USB_OTG_HS_ULPI_CLK_DISABLE();

    /**USB_OTG_HS GPIO Configuration
    PC0     ------> USB_OTG_HS_ULPI_STP
    PC2_C     ------> USB_OTG_HS_ULPI_DIR
    PC3_C     ------> USB_OTG_HS_ULPI_NXT
    PA3     ------> USB_OTG_HS_ULPI_D0
    PA5     ------> USB_OTG_HS_ULPI_CK
    PB0     ------> USB_OTG_HS_ULPI_D1
    PB1     ------> USB_OTG_HS_ULPI_D2
    PB10     ------> USB_OTG_HS_ULPI_D3
    PB11     ------> USB_OTG_HS_ULPI_D4
    PB12     ------> USB_OTG_HS_ULPI_D5
    PB13     ------> USB_OTG_HS_ULPI_D6
    PB5     ------> USB_OTG_HS_ULPI_D7
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0|GPIO_PIN_2|GPIO_PIN_3);

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_3|GPIO_PIN_5);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_5);

    /* Peripheral interrupt Deinit*/
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);

  /* USER CODE BEGIN USB_OTG_HS_MspDeInit 1 */

  /* USER CODE END USB_OTG_HS_MspDeInit 1 */
  }
  else if(pcdHandle->Instance==USB_OTG_FS)
  {
    __HAL_RCC_USB_OTG_FS_CLK_DISABLE();

    /**USB_OTG_FS GPIO Configuration
    PA11     ------> USB_OTG_FS_DM
    PA12     ------> USB_OTG_FS_DP
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11|GPIO_PIN_12);

    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
  }
}

#if !UVC_USB_STACK_IS_USBX
/**
  * @brief  Setup stage callback
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
#else
volatile uint32_t pcd_setup_stage_calls = 0;
volatile uint32_t pcd_setup_bmrequest = 0;
volatile uint32_t pcd_setup_brequest = 0;
volatile uint32_t pcd_setup_wvalue = 0;
volatile uint32_t pcd_setup_windex = 0;
volatile uint32_t pcd_setup_wlength = 0;
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  const uint8_t *setup = (const uint8_t *)hpcd->Setup;

  pcd_setup_stage_calls++;
  pcd_setup_bmrequest = setup[0];
  pcd_setup_brequest = setup[1];
  pcd_setup_wvalue = ((uint32_t)setup[3] << 8) | setup[2];
  pcd_setup_windex = ((uint32_t)setup[5] << 8) | setup[4];
  pcd_setup_wlength = ((uint32_t)setup[7] << 8) | setup[6];
  USBD_LL_SetupStage((USBD_HandleTypeDef*)hpcd->pData, (uint8_t *)hpcd->Setup);
}

/**
  * @brief  Data Out stage callback.
  * @param  hpcd: PCD handle
  * @param  epnum: Endpoint number
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  USBD_LL_DataOutStage((USBD_HandleTypeDef*)hpcd->pData, epnum, hpcd->OUT_ep[epnum].xfer_buff);
}

/**
  * @brief  Data In stage callback.
  * @param  hpcd: PCD handle
  * @param  epnum: Endpoint number
  * @retval None
  */
volatile uint32_t pcd_datain_stage_calls = 0;
volatile uint32_t pcd_datain_ep0_calls = 0;
volatile uint32_t pcd_datain_ep1_calls = 0;
volatile uint32_t pcd_datain_last_ep = 0xFFFFFFFF;
volatile uint32_t pcd_datain_ep1_diepctl = 0;
volatile uint32_t pcd_datain_ep1_dieptsiz = 0;
volatile uint32_t pcd_datain_ep1_diepint = 0;
volatile uint32_t pcd_datain_ep1_flush_calls = 0;
volatile uint32_t pcd_datain_ep1_flush_status = 0;
volatile uint32_t pcd_datain_ep1_flush_enable = 0;
volatile uint32_t pcd_datain_ep1_iiso_prehandle_enable = 0;
volatile uint32_t pcd_datain_ep1_iiso_prehandled = 0;
volatile uint32_t pcd_datain_ep1_iiso_prehandle_gintsts = 0;
volatile uint32_t uvc_process_from_datain_calls = 0;
volatile uint32_t uvc_process_from_datain_deferred = 0;
volatile uint32_t uvc_process_from_sof_calls = 0;
volatile uint32_t uvc_process_from_sof_retry_calls = 0;
volatile uint32_t uvc_process_from_isoin_incomplete_calls = 0;
volatile uint32_t uvc_process_from_isoin_incomplete_retry_calls = 0;
volatile uint32_t pcd_isoin_incomplete_calls = 0;
volatile uint32_t pcd_isoin_incomplete_last_ep = 0xFFFFFFFF;
volatile uint32_t pcd_isoin_incomplete_raw_ep = 0xFFFFFFFF;
volatile uint32_t pcd_isoin_incomplete_effective_ep = 0xFFFFFFFF;
volatile uint32_t pcd_isoin_incomplete_ep0_remaps = 0;
volatile uint32_t uvc_isoin_incomplete_last_offset = 0;
volatile uint32_t uvc_isoin_incomplete_last_packets = 0;
volatile uint32_t uvc_isoin_incomplete_last_frame_size = 0;
volatile uint32_t uvc_isoin_incomplete_last_preamble = 0;
volatile uint32_t uvc_isoin_incomplete_last_reopen_pending = 0;
volatile uint32_t uvc_isoin_incomplete_dsts = 0;
volatile uint32_t uvc_isoin_incomplete_diepctl = 0;
volatile uint32_t uvc_isoin_incomplete_dieptsiz = 0;
volatile uint32_t uvc_isoin_incomplete_diepint = 0;
volatile uint32_t uvc_isoin_incomplete_handling_enable = 1;
volatile uint32_t uvc_isoin_incomplete_minimal_enable = 0;
volatile uint32_t uvc_isoin_incomplete_burst = 0;
volatile uint32_t uvc_isoin_incomplete_stop_enable = 0;
volatile uint32_t uvc_isoin_incomplete_stop_threshold = 16;
volatile uint32_t uvc_isoin_incomplete_stop_calls = 0;
volatile uint32_t uvc_isoin_incomplete_stop_flush_status = 0;
volatile uint32_t uvc_isoin_incomplete_resync_calls = 0;
volatile uint32_t uvc_isoin_incomplete_flush_enable = 0;
volatile uint32_t uvc_isoin_incomplete_flush_calls = 0;
volatile uint32_t uvc_isoin_incomplete_flush_status = 0;
volatile uint32_t uvc_isoin_incomplete_reopen_calls = 0;
volatile uint32_t uvc_isoin_incomplete_reopen_ok = 0;
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    pcd_datain_stage_calls++;
    pcd_datain_last_ep = epnum;

    if (epnum == 0U)
    {
        pcd_datain_ep0_calls++;

        USBD_LL_DataInStage((USBD_HandleTypeDef*)hpcd->pData,
                            epnum,
                            hpcd->IN_ep[epnum].xfer_buff);
        return;
    }

    if (epnum == (UVC_IN_EP & 0x7FU))
    {
        pcd_datain_ep1_calls++;
        if ((pcd_datain_ep1_iiso_prehandle_enable != 0U) &&
            ((USB_OTG_HS->GINTSTS & USB_OTG_GINTSTS_IISOIXFR) == USB_OTG_GINTSTS_IISOIXFR))
        {
            pcd_datain_ep1_iiso_prehandled++;
            pcd_datain_ep1_iiso_prehandle_gintsts = USB_OTG_HS->GINTSTS;
            USBD_LL_IsoINIncomplete((USBD_HandleTypeDef*)hpcd->pData, epnum);
            __HAL_PCD_CLEAR_FLAG(hpcd, USB_OTG_GINTSTS_IISOIXFR);
        }
        if (usb_reg_snapshot_enable != 0U)
        {
            USB_OTG_INEndpointTypeDef *in_ep_regs =
                (USB_OTG_INEndpointTypeDef *)((uint32_t)USB_OTG_HS +
                                              USB_OTG_IN_ENDPOINT_BASE +
                                              ((uint32_t)epnum * USB_OTG_EP_REG_SIZE));
            pcd_datain_ep1_diepctl = in_ep_regs->DIEPCTL;
            pcd_datain_ep1_dieptsiz = in_ep_regs->DIEPTSIZ;
            pcd_datain_ep1_diepint = in_ep_regs->DIEPINT;
        }
    }

    USBD_LL_DataInStage((USBD_HandleTypeDef*)hpcd->pData,
                        epnum,
                        hpcd->IN_ep[epnum].xfer_buff);
}
#endif
/**
  * @brief  SOF callback.
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
#else
volatile uint32_t uvc_sof_calls = 0;
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
    uvc_sof_calls++;
    if (hpcd != NULL && hpcd->pData != NULL)
    {
        USBD_LL_SOF((USBD_HandleTypeDef *)hpcd->pData);
    }
}

/**
  * @brief  Reset callback.
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  USBD_SpeedTypeDef speed = USBD_SPEED_FULL;
  usb_reset_cb_calls++;

  if (hpcd->Init.speed == PCD_SPEED_HIGH)
  {
    speed = USBD_SPEED_HIGH;
  }
  else if (hpcd->Init.speed == PCD_SPEED_FULL)
  {
    speed = USBD_SPEED_FULL;
  }
  USBD_LL_Reset((USBD_HandleTypeDef*)hpcd->pData);
  usb_reset_cb_speed = (uint32_t)speed;
  USBD_LL_SetSpeed((USBD_HandleTypeDef*)hpcd->pData, speed);
}

/**
  * @brief  Suspend callback.
  * When Low power mode is enabled the debug cannot be used (IAR, Keil doesn't support it)
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usb_suspend_cb_calls++;
  /* Inform USB library that core enters in suspend Mode. */
  USBD_LL_Suspend((USBD_HandleTypeDef*)hpcd->pData);
  usb_suspend_phy_gate_skips++;
  usb_pcgcctl_after_suspend =
      *(__IO uint32_t *)((uint32_t)hpcd->Instance + USB_OTG_PCGCCTL_BASE);
  /* Enter in STOP mode. */
  /* USER CODE BEGIN 2 */
  if (hpcd->Init.low_power_enable)
  {
    /* Set SLEEPDEEP bit and SleepOnExit of Cortex System Control Register. */
    SCB->SCR |= (uint32_t)((uint32_t)(SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk));
  }
  /* USER CODE END 2 */
}

/**
  * @brief  Resume callback.
  * When Low power mode is enabled the debug cannot be used (IAR, Keil doesn't support it)
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usb_resume_cb_calls++;
  /* USER CODE BEGIN 3 */

  /* USER CODE END 3 */
  USBD_LL_Resume((USBD_HandleTypeDef*)hpcd->pData);
}

/**
  * @brief  ISOOUTIncomplete callback.
  * @param  hpcd: PCD handle
  * @param  epnum: Endpoint number
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef*)hpcd->pData, epnum);
}

/**
  * @brief  ISOINIncomplete callback.
  * @param  hpcd: PCD handle
  * @param  epnum: Endpoint number
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#else
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  uint8_t effective_epnum = epnum;

  pcd_isoin_incomplete_calls++;
  pcd_isoin_incomplete_raw_ep = epnum;

  if (epnum == 0U)
  {
    effective_epnum = (uint8_t)(UVC_IN_EP & 0x7FU);
    pcd_isoin_incomplete_ep0_remaps++;
  }

  pcd_isoin_incomplete_last_ep = effective_epnum;
  pcd_isoin_incomplete_effective_ep = effective_epnum;

  if (effective_epnum == (UVC_IN_EP & 0x7FU))
  {
    USB_OTG_DeviceTypeDef *dev_regs =
        (USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_HS + USB_OTG_DEVICE_BASE);
    USB_OTG_INEndpointTypeDef *in_ep_regs =
        (USB_OTG_INEndpointTypeDef *)((uint32_t)USB_OTG_HS +
                                      USB_OTG_IN_ENDPOINT_BASE +
                                      ((uint32_t)effective_epnum * USB_OTG_EP_REG_SIZE));
    uvc_isoin_incomplete_burst++;
    uvc_isoin_incomplete_last_offset = uvc_stream_get_offset();
    uvc_isoin_incomplete_last_packets = uvc_stream_get_packet_count();
    uvc_isoin_incomplete_last_frame_size = uvc_stream_get_frame_size();
    uvc_isoin_incomplete_last_preamble = uvc_stream_get_preamble_sent();
    uvc_isoin_incomplete_last_reopen_pending = uvc_stream_get_reopen_pending();
    uvc_isoin_incomplete_dsts = dev_regs->DSTS;
    uvc_isoin_incomplete_diepctl = in_ep_regs->DIEPCTL;
    uvc_isoin_incomplete_dieptsiz = in_ep_regs->DIEPTSIZ;
    uvc_isoin_incomplete_diepint = in_ep_regs->DIEPINT;
    if (uvc_isoin_incomplete_handling_enable != 0U)
    {
      if ((uvc_isoin_incomplete_dsts & (1U << USB_OTG_DSTS_FNSOF_Pos)) == 0U)
      {
        in_ep_regs->DIEPCTL |= USB_OTG_DIEPCTL_SODDFRM;
      }
      else
      {
        in_ep_regs->DIEPCTL |= USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
      }
      uvc_isoin_incomplete_resync_calls++;
    }
    if (uvc_isoin_incomplete_flush_enable != 0U)
    {
      uvc_isoin_incomplete_flush_status =
          (uint32_t)USB_FlushTxFifo(USB_OTG_HS, effective_epnum);
      uvc_isoin_incomplete_flush_calls++;
    }
    uvc_process_from_isoin_incomplete_calls++;
  }

  USBD_LL_IsoINIncomplete((USBD_HandleTypeDef*)hpcd->pData, effective_epnum);
}

/**
  * @brief  Connect callback.
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usb_connect_cb_calls++;
  USBD_LL_DevConnected((USBD_HandleTypeDef*)hpcd->pData);
}

/**
  * @brief  Disconnect callback.
  * @param  hpcd: PCD handle
  * @retval None
  */
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
#else
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
{
  usb_disconnect_cb_calls++;
  USBD_LL_DevDisconnected((USBD_HandleTypeDef*)hpcd->pData);
}
#endif /* !UVC_USB_STACK_IS_USBX */

/*******************************************************************************
                       LL Driver Interface (USB Device Library --> PCD)
*******************************************************************************/

/**
  * @brief  Initializes the low level portion of the device driver.
  * @param  pdev: Device handle
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef pcd_init_status;

  usb_ll_init_calls++;
  /* Init USB Ip. */
  if (pdev->id == DEVICE_HS) {
  usb_ll_init_hs_calls++;
  /* Link the driver to the stack. */
  hpcd_USB_OTG_HS.pData = pdev;
  pdev->pData = &hpcd_USB_OTG_HS;

  hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hpcd_USB_OTG_HS.Init.dev_endpoints = 4;
  hpcd_USB_OTG_HS.Init.ep0_mps = 0x40;
  hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_HIGH;
  hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  usb_dma_enable_dbg = 0U;
  hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_ULPI_PHY;
  hpcd_USB_OTG_HS.Init.Sof_enable = ENABLE;
  hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
  pcd_init_status = HAL_PCD_Init(&hpcd_USB_OTG_HS);
  usb_hal_pcd_init_status = (uint32_t)pcd_init_status;
  if (pcd_init_status != HAL_OK)
  {
    Error_Handler( );
  }
  ((USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_HS + USB_OTG_DEVICE_BASE))->DTHRCTL = 0x0C100020U;
  usb_dthrctl_dbg =
      ((USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_HS + USB_OTG_DEVICE_BASE))->DTHRCTL;
  USB_MaskIISOIXFRIfEnabled();

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
  /* Register USB PCD CallBacks */
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_SOF_CB_ID, PCD_SOFCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_SETUPSTAGE_CB_ID, PCD_SetupStageCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_RESET_CB_ID, PCD_ResetCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_SUSPEND_CB_ID, PCD_SuspendCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_RESUME_CB_ID, PCD_ResumeCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_CONNECT_CB_ID, PCD_ConnectCallback);
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_HS, HAL_PCD_DISCONNECT_CB_ID, PCD_DisconnectCallback);

  HAL_PCD_RegisterDataOutStageCallback(&hpcd_USB_OTG_HS, PCD_DataOutStageCallback);
  HAL_PCD_RegisterDataInStageCallback(&hpcd_USB_OTG_HS, PCD_DataInStageCallback);
  HAL_PCD_RegisterIsoOutIncpltCallback(&hpcd_USB_OTG_HS, PCD_ISOOUTIncompleteCallback);
  HAL_PCD_RegisterIsoInIncpltCallback(&hpcd_USB_OTG_HS, PCD_ISOINIncompleteCallback);
#endif /* USE_HAL_PCD_REGISTER_CALLBACKS */
  /* USER CODE BEGIN TxRx_HS_Configuration */
  HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS, 0x64);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 0, 0x32);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 1, 0x300);
  usb_fifo_rx_words_dbg = 0x64;
  usb_fifo_tx0_words_dbg = 0x32;
  usb_fifo_tx1_words_dbg = 0x300;
  usb_fifo_tx1_packet_slots_dbg = usb_fifo_tx1_words_dbg / (UVC_IN_PACKET / 4U);
  usb_fifo_total_words_dbg = usb_fifo_rx_words_dbg +
                             usb_fifo_tx0_words_dbg +
                             usb_fifo_tx1_words_dbg;
  usb_grxfsiz_dbg = USB_OTG_HS->GRXFSIZ;
  usb_dieptxf0_dbg = USB_OTG_HS->DIEPTXF0_HNPTXFSIZ;
  usb_dieptxf1_dbg = USB_OTG_HS->DIEPTXF[0];
  /* USER CODE END TxRx_HS_Configuration */
  }
  return USBD_OK;
}

/**
  * @brief  De-Initializes the low level portion of the device driver.
  * @param  pdev: Device handle
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_DeInit(pdev->pData);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Starts the low level portion of the device driver.
  * @param  pdev: Device handle
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  usb_ll_start_calls++;
  usb_gintsts_before_start = USB_OTG_HS->GINTSTS;
  usb_gotgctl_before_start = USB_OTG_HS->GOTGCTL;
  usb_dctl_before_start =
      ((USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_HS + USB_OTG_DEVICE_BASE))->DCTL;
  usb_dsts_before_start =
      ((USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_HS + USB_OTG_DEVICE_BASE))->DSTS;

  hal_status = HAL_PCD_Start(pdev->pData);
  usb_hal_pcd_start_status = (uint32_t)hal_status;
  USB_MaskIISOIXFRIfEnabled();

  usb_status =  USBD_Get_USB_Status(hal_status);
  usb_ll_start_usb_status = (uint32_t)usb_status;
  usb_gccfg_after_start = USB_OTG_HS->GCCFG;
  usb_gintsts_after_start = USB_OTG_HS->GINTSTS;
  usb_gintmsk_after_start = USB_OTG_HS->GINTMSK;
  usb_gusbcfg_after_start = USB_OTG_HS->GUSBCFG;
  usb_grstctl_after_start = USB_OTG_HS->GRSTCTL;
  usb_dctl_after_start =
      ((USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_HS + USB_OTG_DEVICE_BASE))->DCTL;
  usb_dsts_after_start =
      ((USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_HS + USB_OTG_DEVICE_BASE))->DSTS;
  usb_pcgcctl_after_start =
      *(__IO uint32_t *)((uint32_t)USB_OTG_HS + USB_OTG_PCGCCTL_BASE);
  usb_gotgctl_after_start = USB_OTG_HS->GOTGCTL;
  usb_gahbcfg_after_start = USB_OTG_HS->GAHBCFG;
  usb_dcfg_after_start =
      ((USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_HS + USB_OTG_DEVICE_BASE))->DCFG;

  return usb_status;
}

/**
  * @brief  Stops the low level portion of the device driver.
  * @param  pdev: Device handle
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_Stop(pdev->pData);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Opens an endpoint of the low level driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @param  ep_type: Endpoint type
  * @param  ep_mps: Endpoint max packet size
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Closes an endpoint of the low level driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Close(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Flushes an endpoint of the Low Level Driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Flush(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Sets a Stall condition on an endpoint of the Low Level Driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_SetStall(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Clears a Stall condition on an endpoint of the Low Level Driver.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_ClrStall(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Returns Stall condition.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval Stall (1: Yes, 0: No)
  */
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef*) pdev->pData;

  if((ep_addr & 0x80) == 0x80)
  {
    return hpcd->IN_ep[ep_addr & 0x7F].is_stall;
  }
  else
  {
    return hpcd->OUT_ep[ep_addr & 0x7F].is_stall;
  }
}

/**
  * @brief  Assigns a USB address to the device.
  * @param  pdev: Device handle
  * @param  dev_addr: Device address
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_SetAddress(pdev->pData, dev_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Transmits data over an endpoint.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @param  pbuf: Pointer to data to be sent
  * @param  size: Data size
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;
  uint8_t ep_num = ep_addr & 0x7FU;
  uint32_t cycle_start = USB_DWT_Begin();

  if ((hpcd != NULL) &&
      (hpcd->Init.dma_enable == ENABLE) &&
      (ep_num == 0U) &&
      (pbuf != NULL) &&
      (size > 0U) &&
      (size <= sizeof(usb_dma_ep0_tx_buf)))
  {
    memcpy(usb_dma_ep0_tx_buf, pbuf, size);
    pbuf = usb_dma_ep0_tx_buf;
  }

  hal_status = HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size);

  usb_status =  USBD_Get_USB_Status(hal_status);
  USB_DWT_Record(&usb_ll_transmit_cycles_last, &usb_ll_transmit_cycles_max, cycle_start);

  return usb_status;
}

/**
  * @brief  Prepares an endpoint for reception.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @param  pbuf: Pointer to data to be received
  * @param  size: Data size
  * @retval USBD status
  */
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

/**
  * @brief  Returns the last transferred packet size.
  * @param  pdev: Device handle
  * @param  ep_addr: Endpoint number
  * @retval Received Data Size
  */
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef*) pdev->pData, ep_addr);
}

#ifdef USBD_HS_TESTMODE_ENABLE
/**
  * @brief  Set High speed Test mode.
  * @param  pdev: Device handle
  * @param  testmode: test mode
  * @retval USBD Status
  */
USBD_StatusTypeDef USBD_LL_SetTestMode(USBD_HandleTypeDef *pdev, uint8_t testmode)
{
  UNUSED(pdev);
  UNUSED(testmode);

  return USBD_OK;
}
#endif /* USBD_HS_TESTMODE_ENABLE */
/**
  * @brief  Static single allocation.
  * @param  size: Size of allocated memory
  * @retval None
  */
void *USBD_static_malloc(uint32_t size)
{
  UNUSED(size);
  //static uint32_t mem[(sizeof(USBD_HandleTypeDef)/4)+1];/* On 32-bit boundary */
  return 0;
}

/**
  * @brief  Dummy memory free
  * @param  p: Pointer to allocated  memory address
  * @retval None
  */
void USBD_static_free(void *p)
{
  UNUSED(p);
}

/**
  * @brief  Delays routine for the USB device library.
  * @param  Delay: Delay in ms
  * @retval None
  */
void USBD_LL_Delay(uint32_t Delay)
{
  HAL_Delay(Delay);
}

/**
  * @brief  Returns the USB status depending on the HAL status:
  * @param  hal_status: HAL status
  * @retval USB status
  */
USBD_StatusTypeDef USBD_Get_USB_Status(HAL_StatusTypeDef hal_status)
{
  USBD_StatusTypeDef usb_status = USBD_OK;

  switch (hal_status)
  {
    case HAL_OK :
      usb_status = USBD_OK;
    break;
    case HAL_ERROR :
      usb_status = USBD_FAIL;
    break;
    case HAL_BUSY :
      usb_status = USBD_BUSY;
    break;
    case HAL_TIMEOUT :
      usb_status = USBD_FAIL;
    break;
    default :
      usb_status = USBD_FAIL;
    break;
  }
  return usb_status;
}
