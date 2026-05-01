/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tvp5150.c
  * @brief   TVP5150 bring-up helpers.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "tvp5150.h"

#define TVP5150_REG_INPUT_SEL      0x00U
#define TVP5150_REG_ANALOG_CTRL    0x01U
#define TVP5150_REG_MISC_CTRL      0x03U
#define TVP5150_REG_OUTPUT_FORMAT  0x0DU
#define TVP5150_REG_PINS_CONFIG    0x0FU
#define TVP5150_REG_DEVICE_ID_MSB  0x80U
#define TVP5150_REG_DEVICE_ID_LSB  0x81U
#define TVP5150_REG_ROM_MAJOR      0x82U
#define TVP5150_REG_ROM_MINOR      0x83U
#define TVP5150_REG_STATUS1        0x88U
#define TVP5150_REG_STATUS2        0x89U

#define TVP5150_MISC_ENABLE_DATA_CLOCK        0x09U
#define TVP5150_MISC_ENABLE_DATA_CLOCK_SYNCS  0x0DU
#define TVP5150_OUTPUT_8BIT_422_BT656         0x47U
#define TVP5150_OUTPUT_8BIT_422_DISCRETE      0x40U
#define TVP5150_PINS_GLCO_VSYNC_INTREQ_SCLK   0x08U
#define TVP5150_ANALOG_CTRL_AGC_DISABLE       0x14U
#define TVP5150_ANALOG_CTRL_AGC_ENABLE        0x15U

#define TVP5150_I2C_TIMEOUT_MS     10U

volatile uint32_t tvp5150_probe_done_dbg = 0;
volatile uint32_t tvp5150_probe_ok_dbg = 0;
volatile uint32_t tvp5150_probe_addr7_dbg = 0;
volatile uint32_t tvp5150_probe_dev_addr_dbg = 0;
volatile uint32_t tvp5150_probe_hal_status_dbg = 0;
volatile uint32_t tvp5150_probe_error_code_dbg = 0;
volatile uint32_t tvp5150_i2c_found_count_dbg = 0;
volatile uint32_t tvp5150_i2c_found_addr0_dbg = 0;
volatile uint32_t tvp5150_i2c_found_addr1_dbg = 0;
volatile uint32_t tvp5150_i2c_found_addr2_dbg = 0;
volatile uint32_t tvp5150_i2c_found_addr3_dbg = 0;
volatile uint32_t tvp5150_i2c_found_addr4_dbg = 0;
volatile uint32_t tvp5150_i2c_found_addr5_dbg = 0;
volatile uint32_t tvp5150_i2c_found_addr6_dbg = 0;
volatile uint32_t tvp5150_i2c_found_addr7_dbg = 0;
volatile uint32_t tvp5150_id_msb_dbg = 0;
volatile uint32_t tvp5150_id_lsb_dbg = 0;
volatile uint32_t tvp5150_rom_major_dbg = 0;
volatile uint32_t tvp5150_rom_minor_dbg = 0;
volatile uint32_t tvp5150_reg00_dbg = 0;
volatile uint32_t tvp5150_reg01_dbg = 0;
volatile uint32_t tvp5150_reg03_dbg = 0;
volatile uint32_t tvp5150_reg0d_dbg = 0;
volatile uint32_t tvp5150_reg0f_dbg = 0;
volatile uint32_t tvp5150_status1_dbg = 0;
volatile uint32_t tvp5150_status2_dbg = 0;
volatile uint32_t tvp5150_output_mode_dbg = TVP5150_OUTPUT_MODE_DISCRETE_SYNC;
volatile uint32_t tvp5150_agc_enable_dbg = 0U;
volatile uint32_t tvp5150_agc_target_dbg = TVP5150_ANALOG_CTRL_AGC_DISABLE;
volatile uint32_t tvp5150_config_done_dbg = 0;
volatile uint32_t tvp5150_config_ok_dbg = 0;
volatile uint32_t tvp5150_config_addr7_dbg = 0;
volatile uint32_t tvp5150_config_reg01_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp5150_config_reg03_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp5150_config_reg0d_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp5150_config_reg0f_status_dbg = 0xFFFFFFFFU;
volatile uint32_t tvp5150_config_readback_status_dbg = 0xFFFFFFFFU;

