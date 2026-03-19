#include "adxl345.h"
#include "gpio.h"
#include "spi.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

int16_t x, y, z;

void SystemClock_Config(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
	RCC_OscInitStruct.PLL.PLLN = 8;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
	RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
	RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		Error_Handler();
	}
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
		Error_Handler();
	}
}

uint8_t Uart2Transmit(const char *message, uint32_t period_ms) {
    const uint32_t UART_TIMEOUT = 100U;
    static uint32_t last_tick_time = 0;
    uint32_t current_tick = HAL_GetTick();
    if (period_ms == 0 || (uint32_t)(current_tick - last_tick_time) >= period_ms) {
    	HAL_UART_Transmit(&huart2, (uint8_t*)message, (uint16_t)strlen(message), UART_TIMEOUT);
    	if (period_ms > 0)
            last_tick_time = current_tick;
        return 1;
    }
    return 0;
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_USART2_UART_Init();

    if (ADXL345_Init(&hspi1) != HAL_OK)
    {
        Uart2Transmit("ADXL345 not detected!\r\n", 0);
        while (1);
    }

    Uart2Transmit("ADXL345 Ready!\r\n", 0);

    while (1)
    {
        if (ADXL345_ReadXYZ(&x, &y, &z) == HAL_OK)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "X:%d Y:%d Z:%d\r\n", x, y, z);
            Uart2Transmit(msg, 500);
        }

        HAL_Delay(50);
    }
}
void Error_Handler(void) {
    while (1);
}
