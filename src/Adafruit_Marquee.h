/*!
 * @file Adafruit_Marquee.h
 *
 * Adafruit Marquee client for Arduino.
 *
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing products
 * from Adafruit!
 *
 * Written by Brent Rubell for Adafruit Industries, 2026.
 *
 * MIT license, all text here must be included in any redistribution.
 */
#ifndef ADAFRUIT_MARQUEE_H
#define ADAFRUIT_MARQUEE_H

#include "Adafruit_ImageReader_EPD.h"
#include "Adafruit_TinyUSB.h"
#include "Arduino.h"
#include "SdFat_Adafruit_Fork.h"
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <Adafruit_ThinkInk.h>
#include <ArduinoJson.h>
#include <CRC.h>
#include <functional>
#include <map>

#ifndef MARQUEE_DEBUG
#define MARQUEE_DEBUG 1
#endif
#if MARQUEE_DEBUG
#define MQ_DEBUG_PRINT(...) Serial.print(__VA_ARGS__)     ///< Debug, no newline
#define MQ_DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__) ///< Debug + newline
#define MQ_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)   ///< Formatted debug
#define MQ_DEBUG_FLUSH() Serial.flush()                   ///< Drain the TX FIFO
#else
#define MQ_DEBUG_PRINT(...)                                                    \
  do {                                                                         \
  } while (0) ///< Disabled
#define MQ_DEBUG_PRINTLN(...)                                                  \
  do {                                                                         \
  } while (0) ///< Disabled
#define MQ_DEBUG_PRINTF(...)                                                   \
  do {                                                                         \
  } while (0) ///< Disabled
#define MQ_DEBUG_FLUSH()                                                       \
  do {                                                                         \
  } while (0) ///< Disabled
#endif

#define MAX_IO_FEED_NAME_LEN                                                   \
  128 ///< Maximum length of an Adafruit IO feed name, in bytes
#define MQ_BITMAP_SUB_LEN                                                      \
  81920 ///< Holds the payload for the bitmap subscription feed, in bytes (Sized
        ///< for a 4.2" Tricolor ThinkInk panel)
#define MQ_MQTT_BUFFER_LEN                                                     \
  (MQ_BITMAP_SUB_LEN + 256) ///< Packet buffer for the MQTT client + 256 bytes
                            ///< of headroom for the topic, in bytes

#define MQ_IO_HOST "io.adafruit.us" ///< Adafruit IO staging server
#define MQ_IO_MQTT_PORT 8883        ///< Adafruit IO MQTT server port

#define MQ_MQTT_KEEPALIVE_SEC 180 ///< Keepalive, in seconds
#define MQ_WIFI_RETRY_MS                                                       \
  5000 ///< Minimum wait between WiFi association attempts, in milliseconds.
#define MQ_MQTT_RETRY_MS                                                       \
  10000 ///< Minimum wait between MQTT connect attempts, in milliseconds.
#define MQ_WIFI_POLL_MS                                                        \
  3000 ///< How often to re-check for an association while connecting, in ms.

typedef enum {
  SUCCESS = 0,
  ERR_FS_UNFORMATTED = -1,
  ERR_FS_NO_CFG_FILE = -2,
  ERR_JSON_DESERIALIZATION = -3,
  ERR_TI_MODE_UNSUPPORTED = -4,
  ERR_IFACE_UNSUPPORTED = -5,
  ERR_EPD_PANEL_UNSUPPORTED = -6,
  ERR_INVALID_CREDS = -7,
} mq_begin_status_t; ///< Return codes for Adafruit_Marquee::begin()

typedef enum {
  SLEEP_ALARM_NONE = 0,
  SLEEP_ALARM_TIMER = 1,
} mq_sleep_alarm_t; ///< Sleep alarm types

typedef enum {
  SLEEP_MODE_NONE = 0,
  SLEEP_MODE_LIGHT = 1,
  SLEEP_MODE_DEEP = 2,
} mq_sleep_mode_t; ///< Sleep modes

/*!
 * @brief Client for the Adafruit IO Marquee feature.
 */
class Adafruit_Marquee {
public:
  Adafruit_Marquee();
  virtual ~Adafruit_Marquee();
  mq_begin_status_t begin();
  bool connect(unsigned long timeout = 30000);
  void run();

  // Platform-specific networking interface
  virtual bool
  networkConnected() = 0;          ///< Returns true if the network interface is
                                   ///< connected to a network, False otherwise
  virtual int networkStatus() = 0; ///< Returns a platform-specific status code
                                   ///< for the network interface
  virtual const char *
  connectionType() = 0; ///< Returns a string describing the network interface
                        ///< type (e.g. "WiFi", "Ethernet", "BLE")
  virtual void
  setupMQTTClient() = 0; ///< Configures the platform adapter's MQTT client

