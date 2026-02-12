/**
 * @file main.c
 * @brief FreeRTOS-based firmware example for STM32 using CMSIS-OS v2.
 *
 * This application:
 *  - Configures system clock using HSI (no PLL).
 *  - Initializes GPIO and USART2 (115200-8N1).
 *  - Creates three threads:
 *      1) defaultTask: Idle/dummy task.
 *      2) LED_Task: Toggles LED on PA5 every 500 ms.
 *      3) UART_Task: Transmits a periodic message over USART2 every 1 s.
 *  - Provides SysTick increment via TIM6 period elapsed callback.
 *
 * Notes:
 *  - Ensure LED_GREEN_Pin / LED_GREEN_GPIO_Port and B1_Pin / B1_GPIO_Port
 *    symbols are correctly defined in the board's header (e.g., main.h).
 *  - USART2 TX/RX pins must be configured in CubeMX or board support files.
 */

#include "main.h"                                     // Board-specific HAL and pin definitions
#include "cmsis_os.h"                                 // CMSIS-OS v2 (FreeRTOS wrapper) API
#include <string.h>                                   // Provides strlen for UART transmit size

/* UART2 handle for TX/RX operations */
UART_HandleTypeDef huart2;                            // Global UART2 handle used by HAL functions

/* -------------------------------------------------------------------------- */
/*                               RTOS Resources                               */
/* -------------------------------------------------------------------------- */

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;                       // RTOS thread ID handle for default task
/* Thread attributes: name, priority, and stack size */
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",                              // Task name (visible in some debuggers)
  .priority = (osPriority_t) osPriorityNormal,        // Normal priority level
  .stack_size = 128 * 4                               // Stack size in bytes (128 words * 4 bytes)
};

/* Definitions for LED_Task */
osThreadId_t LEDBlink_TaskHandle;                     // RTOS thread ID handle for LED task
/* Thread attributes for LED task */
const osThreadAttr_t LEDBlink_attributes = {
  .name = "LEDBlink",                                 // Task name
  .priority = (osPriority_t) osPriorityNormal,        // Normal priority
  .stack_size = 128 * 4                               // Stack size
};

/* Definitions for UART_Task */
osThreadId_t SerialPrint_TaskHandle;                  // RTOS thread ID handle for UART task
/* Thread attributes for UART task */
const osThreadAttr_t SerialPrint_attributes = {
  .name = "SerialPrint",                              // Task name
  .priority = (osPriority_t) osPriorityNormal,        // Normal priority
  .stack_size = 128 * 4                               // Stack size
};

/* -------------------------------------------------------------------------- */
/*                           System/Peripheral Init                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configures the system clock.
 *        - Uses HSI as SYSCLK source
 *        - No PLL
 *        - AHB/APB1 prescalers set to DIV1
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};         // Structure to configure oscillators
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};         // Structure to configure bus clocks

  /* Configure internal regulator output voltage */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);        // Set regulator scaling for performance

  /* Initializes the RCC Oscillators:
     - Use HSI ON
     - No PLL
     - Default HSI calibration
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;            // Select HSI as oscillator
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;                              // Turn HSI on
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;                              // No division on HSI
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;   // Default calibration
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;                        // Disable PLL
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {                // Apply oscillator configuration
    /* If oscillator config fails, trap in error handler */
    Error_Handler();                                                    // Handle error (infinite loop)
  }

  /* Initializes the CPU, AHB and APB buses clocks:
     - SYSCLK source: HSI
     - AHB prescaler: DIV1
     - APB1 prescaler: DIV1
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;                     // Select clocks to configure
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;                // Use HSI as system clock
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;                    // AHB clock = SYSCLK
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;                     // APB1 clock = HCLK

  /* FLASH latency set to 0 (suitable for HSI without PLL on many parts) */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {     // Apply bus clock config
    Error_Handler();                                                    // Handle error
  }
}

/**
 * @brief Initializes GPIO pins required by the application.
 *        - Configures user button (B1) as rising edge interrupt.
 *        - Configures LED pin (LED_GREEN) as push-pull output.
 */
static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};                               // GPIO configuration structure

  /* Enable GPIO port clocks used by the application */
  __HAL_RCC_GPIOC_CLK_ENABLE();                                         // Enable clock for GPIOC
  __HAL_RCC_GPIOF_CLK_ENABLE();                                         // Enable clock for GPIOF
  __HAL_RCC_GPIOA_CLK_ENABLE();                                         // Enable clock for GPIOA

  /* Initialize LED pin to a known (RESET) output state */
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);  // Set LED low initially

  /* Configure User Button (B1) as external interrupt on rising edge */
  GPIO_InitStruct.Pin = B1_Pin;                                         // Select B1 pin
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;                           // Interrupt on rising edge
  GPIO_InitStruct.Pull = GPIO_NOPULL;                                   // No internal pull-up/down
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);                        // Initialize button GPIO

  /* Configure LED pin as push-pull output, high speed, no pull */
  GPIO_InitStruct.Pin = LED_GREEN_Pin;                                  // Select LED pin
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;                           // Push-pull output
  GPIO_InitStruct.Pull = GPIO_NOPULL;                                   // No internal pull resistor
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;                         // High speed output for fast toggles
  HAL_GPIO_Init(LED_GREEN_GPIO_Port, &GPIO_InitStruct);                 // Initialize LED GPIO
}

