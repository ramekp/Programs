/**
 * @file    main.c
 * @brief   UART4 RX (interrupt-driven) command parser with UART2 TX logging
 *          and periodic LED toggle using HAL_GetTick().
 *
 * Functionality:
 *  - System clock from HSI (no PLL).
 *  - LED toggles every 500 ms (non-blocking).
 *  - UART2 (115200-8N1): used to transmit responses/logs (blocking).
 *  - UART4 (115200-8N1): receives bytes via interrupt; accumulates into a line buffer.
 *    When CR/LF is received, compares against "hi" and, if matched, prints "ok\r\n" on UART2.
 *
 * Notes:
 *  - rx4_buffer is 20 bytes; input longer than 19 chars before newline will be truncated.
 *  - Ensure correct GPIO AF mapping for both USART2 and USART4 in CubeMX or board init.
 */

#include "main.h"     // HAL and board-specific declarations (GPIO ports/pins, handles)
#include <string.h>   // For strlen(), strcmp(), memset()

/** @brief Timestamp (ms) of last LED toggle driven by HAL_GetTick(). */
uint32_t lastBlink = 0;

/** @brief One-byte RX storage for UART4 ISR-driven reception. */
uint8_t rx4_byte;

/** @brief Line buffer to assemble a message from UART4 until CR/LF. */
char rx4_buffer[20];

/** @brief Index into rx4_buffer for next write position (ISR updates). */
volatile uint8_t rx4_index = 0;

/** @brief Flag set when a complete message (CR/LF terminated) is ready to process. */
volatile uint8_t msg_ready = 0;

/** @brief UART2 handle (logging TX). */
UART_HandleTypeDef huart2;

/** @brief UART4 handle (RX interrupt input). */
UART_HandleTypeDef huart4;

/**
 * @brief  Configure system clock: HSI on, no PLL, SYSCLK = HSI, AHB=1, APB1=1.
 * @note   FLASH latency 0 is used (suitable for low HSI frequencies).
 * @retval None (calls Error_Handler on failure).
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};   // Zero-initialize oscillator config
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};   // Zero-initialize clock config

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1); // Voltage scaling Range 1

  // --- Oscillator configuration: enable HSI, default trim, no PLL ---
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;      // Use HSI oscillator
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;                        // Turn HSI on
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;                        // No division
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT; // Default calibration
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;                  // Disable PLL
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {          // Apply oscillator config
    Error_Handler();                                              // Trap on error
  }

  // --- Clock tree: SYSCLK=HSI, AHB=1, APB1=1 ---
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK               // Configure HCLK
                              | RCC_CLOCKTYPE_SYSCLK             // Configure SYSCLK
                              | RCC_CLOCKTYPE_PCLK1;             // Configure PCLK1
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;         // SYSCLK source = HSI
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;             // HCLK = SYSCLK/1
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;              // PCLK1 = HCLK/1
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) { // Apply
    Error_Handler();                                              // Trap on error
  }
}

/**
 * @brief  Initialize GPIO: LED output, user button as rising-edge interrupt.
 * @note   Ensure macros LED_GREEN_GPIO_Port/Pin and B1_Pin/B1_GPIO_Port match board.
 * @retval None
 */
static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};            // Zero-initialize GPIO config

  __HAL_RCC_GPIOC_CLK_ENABLE();                      // Enable GPIOC clock (for B1 on some boards)
  __HAL_RCC_GPIOF_CLK_ENABLE();                      // Enable GPIOF clock (if used)
  __HAL_RCC_GPIOA_CLK_ENABLE();                      // Enable GPIOA clock (LED/USART)

  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port,             // Set LED initial state to LOW (off)
                    LED_GREEN_Pin, GPIO_PIN_RESET);

  // --- Configure User Button B1: rising-edge interrupt, no pull ---
  GPIO_InitStruct.Pin = B1_Pin;                      // Button pin
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;        // Interrupt on rising edge
  GPIO_InitStruct.Pull = GPIO_NOPULL;                // No pull-up/down
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);     // Apply configuration

  // --- Configure LED as push-pull output, high speed, no pull ---
  GPIO_InitStruct.Pin = LED_GREEN_Pin;               // LED pin
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;        // Push-pull
  GPIO_InitStruct.Pull = GPIO_NOPULL;                // No pull
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      // High speed toggling
  HAL_GPIO_Init(LED_GREEN_GPIO_Port, &GPIO_InitStruct); // Apply configuration
}

/**
 * @brief  Initialize USART2 @ 115200-8N1, no flow control, FIFOs disabled.
 * @note   Used here as a TX/logging port (blocking writes).
 * @retval None (calls Error_Handler on failure).
 */
static void MX_USART2_UART_Init(void) {
  huart2.Instance = USART2;                                 // Select USART2
  huart2.Init.BaudRate = 115200;                            // 115200 bps
  huart2.Init.WordLength = UART_WORDLENGTH_8B;              // 8 data bits
  huart2.Init.StopBits = UART_STOPBITS_1;                   // 1 stop bit
  huart2.Init.Parity = UART_PARITY_NONE;                    // No parity
  huart2.Init.Mode = UART_MODE_TX_RX;                       // Enable TX/RX
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;              // No RTS/CTS
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;          // Oversampling x16
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE; // No 1-bit sampling
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;         // No prescaler
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT; // No advanced features
  if (HAL_UART_Init(&huart2) != HAL_OK) {                   // Initialize UART
    Error_Handler();                                        // Trap on error
  }

  // Configure FIFO thresholds (if supported) then disable FIFO mode to use legacy behavior
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK) {      // Disable FIFO mode
    Error_Handler();
  }
}

