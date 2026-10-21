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
#include <functional>
#include <map>

#ifndef MARQUEE_DEBUG
#define MARQUEE_DEBUG 1 ///< Set to 0 to compile out the serial debug output
#endif
#define MQ_USB_DRAIN_MS                                                        \
  150 ///< How long to let the host read the CDC TX FIFO before detaching USB,
      ///< in milliseconds.
#if MARQUEE_DEBUG
#define MQ_DEBUG_PRINT(...) Serial.print(__VA_ARGS__)     ///< Debug, no newline
#define MQ_DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__) ///< Debug + newline
#define MQ_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)   ///< Formatted debug
#define MQ_DEBUG_FLUSH()                                                       \
  do {                                                                         \
    Serial.flush();                                                            \
    delay(MQ_USB_DRAIN_MS);                                                    \
  } while (0) ///< Drain the TX FIFO and wait for the host to read it
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

#define MQ_IO_HOST "io.adafruit.com" ///< Adafruit IO MQTT server address
#define MQ_IO_MQTT_PORT 8883         ///< Adafruit IO MQTT server port

#define MQ_MQTT_KEEPALIVE_MS 180000 ///< MQTT server keepalive, in milliseconds
#define MQ_WIFI_RETRY_MS                                                       \
  5000 ///< Minimum wait between WiFi association attempts, in milliseconds.
#define MQ_MQTT_RETRY_MS                                                       \
  10000 ///< Minimum wait between MQTT connect attempts, in milliseconds.
#define MQ_WIFI_POLL_MS                                                        \
  100 ///< How often to re-check for an association while connecting, in ms.
#define MQ_MQTT_CONNECT_ATTEMPTS                                               \
  3 ///< MQTT connect attempts in connect() before it reports ERR_IO_CONNECT
#define MQ_ERROR_RETRY_MIN_MS                                                  \
  60000 ///< First wait before displayStatus() retries a failed connect, in ms
#define MQ_ERROR_RETRY_MAX_MS                                                  \
  900000 ///< Longest wait between displayStatus() connect retries, in ms
#define MQ_MQTT_RETRY_MAX_MS                                                   \
  300000 ///< Longest wait between handleConnection() reconnects, in ms
#define MQ_MQTT_STABLE_MS                                                      \
  60000 ///< A session up this long resets the reconnect wait, in ms
#define MQ_IO_HOLD_DEFAULT_MS                                                  \
  60000 ///< How long an IO throttle or ban notice with no duration holds off
        ///< reconnects and publishes, in ms
#define MQ_IO_HOLD_MAX_S                                                       \
  3600 ///< Longest hold taken from an IO notice, in seconds (IO's nominal
       ///< ban cap)
#define MQ_IO_NOTICE_SUB_LEN                                                   \
  256 ///< Holds a {user}/throttle or {user}/errors message, in bytes

#define MQ_SSID_MAX_LEN 32     ///< Maximum length of a WiFi SSID, in chars
#define MQ_WPA_PASS_MIN_LEN 8  ///< Minimum length of a WPA password, in chars
#define MQ_WPA_PASS_MAX_LEN 63 ///< Maximum length of a WPA password, in chars

/*!
 * @brief Return codes for Adafruit_Marquee begin(), parseCreds(), and
 *        connect()
 */
typedef enum {
  SUCCESS = 0,                    ///< No error
  ERR_FS_UNFORMATTED = -1,        ///< The filesystem is not formatted
  ERR_FS_NO_CFG_FILE = -2,        ///< cfg-marquee.json was not found
  ERR_JSON_DESERIALIZATION = -3,  ///< cfg-marquee.json is not valid JSON
  ERR_TI_MODE_UNSUPPORTED = -4,   ///< The display mode is not supported
  ERR_IFACE_UNSUPPORTED = -5,     ///< The display interface is not supported
  ERR_EPD_PANEL_UNSUPPORTED = -6, ///< The display panel is not supported
  ERR_INVALID_WIFI_CREDS = -7,    ///< The WiFi SSID or password is invalid
  ERR_FLASH_INIT = -8,            ///< The flash chip could not be started
  ERR_INVALID_IO_CREDS = -9,      ///< The IO username or key is empty
  ERR_INVALID_DEVICE_NAME = -10,  ///< The device name is missing
  ERR_MQTT_INIT = -11,            ///< The MQTT client could not be set up
  ERR_WIFI_CONNECT = -12,         ///< Could not connect to the WiFi network
  ERR_IO_CONNECT = -13,           ///< Could not connect to Adafruit IO
  ERR_MISSING_WIFI_CREDS = -14,   ///< The WiFi credentials are missing
  ERR_MISSING_IO_CREDS = -15,     ///< The IO username or key is missing
} mq_status_t;

