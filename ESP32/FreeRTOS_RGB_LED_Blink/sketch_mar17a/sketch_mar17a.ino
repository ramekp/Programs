/************************************************************
 *  FILE: dual_color_freertos_neopixel.ino
 *  BOARD: ESP32-C6-DevKitC-1
 *  LED: Onboard WS2812 RGB LED (connected to GPIO 8)
 *  
 *  DESCRIPTION:
 *  ---------------------------------------------------------
 *  This program demonstrates TRUE FreeRTOS multitasking on
 *  the ESP32-C6 using the Arduino core. Three independent
 *  FreeRTOS tasks run concurrently:
 *
 *    - taskRed()   → blinks RED   every 500 ms
 *    - taskBlue()  → blinks BLUE  every 300 ms
 *    - taskGreen() → blinks GREEN every 700 ms
 *
 *  Because the WS2812 LED requires precise timing,
 *  a FreeRTOS MUTEX (ledMutex) ensures only one task
 *  updates the LED at a time.
 ************************************************************/

#include <Arduino.h>            // Arduino base functionality
#include <Adafruit_NeoPixel.h>  // WS2812 LED control library

// -----------------------------------------------------------
// CONSTANT DEFINITIONS
// -----------------------------------------------------------
#define LED_PIN     8     // Onboard WS2812 LED on GPIO 8
#define NUM_LEDS    1     // Only one onboard RGB LED

// -----------------------------------------------------------
// GLOBAL OBJECTS
// -----------------------------------------------------------
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Mutex to protect pixel operations across tasks
SemaphoreHandle_t ledMutex;


/************************************************************
 * TASK: taskRed
 * PURPOSE:
 *   Blinks RED every 500ms.
 ************************************************************/
void taskRed(void *pvParameters) {

  for (;;) {

    // ---- Turn RED ON ----
    if (xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
      strip.setPixelColor(0, strip.Color(255, 0, 0));
      strip.show();
      xSemaphoreGive(ledMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(250));

    // ---- Turn LED OFF ----
    if (xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
      strip.setPixelColor(0, 0);
      strip.show();
      xSemaphoreGive(ledMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}


/************************************************************
 * TASK: taskBlue
 * PURPOSE:
 *   Blinks BLUE every 300ms.
 ************************************************************/
void taskBlue(void *pvParameters) {

  for (;;) {

    // ---- Turn BLUE ON ----
    if (xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
      strip.setPixelColor(0, strip.Color(0, 0, 255));
      strip.show();
      xSemaphoreGive(ledMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(300));

    // ---- Turn LED OFF ----
    if (xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
      strip.setPixelColor(0, 0);
      strip.show();
      xSemaphoreGive(ledMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(300));
  }
}


/************************************************************
 * TASK: taskGreen
 * PURPOSE:
 *   Blinks GREEN every 700ms.
 *
 * NOTE:
 *   Uses the same mutex to safely write to WS2812 LED.
 ************************************************************/
void taskGreen(void *pvParameters) {

  for (;;) {

    // ---- Turn GREEN ON ----
    if (xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
      strip.setPixelColor(0, strip.Color(0, 255, 0)); // GREEN
      strip.show();
      xSemaphoreGive(ledMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(350));

    // ---- Turn LED OFF ----
    if (xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
      strip.setPixelColor(0, 0);
      strip.show();
      xSemaphoreGive(ledMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(350));
  }
}


/************************************************************
 * FUNCTION: setup()
 * PURPOSE:
 *   Initialize LED strip, mutex, and create all tasks.
 ************************************************************/
void setup() {

  strip.begin();   // Initialize WS2812 driver
  strip.show();    // Ensure LED is OFF

  ledMutex = xSemaphoreCreateMutex();   // Create mutex

  // Create all tasks
  xTaskCreate(taskRed,   "RedTask",   2048, NULL, 1, NULL);
  xTaskCreate(taskBlue,  "BlueTask",  2048, NULL, 1, NULL);
  xTaskCreate(taskGreen, "GreenTask", 2048, NULL, 1, NULL);
}


/************************************************************
 * FUNCTION: loop()
 * PURPOSE:
 *   Not used — FreeRTOS handles everything.
 ************************************************************/
void loop() {
  // Empty
}