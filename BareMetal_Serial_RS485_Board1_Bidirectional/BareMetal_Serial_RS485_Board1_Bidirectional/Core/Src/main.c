#include "main.h"
#include <string.h>
#include <stdbool.h>

UART_HandleTypeDef huart4;

void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv              = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM            = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN            = 8;
  RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;
  RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);
  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

static void MX_USART4_UART_Init(void) {
  huart4.Instance            = USART4;
  huart4.Init.BaudRate       = 115200;
  huart4.Init.WordLength     = UART_WORDLENGTH_8B;
  huart4.Init.StopBits       = UART_STOPBITS_1;
  huart4.Init.Parity         = UART_PARITY_NONE;
  huart4.Init.Mode           = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling   = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  HAL_UART_Init(&huart4);
}

static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin   = GPIO_PIN_5;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void led_task_500ms(void) {
  static uint32_t last = 0;
  uint32_t now = HAL_GetTick();
  if ((now - last) >= 500) {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    last = now;
  }
}

static bool uart_get_byte(UART_HandleTypeDef *huart, uint8_t *b) {
  if (HAL_UART_Receive(huart, b, 1, 10) == HAL_OK) {
    return true;
  }
  led_task_500ms();
  return false;
}

static bool UART_WaitFor_Frame_StarDigitHash(UART_HandleTypeDef *huart, uint8_t *out_digit) {
  enum { S_WAIT_STAR, S_GET_DIGIT, S_WAIT_HASH, S_EAT_EOL } state = S_WAIT_STAR;
  uint8_t ch = 0;
  for (;;) {
    if (!uart_get_byte(huart, &ch)) {
      continue;
    }
    switch (state)
    {
      case S_WAIT_STAR:
        if (ch == '*') {
          state = S_GET_DIGIT;
        }
        break;
      case S_GET_DIGIT:
        if (ch >= '0' && ch <= '9') {
          *out_digit = (uint8_t)(ch - '0');
          state = S_WAIT_HASH;
        }
        else {
          state = S_WAIT_STAR;
          if (ch == '*') {
            state = S_GET_DIGIT;
          }
        }
        break;
      case S_WAIT_HASH:
        if (ch == '#') {
          state = S_EAT_EOL;
        }
        else {
          state = (ch == '*') ? S_GET_DIGIT : S_WAIT_STAR;
        }
        break;
      case S_EAT_EOL:
    	HAL_Delay(100);
        if (ch == '\r' || ch == '\n') {
          uint8_t c2;
          HAL_UART_Receive(huart, &c2, 1, 0);
        }
        return true;
    }
    led_task_500ms();
  }
}

int main(void) {
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART4_UART_Init();
  static const char wel_msg[] = "BOARD 1\r\n";
  HAL_UART_Transmit(&huart4, (uint8_t*)wel_msg, (uint16_t)strlen(wel_msg), 100U);
  const char *tx_frames[]   = { "*0#\r\n", "*2#\r\n", "*4#\r\n", "*6#\r\n", "*8#\r\n" };
  const uint8_t expect_digits[] = { 1, 3, 5, 7, 9 };
  uint8_t idx = 0;
  bool pending_tx = true;
  for (;;) {
    if (pending_tx) {
      const char *s = tx_frames[idx];
      HAL_UART_Transmit(&huart4, (uint8_t*)s, (uint16_t)strlen(s), 20);
      pending_tx = false;
    }
    uint8_t rx_digit = 0xFF;
    if (UART_WaitFor_Frame_StarDigitHash(&huart4, &rx_digit)) {
      if (rx_digit == expect_digits[idx]) {
        idx = (uint8_t)((idx + 1) % 5);
        pending_tx = true;
      }
    }
    led_task_500ms();
  }
}

void Error_Handler(void) {
  __disable_irq();
  while (1) {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    HAL_Delay(100);
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {
  (void)file;
  (void)line;
}
#endif
