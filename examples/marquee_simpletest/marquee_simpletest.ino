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
  mq_begin_status_t status = marquee.begin();

  Serial.begin(115200);
  while (!Serial && millis() < 5000) {
    delay(10);
  }

  Serial.println("Adafruit Marquee");
  Serial.printf("Marquee begin() returned: %d\n", status);
  if (status != SUCCESS) {
    Serial.printf("Failed to initialize the Marquee client: %d\n", status);
    while (1) {
      delay(10);
    }
  }

  if (!marquee.connect()) {
    Serial.println("Failed to connect to WiFi and/or Adafruit IO");
    while (1) {
      delay(10);
    }
  }

  Serial.println("Connected to Adafruit IO, running Marquee client...");
}

void loop() {
    marquee.run();
}
