// Adafruit Marquee simple test
//
// Skeleton sketch for local PlatformIO development against the
// Adafruit MagTag 2.9".

#include <Adafruit_GFX.h>
#include <Adafruit_Marquee_WiFi.h>
#include <Adafruit_ThinkInk.h>
#include <ArduinoJson.h>

Adafruit_Marquee_WiFi marquee;

void setup() {
  mq_begin_status_t status = marquee.begin();

  Serial.begin(115200);
  while (!Serial && millis() < 5000) {
    delay(10);
  }
  Serial.println("Adafruit Marquee");

  if (status != SUCCESS) {
    Serial.printf("Failed to initialize the Marquee client: %d\n", status);
    while (1) {
      delay(10);
    }
  }
  Serial.printf("Marquee begin() returned: %d\n", status);
  Serial.println("Calling marquee.connect()...");

  Serial.flush();
  if (!marquee.connect()) {
    Serial.println("Failed to connect to WiFi and/or the MQTT broker");
    while (1) {
      delay(10);
    }
  }

  Serial.println("Connected, running marquee app loop()...");
  Serial.flush();
}

void loop() {
    marquee.run();
}