static uint8_t tvp5150_found_addrs[8];

static uint16_t tvp5150_dev_addr(uint8_t addr7)
{
    return (uint16_t)addr7 << 1;
}

static HAL_StatusTypeDef tvp5150_read_reg(I2C_HandleTypeDef *hi2c, uint8_t addr7, uint8_t reg, uint8_t *value)
{
    return HAL_I2C_Mem_Read(hi2c,
                            tvp5150_dev_addr(addr7),
                            reg,
                            I2C_MEMADD_SIZE_8BIT,
                            value,
                            1,
                            TVP5150_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef tvp5150_write_reg(I2C_HandleTypeDef *hi2c, uint8_t addr7, uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(hi2c,
                             tvp5150_dev_addr(addr7),
                             reg,
                             I2C_MEMADD_SIZE_8BIT,
                             &value,
                             1,
                             TVP5150_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef tvp5150_configure_422_output(I2C_HandleTypeDef *hi2c, uint8_t addr7)
{
    HAL_StatusTypeDef status;
    uint8_t value;
    uint8_t misc_value = TVP5150_MISC_ENABLE_DATA_CLOCK;
    uint8_t output_value = TVP5150_OUTPUT_8BIT_422_BT656;

    tvp5150_config_done_dbg = 0;
    tvp5150_config_ok_dbg = 0;
    tvp5150_config_addr7_dbg = addr7;

    if (tvp5150_output_mode_dbg == TVP5150_OUTPUT_MODE_DISCRETE_SYNC)
    {
        misc_value = TVP5150_MISC_ENABLE_DATA_CLOCK_SYNCS;
        output_value = TVP5150_OUTPUT_8BIT_422_DISCRETE;
    }

    tvp5150_agc_target_dbg = (tvp5150_agc_enable_dbg != 0U) ?
        TVP5150_ANALOG_CTRL_AGC_ENABLE :
        TVP5150_ANALOG_CTRL_AGC_DISABLE;

    status = tvp5150_write_reg(hi2c, addr7, TVP5150_REG_ANALOG_CTRL, (uint8_t)tvp5150_agc_target_dbg);
    tvp5150_config_reg01_status_dbg = status;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        tvp5150_config_done_dbg = 1U;
        return status;
    }

    status = tvp5150_write_reg(hi2c, addr7, TVP5150_REG_MISC_CTRL, misc_value);
    tvp5150_config_reg03_status_dbg = status;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        tvp5150_config_done_dbg = 1U;
        return status;
    }

    status = tvp5150_write_reg(hi2c, addr7, TVP5150_REG_OUTPUT_FORMAT, output_value);
    tvp5150_config_reg0d_status_dbg = status;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        tvp5150_config_done_dbg = 1U;
        return status;
    }

    status = tvp5150_write_reg(hi2c, addr7, TVP5150_REG_PINS_CONFIG, TVP5150_PINS_GLCO_VSYNC_INTREQ_SCLK);
    tvp5150_config_reg0f_status_dbg = status;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        tvp5150_config_done_dbg = 1U;
        return status;
    }

    value = 0U;
    status = tvp5150_read_reg(hi2c, addr7, TVP5150_REG_ANALOG_CTRL, &value);
    tvp5150_config_readback_status_dbg = status;
    tvp5150_reg01_dbg = value;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        tvp5150_config_done_dbg = 1U;
        return status;
    }

    value = 0U;
    status = tvp5150_read_reg(hi2c, addr7, TVP5150_REG_MISC_CTRL, &value);
    tvp5150_config_readback_status_dbg = status;
    tvp5150_reg03_dbg = value;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        tvp5150_config_done_dbg = 1U;
        return status;
    }

    value = 0U;
    status = tvp5150_read_reg(hi2c, addr7, TVP5150_REG_OUTPUT_FORMAT, &value);
    tvp5150_config_readback_status_dbg = status;
    tvp5150_reg0d_dbg = value;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        tvp5150_config_done_dbg = 1U;
        return status;
    }

    value = 0U;
    status = tvp5150_read_reg(hi2c, addr7, TVP5150_REG_PINS_CONFIG, &value);
    tvp5150_config_readback_status_dbg = status;
    tvp5150_reg0f_dbg = value;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        tvp5150_config_done_dbg = 1U;
        return status;
    }

    tvp5150_config_ok_dbg =
        (((tvp5150_reg01_dbg & 0x03U) == ((tvp5150_agc_enable_dbg != 0U) ? 0x01U : 0x00U)) &&
         (tvp5150_reg03_dbg == misc_value) &&
         (tvp5150_reg0d_dbg == output_value) &&
         (tvp5150_reg0f_dbg == TVP5150_PINS_GLCO_VSYNC_INTREQ_SCLK)) ? 1U : 0U;
    tvp5150_config_done_dbg = 1U;

    return status;
}

