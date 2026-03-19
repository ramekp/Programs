/*
 * adxl345.c
 *
 *  Created on: Feb 17, 2026
 *      Author: ramekp
 */


/**
 * @file adxl345.c
 * @brief ADXL345 accelerometer driver using SPI
 */

#include "adxl345.h"
static SPI_HandleTypeDef *adxl_spi;

#define ADXL_CS_PORT   GPIOD
#define ADXL_CS_PIN    GPIO_PIN_1

static inline void CS_Low(void) {
    HAL_GPIO_WritePin(ADXL_CS_PORT, ADXL_CS_PIN, GPIO_PIN_RESET);
}

static inline void CS_High(void) {
    HAL_GPIO_WritePin(ADXL_CS_PORT, ADXL_CS_PIN, GPIO_PIN_SET);
}

static HAL_StatusTypeDef adxl_write(uint8_t reg, uint8_t value) {
    uint8_t tx[2] = { reg, value };
    CS_Low();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(adxl_spi, tx, 2, HAL_MAX_DELAY);
    CS_High();
    return st;
}

static HAL_StatusTypeDef adxl_read(uint8_t reg, uint8_t *value) {
    uint8_t cmd = 0x80 | reg;
    CS_Low();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(adxl_spi, &cmd, 1, HAL_MAX_DELAY);
    st |= HAL_SPI_Receive(adxl_spi, value, 1, HAL_MAX_DELAY);
    CS_High();
    return st;
}

static HAL_StatusTypeDef adxl_read_multi(uint8_t reg, uint8_t *buffer, uint8_t len) {
    uint8_t cmd = 0x80 | 0x40 | reg;
    CS_Low();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(adxl_spi, &cmd, 1, HAL_MAX_DELAY);
    st |= HAL_SPI_Receive(adxl_spi, buffer, len, HAL_MAX_DELAY);
    CS_High();
    return st;
}

HAL_StatusTypeDef ADXL345_Init(SPI_HandleTypeDef *spi) {
    adxl_spi = spi;
    CS_High();
    HAL_Delay(10);

    uint8_t id = 0;
    adxl_read(ADXL_REG_DEVID, &id);

    if (id != ADXL_DEVID_EXPECTED)
        return HAL_ERROR;

    adxl_write(ADXL_REG_BW_RATE, ADXL_BW_100HZ);
    adxl_write(ADXL_REG_DATA_FORMAT, ADXL_FULL_RES_2G);
    adxl_write(ADXL_REG_POWER_CTL, ADXL_POWER_MEASURE);

    return HAL_OK;
}

HAL_StatusTypeDef ADXL345_ReadXYZ(int16_t *x, int16_t *y, int16_t *z) {
    uint8_t data[6];
    if (adxl_read_multi(ADXL_REG_DATAX0, data, 6) != HAL_OK)
        return HAL_ERROR;

    *x = (int16_t)((data[1] << 8) | data[0]);
    *y = (int16_t)((data[3] << 8) | data[2]);
    *z = (int16_t)((data[5] << 8) | data[4]);

    return HAL_OK;
}