/**
 * @brief Initializes USART2 peripheral for 115200-8N1 with FIFOs disabled.
 */
static void MX_USART2_UART_Init(void) {
  /* Basic UART configuration for 115200 8N1, no flow control */
  huart2.Instance = USART2;                                             // Use USART2 peripheral
  huart2.Init.BaudRate = 115200;                                        // Set baud rate to 115200
  huart2.Init.WordLength = UART_WORDLENGTH_8B;                          // 8-bit word length
  huart2.Init.StopBits = UART_STOPBITS_1;                               // 1 stop bit
  huart2.Init.Parity = UART_PARITY_NONE;                                // No parity
  huart2.Init.Mode = UART_MODE_TX_RX;                                   // Enable both TX and RX
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;                          // No hardware flow control
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;                      // Standard oversampling
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;             // Disable one-bit sampling
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;                     // No prescaler
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;         // No advanced features

  /* Initialize UART peripheral */
  if (HAL_UART_Init(&huart2) != HAL_OK) {                               // Apply UART configuration
    Error_Handler();                                                    // Handle error
  }

  /* Optional FIFO settings (device dependent). Set thresholds to 1/8. */
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {    // TX FIFO threshold
    Error_Handler();                                                                    // Handle error
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) {    // RX FIFO threshold
    Error_Handler();                                                                    // Handle error
  }

  /* Disable FIFO mode (use legacy mode) */
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK) {                  // Disable FIFO feature
    Error_Handler();                                                    // Handle error
  }
}

/* -------------------------------------------------------------------------- */
/*                                RTOS Threads                                */
/* -------------------------------------------------------------------------- */
/**
 * @brief Default FreeRTOS task function (placeholder/idle workload).
 * @param argument Unused
 */
void StartDefaultTask(void *argument) {
  /* Default task: placeholder loop yielding CPU with small delay */
  for(;;) {                                                             // Run forever
    osDelay(1);                                                         // Sleep for 1 tick to let others run
  }
}

/**
 * @brief LED task function. Toggles PA5 every 500 ms.
 * @param argument Unused
 */
void LEDBlinkFunction(void *argument) {
  /* LED Task: Toggle PA5 (common LED pin on Nucleo boards) every 500 ms */
  for(;;) {                                                             // Run forever
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);                              // Toggle PA5 (LED)
    osDelay(500);                                                       // Wait 500 ms
  }
}

/**
 * @brief UART task function. Periodically transmits a message via USART2
 *        every 1 second.
 * @param argument Unused
 */
void SerialPrintFunction(void *argument) {
  /* UART Task: Transmit a message over UART every 1000 ms */
  char msg[] = "Hello from FreeRTOS GPIO Serial Configurations .... \r\n";      // Message buffer to send
  for(;;) {                                                                     // Run forever
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);      // Blocking transmit
    osDelay(1000);                                                              // Wait 1 second
  }
}

/* -------------------------------------------------------------------------- */
/*                                    main                                    */
/* -------------------------------------------------------------------------- */

int main(void) {
  /* Reset all peripherals, initialize Flash interface and SysTick. */
  HAL_Init();                                                           // Initialize HAL library

  /* Configure the system clock */
  SystemClock_Config();                                                 // Set up clocks with HSI

  /* Initialize GPIO and USART2 peripherals */
  MX_GPIO_Init();                                                       // Configure GPIO pins for LED and button
  MX_USART2_UART_Init();                                                // Configure UART2

  /* Initialize CMSIS-OS kernel */
  osKernelInitialize();                                                 // Prepare RTOS kernel

  /* Create threads (tasks) with their respective attributes */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);           // Create default task
  LEDBlink_TaskHandle = osThreadNew(LEDBlinkFunction, NULL, &LEDBlink_attributes);            // Create LED task
  SerialPrint_TaskHandle = osThreadNew(SerialPrintFunction, NULL, &SerialPrint_attributes);   // Create UART task

  /* Start the RTOS scheduler; control is transferred to threads */
  osKernelStart();                                                      // Start scheduling tasks

  /* Infinite loop: should never reach here as scheduler is running */
  while (1) {                                                           // Safety loop if scheduler returns
  }
}

/* -------------------------------------------------------------------------- */
/*                          HAL Callback / Error Hook                         */
/* -------------------------------------------------------------------------- */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  /* Increment HAL tick when TIM6 overflows (configured elsewhere) */
  if (htim->Instance == TIM6) {                 // Check the timer instance
    HAL_IncTick();                              // Increment HAL millisecond tick
  }
}

void Error_Handler(void) {
  /* Disable interrupts and trap CPU in infinite loop on fatal error */
  __disable_irq();                              // Disable global interrupts
  while (1) {                                   // Stay here forever
  }
}