  static volatile bool fs_changed; ///< True when the filesystem is changed by
                                   ///< the host over USB MSC, False otherwise
protected:
  static Adafruit_Marquee *_instance; ///< Pointer to the instance that the MQTT
                                      ///< callbacks dispatch to
  static bool fs_formatted;
  mq_begin_status_t _begin_status;
  JsonDocument _cfg_doc;
  // USB MSC and Filesystem API
  bool initFilesystem();
  void initUSBMSC();

  // Networking API
  bool initWifi(unsigned long timeout);
  bool connectMqtt();
  bool initMqtt();
  void handleConnection();

  // ThinkInk panel API
  bool createEPD(const char *panel);
  bool parseThinkInkMode(const char *mode);
  bool decodeb64Bmp(const char *b64, size_t b64_len);
  void drawBitmap();
  Adafruit_ImageReader_EPD _reader; ///< In-memory BMP decoder for the EPD
  Adafruit_EPD *_display;           ///< Pointer to the EPD display object
  thinkinkmode_t _thinkInkMode;     ///< ThinkInk mode for the display
  int16_t _pin_cs;                  ///< Chip select pin for EPD
  int16_t _pin_dc;                  ///< Data/Command pin for EPD
  int16_t _pin_rst;                 ///< Reset pin for EPD
  int16_t _pin_busy;                ///< Busy pin for EPD
  int16_t _pin_sram_cs;             ///< SRAM chip select pin for EPD
  uint8_t _rotation;                ///< Display rotation (0-3)
  int16_t _width;                   ///< Panel width in pixels, post-rotation
  int16_t _height;                  ///< Panel height in pixels, post-rotation
  uint8_t *_pending_bmp;            ///< Decoded BMP bytes, or nullptr
  size_t _pending_len;              ///< Byte length of _pending_bmp

  // Networking
  const char *_ssid; ///< WiFi SSID
  const char *_pass; ///< WiFi password

  // Adafruit IO
  const char *_aio_username; ///< Adafruit IO username
  const char *_aio_key;      ///< Adafruit IO key
  const char *_device_name;  ///< Device name for Adafruit IO

  // Adafruit_MQTT
  Adafruit_MQTT_Client *_mqtt; ///< MQTT client, owns the packet buffer
  unsigned long _last_ping; ///< The last time a PINGREQ was sent to the broker,
                            ///< in millis()
  unsigned long _last_wifi_attempt;  ///< The last time a WiFi association was
                                     ///< attempted, in millis()
  unsigned long _last_mqtt_attempt;  ///< The last time an MQTT connection was
                                     ///< attempted, in millis()
  unsigned long _mqtt_retry_ms;      ///< How long to wait before retrying MQTT
                                     ///< connection, in milliseconds
  Adafruit_MQTT_Subscribe *_sub_bmp; ///< Subscription for the bitmap topic
  Adafruit_MQTT_Subscribe *_sub_sleep; ///< Subscription for the sleep topic
  char _feed_name_bmp[MAX_IO_FEED_NAME_LEN];   ///< Feed key for the bitmap feed
  char _feed_name_sleep[MAX_IO_FEED_NAME_LEN]; ///< Feed key for the sleep feed
  char _topic_bmp[MAX_IO_FEED_NAME_LEN + 96];  ///< <user>/f/<feed>/csv
  char _topic_sleep[MAX_IO_FEED_NAME_LEN + 96]; ///< <user>/f/<feed>/csv
  static void cbBitmapMsg(char *data, uint16_t len);
  static void cbSleepMsg(char *data, uint16_t len);
  void getBitmapFromFeed();

  // Network interface within networking/
  virtual void _connect() = 0;
  virtual void _disconnect() = 0;

  // Sleep API
  void handleSleep();
  bool _sleep_pending; ///< True if a sleep request is pending, False otherwise
  mq_sleep_mode_t _sleep_mode; ///< Sleep mode
  mq_sleep_alarm_t _sleep_alarm; ///< Sleep alarm type
  uint64_t _sleep_duration; ///< Sleep duration, in seconds

private:
#ifdef ARDUINO_ARCH_ESP32
  void printWakeupReason();
  bool enableTimerWakeup(uint64_t wakeup_time_sec);
  void disconnectBeforeSleep();
  void resumeFromSleep();
#endif
};

#endif // ADAFRUIT_MARQUEE_H
