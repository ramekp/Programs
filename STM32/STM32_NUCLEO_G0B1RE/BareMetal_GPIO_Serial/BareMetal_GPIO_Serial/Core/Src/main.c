/**
 * @file main.c
 * @brief Bare-metal STM32G0 example: GPIO LED toggle and UART periodic transmit + echo reception using HAL.
 *
 * This application demonstrates:
 *  - Basic system clock configuration using the internal HSI clock.
 *  - GPIO initialization to control an LED and configure a user button (B1) with interrupt.
 *  - UART2 initialization (115200-8N1) for simple transmit/receive operations.
 *  - Cooperative timing using HAL_GetTick() to toggle an LED every 500 ms and
 *    transmit a status message over UART every 1000 ms.
 *  - **Interrupt-driven UART reception** that echoes characters as you type and
 *    prints the accumulated line when Enter is pressed.
 *
 * Key design notes:
 *  - The timing logic uses unsigned 32-bit tick arithmetic in a wraparound-safe manner:
 *      (now - lastX) >= interval
 *  - UART transmission uses blocking HAL_UART_Transmit for simplicity.
 *  - FIFO is explicitly disabled after setting thresholds to a known state.
 *  - GPIO toggling uses a hard-coded pin (GPIOA, PIN_5); ensure this matches LED_GREEN_Pin.
 *
 * Dependencies:
 *  - Cube HAL drivers for STM32G0.
 *  - main.h must define LED_GREEN_GPIO_Port, LED_GREEN_Pin, B1_Pin, B1_GPIO_Port, etc.
 *  - stm32g0xx_it.c must route USART2 IRQ to HAL (USART2_IRQHandler -> HAL_UART_IRQHandler(&huart2)).
 *
 * Safety and robustness:
 *  - Error_Handler() traps the CPU (with IRQs disabled) on configuration failure.
 *  - No dynamic memory allocation is used.
 */

#include "main.h"
#include <string.h>   // Required for strlen() used in UART transmission

/** UART2 handle (global) used by HAL drivers */
UART_HandleTypeDef huart2;

/** Last tick at which the LED was toggled (in ms, from HAL_GetTick) */
uint32_t lastBlink = 0;

/** Last tick at which a UART message was sent (in ms, from HAL_GetTick) */
uint32_t lastUart  = 0;

/* ===================== Added: UART RX state ===================== */
/** Single-byte buffer for interrupt-driven reception */
static uint8_t uart2_rx_byte = 0;
/** Line buffer to accumulate received characters until newline */
static char     uart2_rx_line[128];
/** Current write position in line buffer */
static volatile uint16_t uart2_rx_pos = 0;
/* ================================================================ */

/**
 * @brief Configure system clocks and power scaling.
 *
 * This function:
 *  - Sets voltage scaling to Scale 1 (for performance).
 *  - Enables and configures the HSI oscillator as the system clock source.
 *  - Disables PLL (direct HSI usage).
 *  - Sets bus clock dividers (AHB, APB1) to no division.
 *  - Applies zero wait states (FLASH_LATENCY_0), appropriate for the current frequency.
 *
 * Any failure in clock configuration calls Error_Handler().
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};  // Structure to configure oscillators
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};  // Structure to configure clocks

  // Configure internal voltage scaling to Scale 1 for maximum performance
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  // Enable HSI (High-Speed Internal) oscillator and configure its parameters
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;                // Turn HSI on
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;                // No division on HSI
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT; // Factory calibration
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;          // No PLL used

  // Apply oscillator configuration; on failure, trap in Error_Handler
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  // Configure bus clocks and system clock source
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;  // System clock sourced from HSI
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;      // HCLK = SYSCLK
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;       // PCLK1 = HCLK

  // Apply clock configuration; FLASH latency 0 for this frequency
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * @brief Initialize GPIOs for LED and user button (B1).
 *
 * - Enables clocks for GPIO ports: C, F, A.
 * - Sets LED pin (LED_GREEN_Pin) as push-pull output, no pull, high speed.
 * - Writes LED low initially to ensure a known state.
 * - Configures B1 pin as external interrupt on rising edge, no pull.
 *
 * Note: The EXTI line and NVIC configuration for B1 interrupt (if used) should be
 * performed elsewhere if the interrupt is needed beyond configuration.
 */
