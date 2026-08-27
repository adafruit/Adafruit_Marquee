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

#include "Arduino.h"
#include <functional>
#include <map>
#include <Adafruit_ThinkInk.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <ArduinoJson.h>
#include "Adafruit_TinyUSB.h"
#include "SdFat_Adafruit_Fork.h"
#include "Adafruit_ImageReader_EPD.h"

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

#define MQ_BITMAP_SUB_LEN 81920 ///< Holds the payload for the bitmap subscription feed, in bytes (Sized for a 4.2" Tricolor ThinkInk panel)
#define MQ_MQTT_BUFFER_LEN (MQ_BITMAP_SUB_LEN + 256) ///< Packet buffer for the MQTT client + 256 bytes of headroom for the topic, in bytes

/*! @brief  Adafruit IO MQTT host. */
#define MQ_IO_HOST "io.adafruit.us"
/*! @brief  Adafruit IO MQTT port, TLS. */
#define MQ_IO_MQTT_PORT 8883

/*!
    @brief  How long processPackets() spends waiting on the socket per run(),
            in milliseconds. Short enough that run() stays responsive for the
            EPD draw path; the value Adafruit IO's client used by default.
*/
#define MQ_PACKET_READ_MS 100
/*!
    @brief  PINGREQ interval, in milliseconds. Well inside the 300s
            MQTT_CONN_KEEPALIVE the client negotiates.
*/
#define MQ_PING_INTERVAL_MS 60000
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
  bool decodeb64Bmp(const char *b64, size_t b64_len);
  static volatile bool fs_changed;

  // Network interface within networking/
  virtual bool networkConnected() = 0;
  virtual int networkStatus() = 0;
  virtual const char *connectionType() = 0;
  /*!
      @brief  Builds _mqtt over the platform's secure socket.

      Called by initMQTT(), after begin() has parsed the config - never from a
      constructor. Adafruit_MQTT stores the credential pointers it is handed
              rather than copying them, and they are null until begin() runs.
      Must leave _mqtt non-null on success.
  */
  virtual void setupMQTTClient() = 0;
protected:
  static bool fs_formatted;
  mq_begin_status_t _begin_status;
  JsonDocument _cfg_doc;
  bool initFilesystem();
  void initUSBMSC();
  bool initMQTT();
  bool connectWiFi(unsigned long timeout);
  bool connectMQTT();
  void maintainConnection();
  void requestBitmap();
  bool createEPD(const char *panel);
  bool parseThinkInkMode(const char* mode);
  bool draw(const uint8_t *bmp, size_t len);
  void servicePendingDraw();
  static void cbBitmapMsg(char *data, uint16_t len);
  static void cbSleepMsg(char *data, uint16_t len);
  /*!
      @brief  The instance the MQTT callbacks dispatch to. Adafruit_MQTT takes
              plain function pointers, so the callbacks are static members that
              trampoline through this. Set by initMQTT().
  */
  static Adafruit_Marquee *_instance;
  Adafruit_EPD *_display; ///< Pointer to the EPD display object
  Adafruit_ImageReader_EPD _reader; ///< In-memory BMP decoder for the EPD
  thinkinkmode_t _thinkInkMode; ///< ThinkInk mode for the display
  int16_t _pin_cs; ///< Chip select pin for EPD
  int16_t _pin_dc; ///< Data/Command pin for EPD
  int16_t _pin_rst; ///< Reset pin for EPD
  int16_t _pin_busy; ///< Busy pin for EPD
  int16_t _pin_sram_cs; ///< SRAM chip select pin for EPD
  uint8_t _rotation; ///< Display rotation (0-3)
  int16_t _width;    ///< Panel width in pixels, post-rotation
  int16_t _height;   ///< Panel height in pixels, post-rotation
  // Bitmap awaiting draw. Owned by this object, allocated in
  // decodeb64Bmp() and freed in servicePendingDraw() or the destructor.
  uint8_t *_pending_bmp; ///< Decoded BMP bytes, or nullptr
  size_t _pending_len;   ///< Byte length of _pending_bmp
  // Plain bool, not volatile: the MQTT callbacks are dispatched synchronously
  // on the calling task from inside processPackets(), not from an ISR.
  bool _pending_draw; ///< Set by cbBitmapMsg(), cleared by servicePendingDraw()
  // Networking
  const char* _ssid; ///< WiFi SSID
  const char* _pass; ///< WiFi password
  // Adafruit IO
  const char* _aio_username; ///< Adafruit IO username
  const char* _aio_key; ///< Adafruit IO key
  const char *_device_name; ///< Device name for Adafruit IO
  Adafruit_MQTT_Client *_mqtt; ///< MQTT client, owns the packet buffer
  unsigned long _last_ping;         ///< millis() of the last PINGREQ
  unsigned long _last_wifi_attempt; ///< millis() of the last attempt
  unsigned long _last_mqtt_attempt; ///< millis() of the last MQTT connect()
  /*!
      @brief  How long maintainConnection() waits before retrying MQTT. Bumped
              to MQ_MQTT_FATAL_RETRY_MS by a CONNACK that rejected the protocol
              level or the credentials.
  */
  unsigned long _mqtt_retry_ms;
  // Both feeds are plain MQTT topics rather than an Adafruit IO feed wrapper:
  // that wrapper copies the payload into a 45-byte value buffer, which a
  // bitmap would overrun, and routing the sleep feed the same way keeps one
  // code path.
  Adafruit_MQTT_Subscribe *_sub_bmp;   ///< Subscription for the bitmap topic
  Adafruit_MQTT_Subscribe *_sub_sleep; ///< Subscription for the sleep topic
  char _feed_name_bmp[MAX_IO_FEED_NAME_LEN];   ///< Feed key for the bitmap feed
  char _feed_name_sleep[MAX_IO_FEED_NAME_LEN]; ///< Feed key for the sleep feed
  // The bitmap /get topic is not kept: requestBitmap() builds it from
  // _feed_name_bmp on demand, since it is only needed once per (re)connect.
  char _topic_bmp[MAX_IO_FEED_NAME_LEN + 96];   ///< <user>/f/<feed>/csv
  char _topic_sleep[MAX_IO_FEED_NAME_LEN + 96]; ///< <user>/f/<feed>/csv

  // Network interface within networking/
  virtual void _connect() = 0;
  virtual void _disconnect() = 0;
};

#endif // ADAFRUIT_MARQUEE_H