typedef mq_status_t mq_begin_status_t; ///< Deprecated, use mq_status_t

/*!
 * @brief Sleep alarm types
 */
typedef enum {
  SLEEP_ALARM_NONE = 0,  ///< No wakeup alarm
  SLEEP_ALARM_TIMER = 1, ///< Wake up after a timer expires
} mq_sleep_alarm_t;

/*!
 * @brief Sleep modes
 */
typedef enum {
  SLEEP_MODE_NONE = 0,  ///< Do not sleep
  SLEEP_MODE_LIGHT = 1, ///< Light sleep
  SLEEP_MODE_DEEP = 2,  ///< Deep sleep
} mq_sleep_mode_t;

/*!
 * @brief Client for the Adafruit IO Marquee feature.
 */
class Adafruit_Marquee {
public:
  Adafruit_Marquee();
  virtual ~Adafruit_Marquee();
  mq_status_t begin();
  mq_status_t parseCreds();
  bool connect(unsigned long timeout = 30000);
  mq_status_t getStatus();
  void displayStatus();
  void run();

  // Platform-specific networking interface
  /*!
   * @brief Checks whether the network interface is connected to a network.
   * @returns True if connected, False otherwise.
   */
  virtual bool isNetConnected() = 0;
  /*!
   * @brief Describes the network interface type.
   * @returns A string such as "WiFi", "Ethernet" or "BLE".
   */
  virtual const char *connectionType() = 0;
  virtual void
  setupMQTTClient() = 0; ///< Configures the platform adapter's MQTT client

  static volatile bool fs_changed; ///< True when the filesystem is changed by
                                   ///< the host over USB MSC, False otherwise
protected:
  static Adafruit_Marquee *_instance; ///< Pointer to the instance that the MQTT
                                      ///< callbacks dispatch to
  static bool fs_formatted;           ///< True if the filesystem is formatted
  mq_status_t _status;   ///< Status of the last begin(), parseCreds(), or
                         ///< connect() call
  JsonDocument _cfg_doc; ///< Parsed contents of cfg-marquee.json
  // USB MSC and Filesystem API
  mq_status_t initFilesystem();
  void initUSBMSC();

  // Networking API
  mq_status_t parseNetCreds();
  mq_status_t parseIOCreds();
  bool parseDeviceName();
  bool initWifi(unsigned long timeout);
  bool connectMqtt();
  bool initMqtt();
  void handleConnection();
  bool retryConnect();
  static bool mqttRetrySoon(int8_t rc);
  static bool mqttRetryLater(int8_t rc);
  void holdForIONotice(const char *msg);
  bool ioHoldActive();

  // Board-specific power rails
#if defined(MARQUEE_BOARD_XTEINK_X4_PRO)
  void marqueeBoardPowerUp();
#endif

