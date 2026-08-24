// Adafruit Marquee simple test
//
// Skeleton sketch for local PlatformIO development against the
// Adafruit MagTag 2.9".

#include <Adafruit_GFX.h>
#include <Adafruit_Marquee.h>
#include <Adafruit_ThinkInk.h>
#include <AdafruitIO_WiFi.h>
#include <ArduinoJson.h>

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

}

void loop() { delay(1000); }