static void tvp5150_store_found_addr(uint8_t addr7)
{
    uint32_t index = tvp5150_i2c_found_count_dbg;

    if (index < 8U)
    {
        tvp5150_found_addrs[index] = addr7;

        switch (index)
        {
        case 0U: tvp5150_i2c_found_addr0_dbg = addr7; break;
        case 1U: tvp5150_i2c_found_addr1_dbg = addr7; break;
        case 2U: tvp5150_i2c_found_addr2_dbg = addr7; break;
        case 3U: tvp5150_i2c_found_addr3_dbg = addr7; break;
        case 4U: tvp5150_i2c_found_addr4_dbg = addr7; break;
        case 5U: tvp5150_i2c_found_addr5_dbg = addr7; break;
        case 6U: tvp5150_i2c_found_addr6_dbg = addr7; break;
        default: tvp5150_i2c_found_addr7_dbg = addr7; break;
        }
    }

    tvp5150_i2c_found_count_dbg++;
}

static HAL_StatusTypeDef tvp5150_try_read_id(I2C_HandleTypeDef *hi2c, uint8_t addr7)
{
    HAL_StatusTypeDef status;
    uint8_t value;

    tvp5150_probe_addr7_dbg = addr7;
    tvp5150_probe_dev_addr_dbg = tvp5150_dev_addr(addr7);
    tvp5150_probe_error_code_dbg = 0;

    status = tvp5150_read_reg(hi2c, addr7, TVP5150_REG_DEVICE_ID_MSB, &value);
    tvp5150_probe_hal_status_dbg = status;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        return status;
    }
    tvp5150_id_msb_dbg = value;

    status = tvp5150_read_reg(hi2c, addr7, TVP5150_REG_DEVICE_ID_LSB, &value);
    tvp5150_probe_hal_status_dbg = status;
    if (status != HAL_OK)
    {
        tvp5150_probe_error_code_dbg = HAL_I2C_GetError(hi2c);
        return status;
    }
    tvp5150_id_lsb_dbg = value;

    value = 0;
    (void)tvp5150_read_reg(hi2c, addr7, TVP5150_REG_ROM_MAJOR, &value);
    tvp5150_rom_major_dbg = value;
    value = 0;
    (void)tvp5150_read_reg(hi2c, addr7, TVP5150_REG_ROM_MINOR, &value);
    tvp5150_rom_minor_dbg = value;
    value = 0;
    (void)tvp5150_read_reg(hi2c, addr7, TVP5150_REG_INPUT_SEL, &value);
    tvp5150_reg00_dbg = value;
    value = 0;
    (void)tvp5150_read_reg(hi2c, addr7, TVP5150_REG_ANALOG_CTRL, &value);
    tvp5150_reg01_dbg = value;
    value = 0;
    (void)tvp5150_read_reg(hi2c, addr7, TVP5150_REG_MISC_CTRL, &value);
    tvp5150_reg03_dbg = value;
    value = 0;
    (void)tvp5150_read_reg(hi2c, addr7, TVP5150_REG_OUTPUT_FORMAT, &value);
    tvp5150_reg0d_dbg = value;
    value = 0;
    (void)tvp5150_read_reg(hi2c, addr7, TVP5150_REG_PINS_CONFIG, &value);
    tvp5150_reg0f_dbg = value;
    value = 0;
    (void)tvp5150_read_reg(hi2c, addr7, TVP5150_REG_STATUS1, &value);
    tvp5150_status1_dbg = value;
    value = 0;
    (void)tvp5150_read_reg(hi2c, addr7, TVP5150_REG_STATUS2, &value);
    tvp5150_status2_dbg = value;

    tvp5150_probe_ok_dbg =
        ((tvp5150_id_msb_dbg == 0x51U) && (tvp5150_id_lsb_dbg == 0x50U)) ? 1U : 0U;

    return HAL_OK;
}