static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};  // Structure for GPIO configuration

  // Enable GPIO peripheral clocks needed for LED and B1 button
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  // Initialize LED to a known OFF state
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);

  // Configure the user button B1 as an interrupt input (rising edge trigger)
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;  // Interrupt on rising edge
  GPIO_InitStruct.Pull = GPIO_NOPULL;          // No internal pull-up/down
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  // Configure the LED pin as push-pull output, high speed, no pull
  GPIO_InitStruct.Pin = LED_GREEN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;      // Push-pull output
  GPIO_InitStruct.Pull = GPIO_NOPULL;              // No internal pull-up/down
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;    // High speed for fast toggling
  HAL_GPIO_Init(LED_GREEN_GPIO_Port, &GPIO_InitStruct);
}

/**
 * @brief Initialize USART2 for 115200-8N1, TX/RX, no flow control.
 *
 * Settings:
 *  - BaudRate: 115200
 *  - WordLength: 8 bits
 *  - StopBits: 1
 *  - Parity: None
 *  - Mode: TX and RX enabled
 *  - Oversampling: 16
 *  - One-bit sampling: disabled
 *  - Clock prescaler: DIV1
 *  - Advanced features: none
 *
 * The function also sets FIFO thresholds, then disables FIFO mode explicitly,
 * keeping the peripheral in a predictable configuration. Any init error
 * results in Error_Handler().
 *
 * **Starts interrupt-driven reception (1 byte) to enable echo.**
 */
static void MX_USART2_UART_Init(void) {
  // Select USART2 instance and apply standard UART parameters
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;                         // UART speed
  huart2.Init.WordLength = UART_WORDLENGTH_8B;           // 8 data bits
  huart2.Init.StopBits = UART_STOPBITS_1;                // 1 stop bit
  huart2.Init.Parity = UART_PARITY_NONE;                 // No parity
  huart2.Init.Mode = UART_MODE_TX_RX;                    // Enable both TX and RX
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;           // No hardware flow control
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;       // Standard oversampling
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE; // Disable one-bit sampling
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;      // No prescaler
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT; // No advanced features

  // Initialize UART; on failure, trap in Error_Handler
  if (HAL_UART_Init(&huart2) != HAL_OK) {
    Error_Handler();
  }

  // Configure FIFO thresholds (even though FIFO will be disabled afterward)
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) {
    Error_Handler();
  }

  // Disable FIFO mode to operate in non-FIFO (legacy) mode
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK) {
    Error_Handler();
  }

  /* ===================== Added: Start RX in IT mode ===================== */
  if (HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1) != HAL_OK) {
    Error_Handler();
  }
  /* ===================================================================== */
}

/**
 * @brief Periodic LED toggle task (500 ms).
 * @param now        Current tick in milliseconds (HAL_GetTick()).
 * @param pLastBlink Pointer to the last toggle timestamp; updated when toggled.
 *
 * Behavior: Toggles LED on GPIOA, PIN_5 every 500 ms using wraparound-safe timing.
 */
static inline void LedToggleTask(uint32_t now, uint32_t *pLastBlink) {
  // Check if 500 ms have elapsed since last LED toggle (wraparound-safe)
  if ((uint32_t)(now - *pLastBlink) >= 500U) {
    // Toggle LED (ensure PA5 is your LED; otherwise use LED_GREEN_GPIO_Port/Pin)
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    // Update last toggle timestamp
    *pLastBlink = now;
  }
}

/**
 * @brief Periodic UART transmit task (1000 ms).
 * @param now        Current tick in milliseconds (HAL_GetTick()).
 * @param pLastUart  Pointer to the last transmit timestamp; updated when sent.
 * @param msg        Null-terminated C string to transmit (without the '\0').
 *
 * Behavior: Sends msg once every 1000 ms in blocking mode with 100 ms timeout.
 */
static inline void SerialTransmitTask(uint32_t now, uint32_t *pLastUart, const char *msg) {
  // Check if 1000 ms have elapsed since last UART transmit (wraparound-safe)
  if ((uint32_t)(now - *pLastUart) >= 1000U) {
    // Transmit message (not including the string's terminating null character)
    HAL_UART_Transmit(&huart2, (const uint8_t *)msg, (uint16_t)strlen(msg), 100U);
    // Update last transmit timestamp
    *pLastUart = now;
  }
}

