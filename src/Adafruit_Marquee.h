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
#include <AdafruitIO_WiFi.h>
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

    Prints the step, the calling task's remaining stack, and free internal/PSRAM
    heap, then flushes. This exists because the ESP32-S2 has no USB-Serial-JTAG
    peripheral: its USB is OTG driven in software by TinyUSB, the IDF console is
    UART0 (CONFIG_ESP_CONSOLE_UART_NUM=0), and the panic handler runs with
    interrupts off - so a backtrace goes out GPIO43 and never reaches the USB
    CDC port. With PANIC_PRINT_REBOOT and a 0s delay, the last line that made it
    out over USB is the only evidence of where execution stopped.

    stack= is the high-water mark in bytes remaining; if it trends toward 0 the
    fault is stack exhaustion (loopTask defaults to 8KB) rather than a bad
    pointer.
*/
#if MARQUEE_DEBUG
#define MQ_TRACE(step)                                                         \
  do {                                                                         \
    Serial.printf("[trace] %-24s stack=%u heap=%u psram=%u\n", (step),         \
                  (unsigned)uxTaskGetStackHighWaterMark(NULL),                 \
                  (unsigned)ESP.getFreeHeap(),                                 \
                  (unsigned)ESP.getFreePsram());                               \
    Serial.flush();                                                            \
  } while (0)
#else
#define MQ_TRACE(step)                                                         \
  do {                                                                         \
  } while (0) ///< Disabled
#endif

/*!
    @brief  Sanity ceiling on a decoded BMP. A 24bpp full-panel image is ~114KB;
            past this the length is malformed, not an image we meant to draw.
*/
#define MQ_MAX_BMP_BYTES (128UL * 1024UL)
/*!
    @brief  base64 expansion of MIN_SZ_BMP_HEADER (54) bytes: ceil(54/3)*4.
*/
#define MQ_MIN_BMP_B64_LEN 72
/*!
    @brief  Payload buffer for the bitmap subscription, in bytes. Must hold a
            whole MQTT PUBLISH payload: ~20,980 base64 chars for a 250x122 4bpp
            BMP, plus headroom. Adafruit_MQTT's MAXBUFFERSIZE must be at least
            this plus the fixed header, remaining-length varint and topic.
*/
#define MQ_BITMAP_SUB_LEN 22528

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
    @brief  Exposes AdafruitIO's MQTT client.

    AdafruitIO keeps _mqtt protected and only befriends its own feed/group
    classes, but a derived class may reach it. That lets the bitmap feed use a
    raw Adafruit_MQTT_Subscribe with a large per-subscription payload buffer,
    bypassing AdafruitIO_Data - whose _value is 45 bytes (AIO_DATA_LENGTH) and
    is filled by an unbounded strcpy, so it cannot carry a bitmap.

    Note AdafruitIO_WiFi is a typedef (AdafruitIO_ESP32 on ESP32), not a class,
    so the constructor is spelled out rather than inherited with `using`.
*/
class MarqueeIO : public AdafruitIO_WiFi {
public:
  /*!
      @brief  Constructs the Adafruit IO WiFi client.
      @param  user  Adafruit IO username.
      @param  key   Adafruit IO key.
      @param  ssid  WiFi SSID.
      @param  pass  WiFi password.
  */
  MarqueeIO(const char *user, const char *key, const char *ssid,
            const char *pass)
      : AdafruitIO_WiFi(user, key, ssid, pass) {}
  /*!
      @brief  Returns the underlying MQTT client.
      @return Pointer to the Adafruit_MQTT client, or nullptr if unbuilt.
  */
  Adafruit_MQTT *mqtt() { return _mqtt; }
};

/*!
 * @brief Client for the Adafruit IO Marquee feature.
 */
class Adafruit_Marquee {
public:
  Adafruit_Marquee();
  ~Adafruit_Marquee();
  mq_begin_status_t begin();
  bool connect(unsigned long timeout = 30000);
  void run();
  bool queueBitmapBase64(const char *b64);

  static bool fs_formatted;
  static volatile bool fs_changed;
private:
  mq_begin_status_t _begin_status;
  JsonDocument _cfg_doc;
  bool initFilesystem();
  void initUSBMSC();
  bool initAIO();
  bool createEPD(const char *panel);
  bool parseThinkInkMode(const char* mode);
  bool draw(const uint8_t *bmp, size_t len);
  void servicePendingDraw();
  static void cbBitmapMsg(char *data, uint16_t len);
  static void cbSleepMsg(char *data, uint16_t len);
  /*!
      @brief  The instance the MQTT callbacks dispatch to. Adafruit_MQTT takes
              plain function pointers, so the callbacks are static members that
              trampoline through this. Set by initAIO().
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
  // queueBitmapBase64() and freed in servicePendingDraw() or the destructor.
  uint8_t *_pending_bmp; ///< Decoded BMP bytes, or nullptr
  size_t _pending_len;   ///< Byte length of _pending_bmp
  // Plain bool, not volatile: the MQTT callbacks are dispatched synchronously
  // on the calling task from inside _io->run(), not from an ISR.
  bool _pending_draw; ///< Set by cbBitmapMsg(), cleared by servicePendingDraw()
  // Networking
  const char* _ssid; ///< WiFi SSID
  const char* _pass; ///< WiFi password
  // Adafruit IO
  const char* _aio_username; ///< Adafruit IO username
  const char* _aio_key; ///< Adafruit IO key
  const char *_device_name; ///< Device name for Adafruit IO
  MarqueeIO *_io; ///< Pointer to the Adafruit IO WiFi client
  // The bitmap and sleep feeds both use raw subscriptions rather than
  // AdafruitIO_Feed: the bitmap payload does not fit AdafruitIO_Data's 45-byte
  // _value, and routing the sleep feed the same way keeps one code path.
  Adafruit_MQTT_Subscribe *_sub_bmp;   ///< Subscription for the bitmap topic
  Adafruit_MQTT_Subscribe *_sub_sleep; ///< Subscription for the sleep topic
  Adafruit_MQTT_Publish *_pub_bmp_get; ///< Publishes to the bitmap /get topic
  char _feed_name_bmp[MAX_IO_FEED_NAME_LEN];   ///< Feed key for the bitmap feed
  char _feed_name_sleep[MAX_IO_FEED_NAME_LEN]; ///< Feed key for the sleep feed
  char _topic_bmp[MAX_IO_FEED_NAME_LEN + 96];     ///< <user>/f/<feed>/csv
  char _topic_bmp_get[MAX_IO_FEED_NAME_LEN + 96]; ///< <user>/f/<feed>/csv/get
  char _topic_sleep[MAX_IO_FEED_NAME_LEN + 96];   ///< <user>/f/<feed>/csv
};

#endif // ADAFRUIT_MARQUEE_H