void TVP5150_I2CProbe_Run(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef status;

    tvp5150_probe_done_dbg = 0;
    tvp5150_probe_ok_dbg = 0;
    tvp5150_probe_addr7_dbg = 0;
    tvp5150_probe_dev_addr_dbg = 0;
    tvp5150_probe_hal_status_dbg = 0;
    tvp5150_probe_error_code_dbg = 0;
    tvp5150_i2c_found_count_dbg = 0;
    tvp5150_i2c_found_addr0_dbg = 0;
    tvp5150_i2c_found_addr1_dbg = 0;
    tvp5150_i2c_found_addr2_dbg = 0;
    tvp5150_i2c_found_addr3_dbg = 0;
    tvp5150_i2c_found_addr4_dbg = 0;
    tvp5150_i2c_found_addr5_dbg = 0;
    tvp5150_i2c_found_addr6_dbg = 0;
    tvp5150_i2c_found_addr7_dbg = 0;
    tvp5150_id_msb_dbg = 0;
    tvp5150_id_lsb_dbg = 0;
    tvp5150_rom_major_dbg = 0;
    tvp5150_rom_minor_dbg = 0;
    tvp5150_reg00_dbg = 0;
    tvp5150_reg01_dbg = 0;
    tvp5150_reg03_dbg = 0;
    tvp5150_reg0d_dbg = 0;
    tvp5150_reg0f_dbg = 0;
    tvp5150_status1_dbg = 0;
    tvp5150_status2_dbg = 0;
    tvp5150_config_done_dbg = 0;
    tvp5150_config_ok_dbg = 0;
    tvp5150_config_addr7_dbg = 0;
    tvp5150_config_reg01_status_dbg = 0xFFFFFFFFU;
    tvp5150_config_reg03_status_dbg = 0xFFFFFFFFU;
    tvp5150_config_reg0d_status_dbg = 0xFFFFFFFFU;
    tvp5150_config_reg0f_status_dbg = 0xFFFFFFFFU;
    tvp5150_config_readback_status_dbg = 0xFFFFFFFFU;

    for (uint8_t addr7 = 0x03U; addr7 <= 0x77U; addr7++)
    {
        status = HAL_I2C_IsDeviceReady(hi2c, tvp5150_dev_addr(addr7), 2, TVP5150_I2C_TIMEOUT_MS);
        if (status == HAL_OK)
        {
            tvp5150_store_found_addr(addr7);
        }
    }

    status = tvp5150_try_read_id(hi2c, TVP5150_I2C_ADDR0_7BIT);
    if ((status != HAL_OK) || (tvp5150_probe_ok_dbg == 0U))
    {
        status = tvp5150_try_read_id(hi2c, TVP5150_I2C_ADDR1_7BIT);
    }

    if ((status != HAL_OK) || (tvp5150_probe_ok_dbg == 0U))
    {
        uint32_t found_to_try = tvp5150_i2c_found_count_dbg;
        if (found_to_try > 8U)
        {
            found_to_try = 8U;
        }

        for (uint32_t i = 0; i < found_to_try; i++)
        {
            status = tvp5150_try_read_id(hi2c, tvp5150_found_addrs[i]);
            if ((status == HAL_OK) && (tvp5150_probe_ok_dbg != 0U))
            {
                break;
            }
        }
    }

    if ((status == HAL_OK) && (tvp5150_probe_ok_dbg != 0U))
    {
        status = tvp5150_configure_422_output(hi2c, (uint8_t)tvp5150_probe_addr7_dbg);
    }

    tvp5150_probe_done_dbg = 1;
}
