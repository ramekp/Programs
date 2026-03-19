/************************************************************
 *  FILE: vibgyor_serial_freertos_neopixel.ino
 *  BOARD: ESP32-C6-DevKitC-1
 *  LED: Onboard WS2812 RGB LED (GPIO 8)
 *
 *  DESCRIPTION:
 *  ---------------------------------------------------------
 *  This program uses FreeRTOS to create a dedicated task
 *  that waits for SERIAL INPUT. Based on the received
 *  character, the onboard RGB LED will blink a color
 *  from the VIBGYOR spectrum.
 *
 *  SUPPORTED COMMANDS:
 *      'v' → Violet
 *      'i' → Indigo
 *      'b' → Blue
 *      'g' → Green
 *      'y' → Yellow
 *      'o' → Orange
 *      'r' → Red
 *
 *  The LED is a WS2812 RGB device, so access must be
 *  protected with a FreeRTOS MUTEX.
 ************************************************************/

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// -----------------------------------------------------------
// CONSTANT DEFINITIONS
// -----------------------------------------------------------
#define LED_PIN     8
#define NUM_LEDS    1

// -----------------------------------------------------------
// GLOBAL OBJECTS
// -----------------------------------------------------------
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
SemaphoreHandle_t ledMutex;


/************************************************************
 * FUNCTION: blinkColor
 * PURPOSE:
 *   Blinks LED ONCE in specified RGB color.
 ************************************************************/
void blinkColor(uint8_t r, uint8_t g, uint8_t b, uint32_t delayMs) {

  // Turn ON LED
  if (xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
    strip.setPixelColor(0, strip.Color(r, g, b));
    strip.show();
    xSemaphoreGive(ledMutex);
  }

  vTaskDelay(pdMS_TO_TICKS(delayMs));

  // Turn LED OFF
  if (xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
    strip.setPixelColor(0, 0);
    strip.show();
    xSemaphoreGive(ledMutex);
  }

  vTaskDelay(pdMS_TO_TICKS(delayMs));
}


/************************************************************
 * TASK: serialTask
 * PURPOSE:
 *   Waits for serial input and triggers VIBGYOR colors.
 ************************************************************/
void serialTask(void *pvParameters) {

  for (;;) {

    if (Serial.available() > 0) {
      char c = Serial.read();

      Serial.print("Received: ");
      Serial.println(c);

      switch (c) {

        case 'v': blinkColor(148,   0, 211, 400); break; // Violet
        case 'i': blinkColor( 75,   0, 130, 400); break; // Indigo
        case 'b': blinkColor(  0,   0, 255, 400); break; // Blue
        case 'g': blinkColor(  0, 255,   0, 400); break; // Green
        case 'y': blinkColor(255, 255,   0, 400); break; // Yellow
        case 'o': blinkColor(255, 165,   0, 400); break; // Orange
        case 'r': blinkColor(255,   0,   0, 400); break; // Red

        default:
          Serial.println("Unknown command. Use v,i,b,g,y,o,r");
          break;
      }
    }

    // MODIFIED DELAY (20ms → 200ms)
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}


/************************************************************
 * FUNCTION: setup()
 ************************************************************/
void setup() {

  Serial.begin(115200);
  while (!Serial) {}

  strip.begin();
  strip.show();

  ledMutex = xSemaphoreCreateMutex();

  xTaskCreate(
      serialTask,
      "SerialTask",
      4096,
      NULL,
      1,
      NULL
  );

  Serial.println("VIBGYOR System Ready. Use v,i,b,g,y,o,r");
}


/************************************************************
 * LOOP NOT USED
 ************************************************************/
void loop() {}