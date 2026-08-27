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

#define MAX_IO_FEED_NAME_LEN 128

/*!
    @brief  Serial tracing. Build with -DMARQUEE_DEBUG=0 to compile it out.
*/
#ifndef MARQUEE_DEBUG
#define MARQUEE_DEBUG 1
#endif
#if MARQUEE_DEBUG
#define MQ_DEBUG_PRINT(...) Serial.print(__VA_ARGS__)     ///< Trace, no newline
#define MQ_DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__) ///< Trace + newline
#define MQ_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)   ///< Formatted trace
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
#endif

/*!
    @brief  Bring-up step trace.

    Prints the step and flushes. This exists because the ESP32-S2 has no
    USB-Serial-JTAG peripheral: its USB is OTG driven in software by TinyUSB,
    the IDF console is UART0 (CONFIG_ESP_CONSOLE_UART_NUM=0), and the panic
    handler runs with interrupts off - so a backtrace goes out GPIO43 and never
    reaches the USB CDC port. With PANIC_PRINT_REBOOT and a 0s delay, the last
    step that made it out over USB is the only evidence of where execution
    stopped.
*/
#if MARQUEE_DEBUG
#define MQ_TRACE(step)                                                         \
  do {                                                                         \
    Serial.printf("[trace] %s\n", (step));                                     \
    Serial.flush();                                                            \
  } while (0)
#else
#define MQ_TRACE(step)                                                         \
  do {                                                                         \
  } while (0) ///< Disabled
#endif

#define MQ_BITMAP_SUB_LEN                                                      \
  81920 ///< Holds the payload for the bitmap subscription feed, in bytes (Sized
        ///< for a 4.2" Tricolor ThinkInk panel)
#define MQ_MQTT_BUFFER_LEN                                                     \
  (MQ_BITMAP_SUB_LEN + 256) ///< Packet buffer for the MQTT client + 256 bytes
                            ///< of headroom for the topic, in bytes

/*! @brief  Adafruit IO MQTT host. */
#define MQ_IO_HOST "io.adafruit.us"
/*! @brief  Adafruit IO MQTT port, TLS. */
#define MQ_IO_MQTT_PORT 8883

/*!
    @brief  Keepalive the client asks for in CONNECT, in seconds. Overrides the
            library's 300s MQTT_CONN_KEEPALIVE default: shorter means the broker
            notices a dead link sooner, and Adafruit IO caps how long a client
            may ask for anyway.
*/
#define MQ_MQTT_KEEPALIVE_SEC 180
/*! @brief  Minimum wait between WiFi association attempts, in milliseconds. */
#define MQ_WIFI_RETRY_MS 5000
/*! @brief  Minimum wait between MQTT connect attempts, in milliseconds. */
#define MQ_MQTT_RETRY_MS 10000
/*!
    @brief  Wait after a CONNACK that rejected us on protocol or credentials,
            in milliseconds. Retrying those at MQ_MQTT_RETRY_MS just hammers
            the broker with a request that cannot start succeeding on its own.
*/
#define MQ_MQTT_FATAL_RETRY_MS 60000

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
  static void cbBitmapMsg(char *data,
                          uint16_t len); ///< Callback for bitmap feed messages
  static void cbSleepMsg(char *data,
                         uint16_t len); ///< Callback for sleep feed messages

  // Network interface within networking/
  virtual void _connect() = 0;
  virtual void _disconnect() = 0;
};

#endif // ADAFRUIT_MARQUEE_H
