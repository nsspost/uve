/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "dma2d.h"
#include "ltdc.h"
#include "mdma.h"
#include "jpeg.h"
#include "spi.h"
#include "usb_device.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "BSP_SDRAM.h"
#include "ili9488.h"
#include "camera_pipeline.h"
#include "video_source.h"
#include "uvc_stream.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
__attribute__((section(".xsdram"), aligned(32)))
uint16_t fb[320 * 480];

volatile uint32_t uvc_process_from_main_poll_calls = 0;
volatile uint32_t main_usb_only_mode = 0;
volatile uint32_t main_test_pattern_enable = 0;
volatile uint32_t main_test_pattern_updates = 0;
volatile uint32_t main_test_pattern_period_ms = 500;
volatile uint32_t main_test_pattern_phase = 0;
volatile uint32_t main_usb_init_early = 0;
volatile uint32_t main_usb_init_done = 0;
volatile uint32_t main_sdram_enable = 1;
volatile uint32_t main_sdram_init_done = 0;
volatile uint32_t main_sdram_test_done = 0;
volatile uint32_t main_sdram_test_ok = 0;
volatile uint32_t main_sdram_test_errors = 0;
volatile uint32_t main_sdram_test_last_addr = 0;
volatile uint32_t main_sdram_test_last_expected = 0;
volatile uint32_t main_sdram_test_last_actual = 0;
volatile uint32_t main_display_enable = 1;
volatile uint32_t main_display_init_done = 0;
volatile uint32_t main_display_pattern_done = 0;
volatile uint32_t main_display_pattern_status = 0;
volatile uint32_t main_jpeg_hw_enable = 1;
volatile uint32_t main_mdma_init_done = 0;
volatile uint32_t main_jpeg_init_done = 0;
volatile uint32_t main_live_jpeg_enable = 1;
volatile uint32_t main_live_jpeg_init_done = 0;
volatile uint32_t main_camera_pipeline_enable = 0;
volatile uint32_t main_first_jpeg_ready_before_usb = 0;
volatile uint32_t main_first_jpeg_size_before_usb = 0;
volatile const uint8_t *main_first_jpeg_ptr_before_usb = 0;
volatile uint32_t main_usb_only_loop_calls = 0;
volatile uint32_t main_mpu_enable_dbg = 0;
volatile uint32_t main_icache_enable_dbg = 0;
volatile uint32_t main_dcache_enable_dbg = 0;
volatile uint32_t dma2d_last_rgb565_color = 0;
volatile uint32_t dma2d_last_argb8888_color = 0;

HAL_StatusTypeDef dma2d_fill_screen(uint16_t color);
HAL_StatusTypeDef dma2d_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

static void sdram_smoke_test(void)
{
    volatile uint32_t *p = (volatile uint32_t *)0xD07FF000U;
    const uint32_t words = 256U;
    uint32_t errors = 0U;

    for (uint32_t i = 0; i < words; i++)
    {
        p[i] = 0xA5A50000U ^ (i * 0x01010101U);
    }

    for (uint32_t i = 0; i < words; i++)
    {
        uint32_t expected = 0xA5A50000U ^ (i * 0x01010101U);
        uint32_t actual = p[i];
        if (actual != expected)
        {
            errors++;
            main_sdram_test_last_addr = (uint32_t)&p[i];
            main_sdram_test_last_expected = expected;
            main_sdram_test_last_actual = actual;
        }
    }

    for (uint32_t i = 0; i < words; i++)
    {
        p[i] = 0x5A5A0000U ^ (i * 0x00110203U);
    }

    for (uint32_t i = 0; i < words; i++)
    {
        uint32_t expected = 0x5A5A0000U ^ (i * 0x00110203U);
        uint32_t actual = p[i];
        if (actual != expected)
        {
            errors++;
            main_sdram_test_last_addr = (uint32_t)&p[i];
            main_sdram_test_last_expected = expected;
            main_sdram_test_last_actual = actual;
        }
    }

    main_sdram_test_errors = errors;
    main_sdram_test_ok = (errors == 0U) ? 1U : 0U;
    main_sdram_test_done = 1U;
}