/**
 * @brief  Initialize USART4 @ 115200-8N1, no flow control.
 * @note   Used as RX input with interrupt-based reception.
 * @retval None (calls Error_Handler on failure).
 */
static void MX_USART4_UART_Init(void) {
  huart4.Instance = USART4;                                 // Select USART4
  huart4.Init.BaudRate = 115200;                            // 115200 bps
  huart4.Init.WordLength = UART_WORDLENGTH_8B;              // 8 data bits
  huart4.Init.StopBits = UART_STOPBITS_1;                   // 1 stop bit
  huart4.Init.Parity = UART_PARITY_NONE;                    // No parity
  huart4.Init.Mode = UART_MODE_TX_RX;                       // Enable TX/RX
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;              // No RTS/CTS
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;          // Oversampling x16
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE; // No 1-bit sampling
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;         // No prescaler
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT; // No advanced features
  if (HAL_UART_Init(&huart4) != HAL_OK) {                   // Initialize UART
    Error_Handler();                                        // Trap on error
  }
}

/**
 * @brief  Periodically toggles LED every 500 ms based on current tick.
 * @param  now        Current time in ms (HAL_GetTick()).
 * @param  pLastBlink Pointer to state storing last toggle timestamp; updated on toggle.
 * @note   Toggles PA5 directly; change to LED_GREEN_* for portability if needed.
 */
static inline void LedToggleTask(uint32_t now, uint32_t *pLastBlink) {
  if ((uint32_t)(now - *pLastBlink) >= 500U) {  // Check if 500 ms elapsed
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);      // Toggle LED on PA5
    *pLastBlink = now;                          // Update last toggle time
  }
}

/**
 * @brief  Program entry point.
 * @retval int Not used (superloop).
 */
int main(void) {
  HAL_Init();                                   // Initialize HAL and SysTick
  SystemClock_Config();                         // Configure clocks (HSI)
  MX_GPIO_Init();                               // Initialize LED and button GPIO
  MX_USART2_UART_Init();                        // Init UART2 for TX/logging
  MX_USART4_UART_Init();                        // Init UART4 for RX (interrupt)

  char response[] = "Welcome111\r\n";           // Startup banner for UART2
  HAL_UART_Transmit(&huart2,                    // Transmit banner (blocking)
                    (uint8_t *)response,
                    strlen(response),
                    HAL_MAX_DELAY);

  HAL_UART_Receive_IT(&huart4, &rx4_byte, 1);   // Arm first RX interrupt for UART4

  // --- Main loop ---
  while (1) {
    uint32_t now = HAL_GetTick();               // Read current tick (ms)
    LedToggleTask(now, &lastBlink);             // Service LED periodic toggle

    if (msg_ready) {                            // If a full line message has arrived
      msg_ready = 0;                            // Clear flag before processing

      // Compare received text to "hi" (exact match)
      if (strcmp(rx4_buffer, "hi") == 0) {
        char response[] = "ok\r\n";             // Prepare OK response
        HAL_UART_Transmit(&huart2,              // Send on UART2 (blocking)
                          (uint8_t *)response,
                          strlen(response),
                          HAL_MAX_DELAY);
      }

      memset(rx4_buffer, 0, sizeof(rx4_buffer)); // Clear buffer for next message
    }
  }
}

/**
 * @brief  HAL callback invoked on UART RX complete (interrupt context).
 * @param  huart Pointer to UART handle that triggered the callback.
 * @note   Accumulates bytes for UART4, terminates on CR/LF, sets msg_ready.
 *         Re-arms the next 1-byte receive interrupt at the end.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART4) {                // Only handle UART4 bytes
    if (rx4_byte == '\r' || rx4_byte == '\n') {   // If line terminator received
      rx4_buffer[rx4_index] = '\0';               // Null-terminate the string
      rx4_index = 0;                               // Reset index for next message
      msg_ready = 1;                               // Signal main loop to process
    } else {
      // Store byte if space remains (leave room for '\0')
      if (rx4_index < sizeof(rx4_buffer) - 1) {
        rx4_buffer[rx4_index++] = rx4_byte;        // Append to buffer and advance index
      }
      // If buffer is full, extra bytes are dropped until newline; could add an overflow flag
    }

    HAL_UART_Receive_IT(&huart4, &rx4_byte, 1);    // Re-arm next 1-byte receive
  }
}

/**
 * @brief  Generic error handler: disable IRQs and loop forever.
 * @note   Consider adding debug indication (LED pattern) or watchdog reset behavior.
 */
void Error_Handler(void) {
  __disable_irq();           // Globally disable interrupts
  while (1) {                // Trap CPU
    // Optional: blink LED to indicate fault
  }
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the file name and the line number where assert_param error has occurred.
 * @param  file Pointer to the source file name.
 * @param  line Line number where assert failed.
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line) {
  // Optionally log file/line via UART2 for diagnostics
  (void)file;
  (void)line;
}
#endif