  // ThinkInk panel API
  mq_status_t parseDisplayCfg(File32 &cfg);
  bool createEPD(const char *panel);
  bool parseThinkInkMode(const char *mode);
  bool decodeb64Bmp(const char *b64, size_t b64_len);
  void drawBitmap();
  void drawStatus();
  void displayErrorMsg(const char *title, const char *details,
                       const char *label = nullptr,
                       const char *value = nullptr);
  Adafruit_ImageReader_EPD _reader; ///< In-memory BMP decoder for the EPD
  Adafruit_EPD *_display;           ///< Pointer to the EPD display object
  thinkinkmode_t _thinkInkMode;     ///< ThinkInk mode for the display
  int16_t _pin_cs;                  ///< Chip select pin for EPD
  int16_t _pin_dc;                  ///< Data/Command pin for EPD
  int16_t _pin_rst;                 ///< Reset pin for EPD
  int16_t _pin_busy;                ///< Busy pin for EPD
  int16_t _pin_sram_cs;             ///< SRAM chip select pin for EPD
  int16_t _pin_sclk;                ///< SPI clock pin
  int16_t _pin_mosi;                ///< SPI MOSI pin
  int16_t _pin_miso;                ///< SPI MISO pin
  uint8_t _rotation;                ///< Display rotation (0-3)
  int16_t _width;                   ///< Panel width in pixels, post-rotation
  int16_t _height;                  ///< Panel height in pixels, post-rotation
  uint8_t *_pending_bmp;            ///< Decoded BMP bytes, or nullptr
  size_t _pending_bmp_len;          ///< Byte length of _pending_bmp
  uint32_t _pending_crc; ///< CRC32 of the base64 payload behind _pending_bmp

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
  unsigned long _last_wifi_attempt; ///< The last time a WiFi association was
                                    ///< attempted, in millis()
  unsigned long _last_mqtt_attempt; ///< The last time an MQTT connection was
                                    ///< attempted, in millis()
  unsigned long _mqtt_retry_ms;     ///< How long to wait before retrying MQTT
                                    ///< connection, in milliseconds
  unsigned long _mqtt_connected_at; ///< When the current MQTT session came up,
                                    ///< in millis()
  int8_t _mqtt_rc; ///< Result of the last MQTT connect: 0 on success, -1 for
                   ///< no connection (also the value before any attempt), -2
                   ///< for a failed subscribe, else the broker's CONNACK
                   ///< refusal; see Adafruit_MQTT::connectErrorString()
  unsigned long _io_hold_start;   ///< When the last IO throttle or ban notice
                                  ///< arrived, in millis()
  unsigned long _io_hold_ms;      ///< How long that notice holds off reconnects
                                  ///< and publishes, in ms; 0 for no hold
  unsigned long _connect_timeout; ///< The WiFi timeout connect() was given, in
                                  ///< milliseconds, reused by retryConnect()
  Adafruit_MQTT_Subscribe *_sub_bmp;      ///< Subscription for the bitmap topic
  Adafruit_MQTT_Subscribe *_sub_sleep;    ///< Subscription for the sleep topic
  Adafruit_MQTT_Subscribe *_sub_throttle; ///< Subscription for IO's data rate
                                          ///< throttle notices
  Adafruit_MQTT_Subscribe *_sub_errors;   ///< Subscription for IO's error and
                                          ///< ban notices
  bool _awaiting_bmp;   ///< True while this session's bitmap feed /get is still
                        ///< unanswered
  bool _awaiting_sleep; ///< True while this session's sleep feed /get is still
                        ///< unanswered
  char _feed_name_bmp[MAX_IO_FEED_NAME_LEN];   ///< Feed key for the bitmap feed
  char _feed_name_sleep[MAX_IO_FEED_NAME_LEN]; ///< Feed key for the sleep feed
  char _topic_bmp[MAX_IO_FEED_NAME_LEN + 96];  ///< {user}/f/{feed}/csv
  char _topic_sleep[MAX_IO_FEED_NAME_LEN + 96];  ///< {user}/f/{feed}
  char _feed_name_status[MAX_IO_FEED_NAME_LEN];  ///< Feed key for the status
                                                 ///< feed
  char _topic_status[MAX_IO_FEED_NAME_LEN + 96]; ///< {user}/f/{feed}, no /csv:
                                                 ///< the payload is JSON
  char _topic_throttle[MAX_IO_FEED_NAME_LEN];    ///< {user}/throttle
  char _topic_errors[MAX_IO_FEED_NAME_LEN];      ///< {user}/errors
  static void cbBitmapMsg(char *data, size_t len);
  static void cbSleepMsg(char *data, size_t len);
  static void cbIONoticeMsg(char *data, size_t len);
  void getFromFeed(const char *topic);
  bool publishStatus(const char *payload);

  // Network interface within networking/
  virtual void _connect() = 0;    ///< Connects the network interface
  virtual void _disconnect() = 0; ///< Disconnects the network interface

  // Sleep API
  void handleSleep();
  bool _is_sleep_pending;        ///< True if a sleep request is pending, False
                                 ///< otherwise
  mq_sleep_mode_t _sleep_mode;   ///< Sleep mode
  mq_sleep_alarm_t _sleep_alarm; ///< Sleep alarm type
  uint64_t _sleep_time;          ///< Sleep duration, in seconds

private:
  bool loadPrvBmpCRC(uint32_t &crc);
  void storeBmpCRC(uint32_t crc);

  // Battery reporting, opt-in via "battery": {"enabled": true} in the config.
  // A no-op on boards without a battery monitor.
  void parseBatteryCfg();
  void sampleBattery();
  void publishBattery();
  bool _batt_enabled;    ///< True if battery reporting is enabled and supported
  bool _batt_as_percent; ///< Publish percent (true) or volts (false)
  bool _batt_pending;    ///< True while a sample is waiting to be published
  float _batt_volts;     ///< Last battery sample, in volts, or < 0 if none
  const char
      *_feed_name_battery; ///< Optional feed key override from the config
  char _topic_batt[MAX_IO_FEED_NAME_LEN + 96]; ///< <user>/f/<feed>

  const char *wakeupReason();
  bool didWakeFromSleep();
#ifdef ARDUINO_ARCH_ESP32
  bool enableTimerWakeup(uint64_t wakeup_time_sec);
  void disconnectBeforeSleep();
#endif
};

#endif // ADAFRUIT_MARQUEE_H
