/**
 * @file main.c
 * @brief Simple STM32 FreeRTOS example demonstrating:
 *        - USART4 periodic transmissions
 *        - LED blinking using a separate RTOS task
 *        - Default idle task
 *
 * Peripherals:
 *  - UART4 @115200 8N1 (TX + RX enabled)
 *  - GPIOA Pin 5 (LED)
 *  - GPIOA Pins 2/3 (USART2 AF routing, though USART2 not used)
 *
 * Tasks:
 *  - defaultTask: simple idle loop
 *  - LEDBlinkTask: toggles LED every 500 ms
 *  - SerialPrintTask: transmits "hi\r\n" every 300 ms
 */

#include "main.h"
#include "cmsis_os.h"
#include <string.h>

/* UART4 handle used globally */
UART_HandleTypeDef huart4;

/* ========================= RTOS TASK DEFINITIONS ========================= */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = osPriorityNormal,
  .stack_size = 128 * 4       // 512 bytes
};

osThreadId_t LEDBlinkHandle;
const osThreadAttr_t LEDBlink_attributes = {
  .name = "LEDBlink",
  .priority = osPriorityNormal,
  .stack_size = 128 * 4
};

osThreadId_t SerialPrintHandle;
const osThreadAttr_t SerialPrint_attributes = {
  .name = "SerialPrint",
  .priority = osPriorityNormal,
  .stack_size = 128 * 4
};

/* ======================= SYSTEM CLOCK CONFIGURATION ======================= */
/**
 * @brief Configure system clocks using internal HSI oscillator.
 *
 * Configuration:
 *  - SYSCLK  = HSI (16 MHz)
 *  - AHB     = SYSCLK / 1
 *  - APB1    = HCLK / 1
 *  - PLL     = Disabled
 *
 * Used for basic low-speed applications without high-frequency requirements.
 * Any HAL configuration failure triggers Error_Handler().
 */
void SystemClock_Config(void) {

  RCC_OscInitTypeDef RCC_OscInitStruct = {0};   // Oscillator configuration structure
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};   // Clock tree configuration structure

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1); // Set voltage scaling

  // --- Configure HSI oscillator ---
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;       // Use internal HSI
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;                         // Enable HSI
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;                         // No prescaler
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT; // Default trim
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;                   // Disable PLL

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler(); // Trap on failure
  }

  // --- Configure CPU, AHB, APB buses ---
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1;

  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI; // SYSCLK = HSI
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;     // AHB = SYSCLK
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;      // APB1 = AHB

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
    Error_Handler();
  }
}

/* ====================== HAL TIM TICK CALLBACK FUNCTION ==================== */
/**
 * @brief Increment HAL tick when TIM6 overflows.
 *
 * TIM6 is used internally by HAL for time base when configured.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM6) {
    HAL_IncTick();   // Increment millisecond tick
  }
}

/* =============================== GPIO SETUP =============================== */
/**
 * @brief Initialize GPIO pins:
 *
 *  - LED on PA5 as push-pull output
 *  - User button (B1) as rising-edge EXTI
 *  - USART2 TX/RX pins configured for AF mode (alternate function),
 *    though USART2 is unused in this example
 */
static void MX_GPIO_Init(void) {

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // Enable peripheral clocks
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  // Ensure LED output starts LOW
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);

  // --- Configure push button (B1) ---
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;  // Interrupt on rising edge
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  // --- Configure USART2 pins (AF mode) ---
  GPIO_InitStruct.Pin = USART2_TX_Pin | USART2_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;       // Alternate function push-pull
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF1_USART2;  // USART2 alternate function
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // --- Configure LED pin (PA5) ---
  GPIO_InitStruct.Pin = LED_GREEN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;   // Push-pull output
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // Fast toggle speed
  HAL_GPIO_Init(LED_GREEN_GPIO_Port, &GPIO_InitStruct);
}

/* =============================== UART SETUP =============================== */
/**
 * @brief Initialize USART4 for 115200 baud, 8 data bits, no parity, 1 stop bit.
 *
 * Used for:
 *  - TX: sending welcome message
 *  - TX: periodic "hi\r\n" from SerialPrint task
 */
static void MX_USART4_UART_Init(void) {

  huart4.Instance = USART4;                       // Select UART4
  huart4.Init.BaudRate = 115200;                  // Standard baud
  huart4.Init.WordLength = UART_WORDLENGTH_8B;    // 8 data bits
  huart4.Init.StopBits = UART_STOPBITS_1;         // 1 stop bit
  huart4.Init.Parity = UART_PARITY_NONE;          // No parity
  huart4.Init.Mode = UART_MODE_TX_RX;             // Enable TX + RX
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;    // No flow control
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart4) != HAL_OK) {
    Error_Handler();
  }
}

/* ============================== RTOS TASKS =============================== */
/**
 * @brief Default idle-like task.
 *
 * Runs continuously with a small delay so the scheduler can rotate.
 */
void StartDefaultTask(void *argument) {
  for (;;) {
    osDelay(1);   // Give CPU time to other tasks
  }
}

/**
 * @brief LED blink task.
 *
 * Toggles LED on PA5 every 500 ms.
 */
void LEDBlinkFunction(void *argument) {
  for (;;) {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);  // Flip LED state
    osDelay(500);                           // 0.5 second delay
  }
}

/**
 * @brief UART periodic message transmitter.
 *
 * Repeatedly transmits "hi\r\n" via UART4 every 300 ms.
 */
void SerialPrintFunction(void *argument) {
  char msg[] = "hi\r\n";                     // Message to transmit
  for (;;) {
    HAL_UART_Transmit(&huart4,              // UART handle
                      (uint8_t*)msg,        // Data buffer
                      strlen(msg),          // Length of buffer
                      HAL_MAX_DELAY);       // Block until sent
    osDelay(300);                           // Transmit every 300 ms
  }
}

/* ============================ APPLICATION ENTRY ========================== */
/**
 * @brief Main program entry point.
 *
 * Steps:
 *  1) Initialize HAL + system clocks
 *  2) Initialize GPIO and UART4
 *  3) Send a startup message: "WELCOME\r\n"
 *  4) Initialize RTOS kernel
 *  5) Create tasks
 *  6) Start scheduler (never returns)
 */
int main(void) {

  HAL_Init();             // Reset peripherals, initialize HAL
  SystemClock_Config();   // Set up the system clock
  MX_GPIO_Init();         // Configure GPIO
  MX_USART4_UART_Init();  // Configure UART4

  // --- Send a welcome message ---
  char wel_msg[] = "WELCOME\r\n";
  HAL_UART_Transmit(&huart4, (uint8_t*)wel_msg, strlen(wel_msg), HAL_MAX_DELAY);

  // --- Initialize and start RTOS ---
  osKernelInitialize();

  // Create tasks
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  LEDBlinkHandle    = osThreadNew(LEDBlinkFunction, NULL, &LEDBlink_attributes);
  SerialPrintHandle = osThreadNew(SerialPrintFunction, NULL, &SerialPrint_attributes);

  osKernelStart();        // Begin scheduling (never exits)

  while (1) {
    // Should never reach here
  }
}

/* ============================ ERROR HANDLER ============================== */
/**
 * @brief Critical fault handler.
 *
 * Disables interrupts and traps execution in an infinite loop.
 */
void Error_Handler(void) {
  __disable_irq();
  while (1) {
    // Stay here forever
  }
}

#ifdef USE_FULL_ASSERT
/**
 * @brief HAL assertion failure handler (optional).
 */
void assert_failed(uint8_t *file, uint32_t line) {
  // User may implement debug printing here
}
#endif
