// Adafruit Marquee simple test
//
// Skeleton sketch for local PlatformIO development against the
// Adafruit MagTag 2.9".

#include <Adafruit_GFX.h>
#include <Adafruit_Marquee.h>
#include <Adafruit_ThinkInk.h>
#include <AdafruitIO_WiFi.h>
#include <ArduinoJson.h>
#include <esp_system.h>

Adafruit_Marquee marquee;

// Build with -DMARQUEE_SELFTEST to push a known-good bitmap straight into
// Adafruit_Marquee::queueBitmapBase64() at boot, bypassing MQTT entirely. Use
// it to tell a wiring problem apart from a transport problem: if the panel
// draws this but not a real feed value, the decode/draw path is fine.
#ifdef MARQUEE_SELFTEST
// 16x8, 1bpp, 2-entry palette, left half white / right half black. 94 bytes.
static const char kSelfTestBmp[] =
    "Qk1eAAAAAAAAAD4AAAAoAAAAEAAAAAgAAAABAAEAAAAAACAAAAAQAAAAEAAAAAIAAAACAAAA"
    "AAAAAP///wDwDwAA8A8AAPAPAADwDwAA8A8AAPAPAADwDwAA8A8AAA==";
#endif

void setup() {
  mq_begin_status_t status = marquee.begin();

  Serial.begin(115200);
  while (!Serial && millis() < 5000) {
    delay(10);
  }

  Serial.println("Adafruit Marquee");

  // Why did we boot? On this board the log goes out over USB CDC, so a reset
  // takes the serial device with it and any panic message is lost before the
  // host can read it - the monitor just reports "Device not configured". This
  // register survives the reset and says what actually happened.
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
    Serial.println("Failed to connect to WiFi and/or Adafruit IO");
    while (1) {
      delay(10);
    }
  }

  Serial.println("Connected to Adafruit IO, running Marquee client...");
  Serial.flush();

  // The bitmap feed sends a whole base64-encoded BMP in one MQTT payload, which
  // only fits if the forked Adafruit_MQTT and its -DMAXBUFFERSIZE are both in
  // the build. Both can be lost silently - a -D colliding with an unguarded
  // #define, or a fork carried as a .pio/libdeps edit that got reverted - and
  // the only symptom is a truncated payload. So say so out loud at boot.
  Serial.printf("MAXBUFFERSIZE=%d SUBSCRIPTIONDATALEN=%d\n", MAXBUFFERSIZE,
                SUBSCRIPTIONDATALEN);
  if (MAXBUFFERSIZE < MQ_BITMAP_SUB_LEN) {
    Serial.println("WARNING: MAXBUFFERSIZE is too small for a bitmap payload; "
                   "the feed value will arrive truncated.");
  }

#ifdef MARQUEE_SELFTEST
  Serial.println("[selftest] pushing built-in bitmap...");
  if (!marquee.queueBitmapBase64(kSelfTestBmp)) {
    Serial.println("[selftest] FAILED to queue");
  }
#endif
}

void loop() {
    marquee.run();
}