static void cpu_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= 320U || y >= 480U || w == 0U || h == 0U)
        return;

    if ((uint32_t)x + w > 320U)
        w = 320U - x;

    if ((uint32_t)y + h > 480U)
        h = 480U - y;

    for (uint16_t yy = 0; yy < h; yy++)
    {
        uint32_t row = ((uint32_t)y + yy) * 320U + x;
        for (uint16_t xx = 0; xx < w; xx++)
        {
            fb[row + xx] = color;
        }
    }
}

static void cpu_fill_screen(uint16_t color)
{
    cpu_fill_rect(0, 0, 320, 480, color);
}

static void draw_stream_test_pattern(void)
{
    static uint32_t last_tick = 0xFFFFFFFFU;
    static uint32_t phase = 0U;
    uint32_t now = HAL_GetTick();
    uint16_t marker_x;

    if (last_tick != 0xFFFFFFFFU && (now - last_tick) < main_test_pattern_period_ms)
        return;

    last_tick = now;
    phase++;
    main_test_pattern_phase = phase;
    main_test_pattern_updates++;

    if ((phase & 1U) == 0U)
    {
        cpu_fill_screen(0x0000); // black
        cpu_fill_rect(0, 0, 64, 480, 0xF800); // red
        cpu_fill_rect(64, 0, 64, 480, 0x07E0); // green
        cpu_fill_rect(128, 0, 64, 480, 0x001F); // blue
        cpu_fill_rect(192, 0, 64, 480, 0xFFE0); // yellow
        cpu_fill_rect(256, 0, 64, 480, 0xF81F); // magenta
    }
    else
    {
        cpu_fill_screen(0x07FF); // cyan
        for (uint16_t y = 0; y < 480; y += 40)
        {
            for (uint16_t x = 0; x < 320; x += 40)
            {
                uint16_t color = (((x + y) / 40U) & 1U) ? 0xFFFFU : 0x0000U;
                cpu_fill_rect(x, y, 40, 40, color);
            }
        }
    }

    marker_x = (uint16_t)((phase % 20U) * 16U);
    cpu_fill_rect(0, 0, 320, 12, 0x0000); // black timing strip
    cpu_fill_rect(marker_x, 0, 16, 12, 0xFFFF); // moving marker
    cpu_fill_rect(0, 468, (uint16_t)((phase % 21U) * 16U), 12, 0x07E0); // phase bar
}

void fill_fb(uint16_t color)
{
    for (uint32_t i = 0; i < 320U * 480U; i++) {
        fb[i] = color;
    }
}

static uint32_t rgb565_to_argb8888(uint16_t color)
{
    uint32_t r5 = (color >> 11) & 0x1FU;
    uint32_t g6 = (color >> 5) & 0x3FU;
    uint32_t b5 = color & 0x1FU;
    uint32_t r8 = (r5 << 3) | (r5 >> 2);
    uint32_t g8 = (g6 << 2) | (g6 >> 4);
    uint32_t b8 = (b5 << 3) | (b5 >> 2);

    return 0xFF000000U | (r8 << 16) | (g8 << 8) | b8;
}

