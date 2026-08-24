// Adafruit Marquee simple test
//
// Skeleton sketch for local PlatformIO development against the
// Adafruit MagTag 2.9".

#include <Adafruit_Marquee.h>
#include <Adafruit_ThinkInk.h>
#include <Adafruit_GFX.h>
#include <AdafruitIO_WiFi.h>
#include <ArduinoJson.h>

// MagTag 2.9" grayscale E-Ink pinout
#define EPD_DC 7
#define EPD_CS 8
#define EPD_BUSY 5
#define SRAM_CS -1
#define EPD_RESET 6
#define EPD_SPI &SPI

ThinkInk_290_Grayscale4_T5 display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY,
                                  EPD_SPI);

Adafruit_Marquee marquee;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000) {
    delay(10);
  }
  Serial.println("Adafruit Marquee simple test");

  mq_begin_status_t status = marquee.begin();
  if (status != SUCCESS) {
    Serial.printf("Failed to initialize the Marquee client: %d\n", status);
    while (1) {
      delay(10);
    }
  }

  display.begin(THINKINK_GRAYSCALE4);
  display.clearBuffer();
  display.setTextColor(EPD_BLACK);
  display.setCursor(10, 10);
  display.print("Adafruit Marquee");
  display.display();
}

void loop() { delay(1000); }
