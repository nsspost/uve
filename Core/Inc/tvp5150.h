/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tvp5150.h
  * @brief   TVP5150 bring-up helpers.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __TVP5150_H__
#define __TVP5150_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define TVP5150_I2C_ADDR0_7BIT 0x5CU
#define TVP5150_I2C_ADDR1_7BIT 0x5DU

#define TVP5150_OUTPUT_MODE_BT656            0U
#define TVP5150_OUTPUT_MODE_DISCRETE_SYNC    1U

extern volatile uint32_t tvp5150_probe_done_dbg;
extern volatile uint32_t tvp5150_probe_ok_dbg;
extern volatile uint32_t tvp5150_probe_addr7_dbg;
extern volatile uint32_t tvp5150_probe_dev_addr_dbg;
extern volatile uint32_t tvp5150_probe_hal_status_dbg;
extern volatile uint32_t tvp5150_probe_error_code_dbg;
extern volatile uint32_t tvp5150_i2c_found_count_dbg;
extern volatile uint32_t tvp5150_i2c_found_addr0_dbg;
extern volatile uint32_t tvp5150_i2c_found_addr1_dbg;
extern volatile uint32_t tvp5150_i2c_found_addr2_dbg;
extern volatile uint32_t tvp5150_i2c_found_addr3_dbg;
extern volatile uint32_t tvp5150_i2c_found_addr4_dbg;
extern volatile uint32_t tvp5150_i2c_found_addr5_dbg;
extern volatile uint32_t tvp5150_i2c_found_addr6_dbg;
extern volatile uint32_t tvp5150_i2c_found_addr7_dbg;
extern volatile uint32_t tvp5150_id_msb_dbg;
extern volatile uint32_t tvp5150_id_lsb_dbg;
extern volatile uint32_t tvp5150_rom_major_dbg;
extern volatile uint32_t tvp5150_rom_minor_dbg;
extern volatile uint32_t tvp5150_reg00_dbg;
extern volatile uint32_t tvp5150_reg01_dbg;
extern volatile uint32_t tvp5150_reg03_dbg;
extern volatile uint32_t tvp5150_reg0d_dbg;
extern volatile uint32_t tvp5150_reg0f_dbg;
extern volatile uint32_t tvp5150_status1_dbg;
extern volatile uint32_t tvp5150_status2_dbg;
extern volatile uint32_t tvp5150_output_mode_dbg;
extern volatile uint32_t tvp5150_agc_enable_dbg;
extern volatile uint32_t tvp5150_agc_target_dbg;
extern volatile uint32_t tvp5150_config_done_dbg;
extern volatile uint32_t tvp5150_config_ok_dbg;
extern volatile uint32_t tvp5150_config_addr7_dbg;
extern volatile uint32_t tvp5150_config_reg01_status_dbg;
extern volatile uint32_t tvp5150_config_reg03_status_dbg;
extern volatile uint32_t tvp5150_config_reg0d_status_dbg;
extern volatile uint32_t tvp5150_config_reg0f_status_dbg;
extern volatile uint32_t tvp5150_config_readback_status_dbg;

void TVP5150_I2CProbe_Run(I2C_HandleTypeDef *hi2c);

#ifdef __cplusplus
}
#endif

#endif /* __TVP5150_H__ */