HAL_StatusTypeDef dma2d_fill_screen(uint16_t color)
{
    uint32_t argb = rgb565_to_argb8888(color);

    hdma2d.Init.Mode = DMA2D_R2M;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = 0;
    dma2d_last_rgb565_color = color;
    dma2d_last_argb8888_color = argb;

    if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (HAL_DMA2D_Start(&hdma2d, argb, (uint32_t)fb, 320, 480) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (HAL_DMA2D_PollForTransfer(&hdma2d, 100) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

static void display_draw_start_pattern(void)
{
    HAL_StatusTypeDef status = HAL_OK;

    if (status == HAL_OK) status = dma2d_fill_screen(0x0000U);
    if (status == HAL_OK) status = dma2d_fill_rect(0,   0, 320, 80,  0xF800U);
    if (status == HAL_OK) status = dma2d_fill_rect(0,  80, 320, 80,  0x07E0U);
    if (status == HAL_OK) status = dma2d_fill_rect(0, 160, 320, 80,  0x001FU);
    if (status == HAL_OK) status = dma2d_fill_rect(0, 240, 320, 80,  0xFFE0U);
    if (status == HAL_OK) status = dma2d_fill_rect(0, 320, 320, 80,  0xF81FU);
    if (status == HAL_OK) status = dma2d_fill_rect(0, 400, 320, 80,  0x07FFU);

    main_display_pattern_status = (uint32_t)status;
    main_display_pattern_done = 1U;
}

static void display_layer_init(void)
{
    MX_LTDC_Init();
    MX_SPI5_Init();

    ILI9488_Init();
    ILI9488_SendCommand(0x21);
    HAL_LTDC_SetAddress(&hltdc, (uint32_t)fb, 0);
    HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_IMMEDIATE);

    MX_DMA2D_Init();
    main_display_init_done = 1U;
    display_draw_start_pattern();
}

static void jpeg_hw_layer_init(void)
{
    MX_MDMA_Init();
    main_mdma_init_done = 1U;

    MX_JPEG_Init();
    main_jpeg_init_done = 1U;
}

HAL_StatusTypeDef dma2d_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint32_t argb = rgb565_to_argb8888(color);

    if (x >= 320 || y >= 480 || w == 0 || h == 0)
        return HAL_ERROR;

    if ((uint32_t)x + w > 320)
        w = 320 - x;

    if ((uint32_t)y + h > 480)
        h = 480 - y;

    uint32_t dst = (uint32_t)&fb[y * 320 + x];

    hdma2d.Init.Mode = DMA2D_R2M;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = 320 - w;
    dma2d_last_rgb565_color = color;
    dma2d_last_argb8888_color = argb;

    if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (HAL_DMA2D_Start(&hdma2d, argb, dst, w, h) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (HAL_DMA2D_PollForTransfer(&hdma2d, 100) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

extern JPEG_HandleTypeDef hjpeg;


/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint32_t dbg_dbgmcu_idcode = 0U;
volatile uint32_t dbg_dbgmcu_devid = 0U;
volatile uint32_t dbg_dbgmcu_revid = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
extern uint32_t jpeg_dbg_jpeg_irq_enabled;// = NVIC_GetEnableIRQ(JPEG_IRQn);
extern uint32_t jpeg_dbg_jpeg_irq_pending;// = NVIC_GetPendingIRQ(JPEG_IRQn);
extern uint32_t jpeg_dbg_mdma_irq_enabled;// = NVIC_GetEnableIRQ(MDMA_IRQn);
extern uint32_t jpeg_dbg_mdma_irq_pending;// = NVIC_GetPendingIRQ(MDMA_IRQn);

extern uint32_t jpeg_dbg_hjpeg_state;// = (uint32_t)hjpeg.State;
extern uint32_t jpeg_dbg_hjpeg_error;// = (uint32_t)hjpeg.ErrorCode;
extern uint32_t jpeg_dbg_in_length;// = hjpeg.InDataLength;
extern uint32_t jpeg_dbg_out_length;// = hjpeg.OutDataLength;
extern uint32_t jpeg_dbg_in_count;// = hjpeg.JpegInCount;
extern uint32_t jpeg_dbg_out_count;// = hjpeg.JpegOutCount;

extern uint32_t jpeg_dbg_hdmain_state;// = (uint32_t)hjpeg.hdmain->State;
extern uint32_t jpeg_dbg_hdmaout_state;// = (uint32_t)hjpeg.hdmaout->State;

/* прямое чтение регистров JPEG */
extern  uint32_t jpeg_dbg_sr;// = hjpeg.Instance->SR;
extern  uint32_t jpeg_dbg_cr;// = hjpeg.Instance->CR;

extern uint32_t jpeg_dbg_rgb_input_index;// = g_rgb_input_index;
extern uint32_t g_rgb_input_index;

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();
  main_mpu_enable_dbg = 1U;
  main_icache_enable_dbg = 0U;
  main_dcache_enable_dbg = 0U;

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
  dbg_dbgmcu_idcode = DBGMCU->IDCODE;
  dbg_dbgmcu_devid = dbg_dbgmcu_idcode & 0x0FFFU;
  dbg_dbgmcu_revid = dbg_dbgmcu_idcode >> 16;

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
//  MX_GPIO_Init();
//  MX_DMA2D_Init();
//  MX_FMC_Init();
//  MX_LTDC_Init();
//  MX_SPI5_Init();
//  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */



//  HAL_Delay(200);

  MX_GPIO_Init();
  if (main_usb_init_early != 0U)
  {
      MX_USB_DEVICE_Init();
      main_usb_init_done = 1U;
  }

  if (main_sdram_enable != 0U)
  {
      MX_FMC_Init();
      BSP_SDRAM_Init();
      main_sdram_init_done = 1U;
      sdram_smoke_test();
  }

  if (main_display_enable != 0U)
  {
      display_layer_init();
  }

  if (main_jpeg_hw_enable != 0U)
  {
      jpeg_hw_layer_init();
  }

  if (main_usb_only_mode == 0U)
  {
  if (main_sdram_init_done == 0U)
  {
      MX_FMC_Init();
      BSP_SDRAM_Init();
      main_sdram_init_done = 1U;
      sdram_smoke_test();
  }

  if (main_display_init_done == 0U)
  {
      display_layer_init();
  }
//  __HAL_RCC_SYSCFG_CLK_ENABLE();
//  HAL_EnableCompensationCell();

// отрисовка тестового фреймбуфера
  if (main_jpeg_init_done == 0U)
  {
      jpeg_hw_layer_init();
  }

  camera_pipeline_force_test_jpeg = (main_live_jpeg_enable != 0U) ? 0U : 1U;
  camera_pipeline_init();
  main_live_jpeg_init_done = 1U;
  camera_pipeline_update();
  video_source_commit_pending_if_any();
  main_camera_pipeline_enable = 1U;
  }
  else
  {
      main_test_pattern_enable = 0U;
      if (main_live_jpeg_enable != 0U)
      {
          camera_pipeline_force_test_jpeg = 0U;
          camera_pipeline_init();
          main_live_jpeg_init_done = 1U;
          camera_pipeline_update();
          video_source_commit_pending_if_any();
      }
      else
      {
          camera_pipeline_force_test_jpeg = 1U;
          video_source_init();
      }
      main_camera_pipeline_enable = 1U;
  }
  //camera_pipeline_init();
  //camera_pipeline_update();   // <-- подготовить первый JPEG заранее

  if (main_usb_init_done == 0U)
  {
      const video_frame_t *first_frame = video_source_get_current_frame();

      main_first_jpeg_ready_before_usb =
          ((first_frame != 0) && (first_frame->data != 0) && (first_frame->size != 0U)) ? 1U : 0U;
      main_first_jpeg_size_before_usb =
          (main_first_jpeg_ready_before_usb != 0U) ? first_frame->size : 0U;
      main_first_jpeg_ptr_before_usb =
          (main_first_jpeg_ready_before_usb != 0U) ? first_frame->data : 0;
      if (main_first_jpeg_ready_before_usb == 0U)
      {
          video_source_init();
          first_frame = video_source_get_current_frame();
          main_first_jpeg_ready_before_usb =
              ((first_frame != 0) && (first_frame->data != 0) && (first_frame->size != 0U)) ? 1U : 0U;
          main_first_jpeg_size_before_usb =
              (main_first_jpeg_ready_before_usb != 0U) ? first_frame->size : 0U;
          main_first_jpeg_ptr_before_usb =
              (main_first_jpeg_ready_before_usb != 0U) ? first_frame->data : 0;
      }

      MX_USB_DEVICE_Init();
      main_usb_init_done = 1U;
  }




      #define LCD_W 320
#define LCD_H 480


//      uvc_stream_init();
//      uvc_stream_start();



      while (1)
          {

          main_usb_only_loop_calls++;

    	  if ((main_usb_only_mode == 0U) && (main_test_pattern_enable != 0U))
    	  {
    	      draw_stream_test_pattern();
    	  }
    	  if (main_camera_pipeline_enable != 0U)
    	  {
    	      camera_pipeline_update();
    	  }
    	  uvc_stream_watchdog_poll();
    	  if (uvc_stream_needs_sof_poll())
    	  {
    	      uvc_process_from_main_poll_calls++;
    	      uvc_stream_poll_pending();
    	  }
//    	  if (uvc_stream_is_active())
//    	      {
//    	          uvc_stream_process();
//    	          HAL_Delay(2);
//    	      }

    	    //  camera_pipeline_update();
//
//              dma2d_fill_screen(0xF800); // black
//
//              dma2d_fill_rect(0,   0, 320, 80,  0xF800); // red
//              dma2d_fill_rect(0,  80, 320, 80,  0x07E0); // green
//              dma2d_fill_rect(0, 160, 320, 80,  0x001F); // blue
//              dma2d_fill_rect(0, 240, 320, 80,  0xFFE0); // yellow
//              dma2d_fill_rect(0, 320, 320, 80,  0xF81F); // magenta
//              dma2d_fill_rect(0, 400, 320, 80,  0x07FF); // cyan
//              camera_pipeline_update();
//
//              HAL_Delay(10);
//       //       camera_pipeline_update();
//              dma2d_fill_screen(0xFFE0);
//
//              for (uint16_t i = 0; i < 100; i += 10)
//              {
//                  dma2d_fill_rect(10 + i, 10 + i, 80, 80, 0xFFFF); // white
//                  dma2d_fill_rect(230 - i, 10 + i, 80, 80, 0xF800); // red
//                  dma2d_fill_rect(10 + i, 390 - i, 80, 80, 0x07E0); // green
//                  dma2d_fill_rect(230 - i, 390 - i, 80, 80, 0x001F); // blue
//              }
//              camera_pipeline_update();
//
//              HAL_Delay(10);
//              jpeg_dbg_jpeg_irq_enabled = NVIC_GetEnableIRQ(JPEG_IRQn);
//              jpeg_dbg_jpeg_irq_pending = NVIC_GetPendingIRQ(JPEG_IRQn);
//              jpeg_dbg_mdma_irq_enabled = NVIC_GetEnableIRQ(MDMA_IRQn);
//              jpeg_dbg_mdma_irq_pending = NVIC_GetPendingIRQ(MDMA_IRQn);
//
//              jpeg_dbg_hjpeg_state = (uint32_t)hjpeg.State;
//              jpeg_dbg_hjpeg_error = (uint32_t)hjpeg.ErrorCode;
//              jpeg_dbg_in_length = hjpeg.InDataLength;
//              jpeg_dbg_out_length = hjpeg.OutDataLength;
//              jpeg_dbg_in_count = hjpeg.JpegInCount;
//              jpeg_dbg_out_count = hjpeg.JpegOutCount;
//
//              jpeg_dbg_hdmain_state = (uint32_t)hjpeg.hdmain->State;
//              jpeg_dbg_hdmaout_state = (uint32_t)hjpeg.hdmaout->State;
//
//              /* прямое чтение регистров JPEG */
//              jpeg_dbg_sr = hjpeg.Instance->SR;
//              jpeg_dbg_cr = hjpeg.Instance->CR;


          }
      //  ILI9488_Init();
//  MX_LTDC_Init();
//  MX_DMA2D_Init();
  HAL_Delay(200);
  // тест sdram
//  BSP_LCD_Init();
//  BSP_LCD_SelectLayer(0);
//  HAL_LTDC_SetAddress(&hltdc, (uint32_t)fb, 0);
//  HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_IMMEDIATE);
  //fill_fb(0xF800);

  //BSP_LCD_Clear(LCD_COLOR_RED);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 480;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 16;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x08000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_2MB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_PRIV_RO;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0xD0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_8MB;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* USB internal DMA reads UVC buffers from RAM_D1. Keep the whole bank
   * non-cacheable for this experiment, so DMA and CPU always see the same data.
   */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress = 0x24000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
