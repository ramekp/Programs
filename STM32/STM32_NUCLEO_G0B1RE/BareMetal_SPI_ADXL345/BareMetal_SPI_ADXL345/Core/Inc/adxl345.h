/*
 * adxl345.h
 *
 *  Created on: Feb 17, 2026
 *      Author: ramekp
 */

#ifndef INC_ADXL345_H_
#define INC_ADXL345_H_


#include "main.h"

/* Register addresses */
#define ADXL_REG_DEVID        0x00
#define ADXL_REG_BW_RATE      0x2C
#define ADXL_REG_POWER_CTL    0x2D
#define ADXL_REG_DATA_FORMAT  0x31
#define ADXL_REG_DATAX0       0x32

/* Register values */
#define ADXL_DEVID_EXPECTED   0xE5
#define ADXL_POWER_MEASURE    0x08
#define ADXL_BW_100HZ         0x0A
#define ADXL_FULL_RES_2G      0x08

/* Public functions */
HAL_StatusTypeDef ADXL345_Init(SPI_HandleTypeDef *spi);
HAL_StatusTypeDef ADXL345_ReadXYZ(int16_t *x, int16_t *y, int16_t *z);


#endif /* INC_ADXL345_H_ */
