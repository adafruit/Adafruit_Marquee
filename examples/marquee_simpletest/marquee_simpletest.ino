// Adafruit Marquee simple test
//
// Skeleton sketch for local PlatformIO development against the
// Adafruit MagTag 2.9".

#include <Adafruit_GFX.h>
#include <Adafruit_Marquee_WiFi.h>
#include <Adafruit_ThinkInk.h>
#include <ArduinoJson.h>
#include <esp_system.h>

Adafruit_Marquee_WiFi marquee;

void setup() {
  mq_begin_status_t status = marquee.begin();

  Serial.begin(115200);
  while (!Serial && millis() < 5000) {
    delay(10);
  }

  Serial.println("Adafruit Marquee");


  /*
  const char *why;
  switch (esp_reset_reason()) {
  case ESP_RST_POWERON:  why = "power-on"; break;
  case ESP_RST_EXT:      why = "external pin"; break;
  case ESP_RST_SW:       why = "software restart"; break;
  case ESP_RST_PANIC:    why = "PANIC / exception"; break;
  case ESP_RST_INT_WDT:  why = "interrupt watchdog"; break;
  case ESP_RST_TASK_WDT: why = "task watchdog"; break;
  case ESP_RST_WDT:      why = "other watchdog"; break;
  case ESP_RST_DEEPSLEEP: why = "deep sleep wake"; break;
  case ESP_RST_BROWNOUT: why = "BROWNOUT (supply sagged)"; break;
  case ESP_RST_SDIO:     why = "SDIO"; break;
  default:               why = "unknown"; break;
  }
  Serial.printf("Reset reason: %s (%d)\n", why, (int)esp_reset_reason());
  */
  Serial.printf("Marquee begin() returned: %d\n", status);
  if (status != SUCCESS) {
    Serial.printf("Failed to initialize the Marquee client: %d\n", status);
    while (1) {
      delay(10);
    }
  }

  Serial.println("Calling marquee.connect()...");
  Serial.flush();
  if (!marquee.connect()) {
    Serial.println("Failed to connect to WiFi and/or the MQTT broker");
    while (1) {
      delay(10);
    }
  }

  Serial.println("Connected, running Marquee client...");
  Serial.flush();
}

void loop() {
    marquee.run();
}