/* ===================== Added: UART callbacks ===================== */
/**
 * @brief UART RX complete callback: echoes typed characters and prints the line on Enter.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART2) {
    uint8_t c = uart2_rx_byte;

    // Local echo of the received character (so terminal shows what you type)
    HAL_UART_Transmit(&huart2, &c, 1, 100U);

    // If newline received, print the accumulated line as a message
    if (c == '\r' || c == '\n') {
      if (uart2_rx_pos > 0U) {
        // Terminate the collected line
        uart2_rx_line[uart2_rx_pos] = '\0';
        // Print CRLF + the exact string the user typed
        const char *prefix = "\r\nRX: ";
        HAL_UART_Transmit(&huart2, (uint8_t *)prefix, (uint16_t)strlen(prefix), 100U);
        HAL_UART_Transmit(&huart2, (uint8_t *)uart2_rx_line, (uint16_t)uart2_rx_pos, 100U);
        HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2U, 100U);
        uart2_rx_pos = 0U;
      } else {
        // If newline without buffered chars, still print CRLF to keep prompt clean
        HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2U, 100U);
      }
    } else {
      // Append character to line buffer if space available
      if (uart2_rx_pos < (sizeof(uart2_rx_line) - 1U)) {
        uart2_rx_line[uart2_rx_pos++] = (char)c;
      }
      // If buffer full, flush as a line to avoid overflow
      else {
        uart2_rx_line[uart2_rx_pos] = '\0';
        const char *prefix = "\r\nRX: ";
        HAL_UART_Transmit(&huart2, (uint8_t *)prefix, (uint16_t)strlen(prefix), 100U);
        HAL_UART_Transmit(&huart2, (uint8_t *)uart2_rx_line, (uint16_t)uart2_rx_pos, 100U);
        HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2U, 100U);
        uart2_rx_pos = 0U;
      }
    }

    // Restart interrupt reception for the next byte
    (void)HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1);
  }
}

/**
 * @brief UART error callback: clears state (if needed) and restarts reception.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART2) {
    // In most cases simply restarting RX is sufficient; HAL clears error flags
    (void)HAL_UART_Receive_IT(&huart2, &uart2_rx_byte, 1);
  }
}
/* ================================================================= */

/**
 * @brief Application entry point.
 *
 * Initialization sequence:
 *  - HAL_Init(): Initializes the HAL library and SysTick.
 *  - SystemClock_Config(): Configures system clock to use HSI.
 *  - MX_GPIO_Init(): Sets up GPIO pins for LED and button.
 *  - MX_USART2_UART_Init(): Configures UART2 for serial communication.
 *
 * Main loop:
 *  - Uses HAL_GetTick() (ms) to implement two periodic tasks:
 *      * Toggle LED every 500 ms.
 *      * Transmit a greeting over UART every 1000 ms.
 *  - Timing uses wraparound-safe subtraction on unsigned 32-bit ticks.
 */
int main(void) {
  // Initialize HAL, which sets up SysTick and low-level drivers
  HAL_Init();

  // Configure clocks and power
  SystemClock_Config();

  // Initialize GPIO (LED, button) and UART2
  MX_GPIO_Init();
  MX_USART2_UART_Init();

  // Superloop: cooperative scheduling based on HAL_GetTick()
  while (1) {
    // Read current tick count (milliseconds since startup)
    uint32_t now = HAL_GetTick();
    const char *msgPtr = "Fourth Check ..........\r\n";

    // If 500 ms elapsed since last LED toggle, toggle LED and update timestamp
    // Wraparound-safe condition: valid for uint32_t tick counters
    LedToggleTask(now, &lastBlink);

    // If 1000 ms elapsed since last UART transmit, send a message and update timestamp
    SerialTransmitTask(now, &lastUart, msgPtr);
  }
}

/**
 * @brief Generic error handler: disables interrupts and traps the CPU.
 *
 * This function is invoked when critical HAL configuration calls fail.
 * By disabling interrupts and looping indefinitely, it prevents further
 * execution and allows debugging via a halted state.
 */
void Error_Handler(void) {
  __disable_irq();  // Disable all interrupts to prevent further ISR execution
  while (1) {
    // Infinite loop: stay here for debugging/inspection
  }
}

#ifdef USE_FULL_ASSERT
/**
 * @brief Report the file and line number where assert_param() failed.
 *
 * @param file Pointer to the source file name string.
 * @param line Line number in the source file where the failure occurred.
 *
 * This function is enabled when USE_FULL_ASSERT is defined. It can be extended
 * to output the error information via UART or semihosting for easier debugging.
 */
void assert_failed(uint8_t *file, uint32_t line) {
  // Optionally print or log the assertion failure details:
  // Example (if UART is safe to use here):
  // char buf[64];
  // int n = snprintf(buf, sizeof(buf), "Assert failed: %s:%lu\r\n", file, (unsigned long)line);
  // HAL_UART_Transmit(&huart2, (uint8_t*)buf, (uint16_t)n, 100);
}
#endif
