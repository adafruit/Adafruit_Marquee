/*!
 * @file Adafruit_Marquee.cpp
 *
 * @mainpage Adafruit Marquee client for Arduino.
 *
 * @section intro_sec Introduction
 *
 * Client library for the Adafruit IO Marquee feature.
 *
 * @section author Author
 *
 * Written by Brent Rubell for Adafruit Industries.
 *
 * @section license License
 *
 * MIT license, all text here must be included in any redistribution.
 */
#include "Adafruit_Marquee.h"
#include <base64.hpp>

// for flashTransport definition
#define ADAFRUIT_MARQUEE_INTERNAL
#include "flash_config.h"

// The flash chip and the USB Mass Storage endpoint are single pieces of
// hardware and nothing outside this file touches them, so they are internal
// to this translation unit. Keep them below the flash_config.h include:
// `flash` captures &flashTransport at construction, and same-TU declaration
// order is what guarantees the transport is built first.
static Adafruit_SPIFlash flash(&flashTransport);

// file system object from SdFat
static FatVolume fatfs;
static FatFile root;
static FatFile file;

// USB Mass Storage object
static Adafruit_USBD_MSC usb_msc;

// Check if flash is formatted
bool Adafruit_Marquee::fs_formatted;
// Set to true when the PC writes to flash
volatile bool Adafruit_Marquee::fs_changed;

// Set in initMqtt(). Adafruit_MQTT_Subscribe::setCallback takes a plain
// function pointer, so the feed callbacks are static members that trampoline
// through this. A sketch only ever constructs one Adafruit_Marquee.
Adafruit_Marquee *Adafruit_Marquee::_instance = nullptr;

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
static int32_t msc_read_cb(uint32_t lba, void *buffer, uint32_t bufsize) {
  // Note: SPIFLash Block API: readBlocks/writeBlocks/syncBlocks
  // already include 4K sector caching internally. We don't need to cache it,
  // yahhhh!!
  return flash.readBlocks(lba, (uint8_t *)buffer, bufsize / 512) ? bufsize : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
static int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
  // Note: SPIFLash Block API: readBlocks/writeBlocks/syncBlocks
  // already include 4K sector caching internally. We don't need to cache it,
  // yahhhh!!
  return flash.writeBlocks(lba, buffer, bufsize / 512) ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and
// accepted by host). used to flush any pending cache.
static void msc_flush_cb(void) {
  // sync with flash
  flash.syncBlocks();
  // clear file system's cache to force refresh
  fatfs.cacheClear();
  Adafruit_Marquee::fs_changed = true;
}

/*!
    @brief  Factory function that constructs and begins an Adafruit_EPD panel.
*/
using FnCreateAdafruit_EPD = std::function<Adafruit_EPD *(
    int16_t, int16_t, int16_t, int16_t, int16_t, SPIClass *, thinkinkmode_t)>;

/*! @brief Maps a config panel identifier to its Adafruit_EPD factory. */
using Adafruit_EPDFactory = std::map<std::string, FnCreateAdafruit_EPD>;

/*!
    @brief  Config panel identifier to Adafruit_EPD factory table.

    Keys are the `display.panel` values accepted in marquee-cfg.json and are
    matched verbatim, so adding support for a panel means adding one entry
    here. The identifiers follow the `<size>-<mode>-<suffix>` shape of the
    corresponding ThinkInk class name, with the mode lowercased.
    @return A reference to the factory map.
*/
static const Adafruit_EPDFactory &getAdafruitEPDFactory() {
  static const Adafruit_EPDFactory adafruitEPDFactory = {
      {"290-grayscale4-FPC7519",
       [](int16_t dc, int16_t rst, int16_t cs, int16_t sram_cs, int16_t busy,
          SPIClass *spi, thinkinkmode_t mode) -> Adafruit_EPD * {
         auto *d = new ThinkInk_290_Grayscale4_FPC7519(dc, rst, cs, sram_cs,
                                                       busy, spi);
         d->begin(mode);
         return d;
       }},
      {"213-tricolor-MFGNR",
       [](int16_t dc, int16_t rst, int16_t cs, int16_t sram_cs, int16_t busy,
          SPIClass *spi, thinkinkmode_t mode) -> Adafruit_EPD * {
         auto *d =
             new ThinkInk_213_Tricolor_MFGNR(dc, rst, cs, sram_cs, busy, spi);
         d->begin(mode);
         return d;
       }},
      {"290-grayscale4-EAAMFGN",
       [](int16_t dc, int16_t rst, int16_t cs, int16_t sram_cs, int16_t busy,
          SPIClass *spi, thinkinkmode_t mode) -> Adafruit_EPD * {
         auto *d = new ThinkInk_290_Grayscale4_EAAMFGN(dc, rst, cs, sram_cs,
                                                       busy, spi);
         d->begin(mode);
         return d;
       }},
      {"magtag-2025",
       [](int16_t dc, int16_t rst, int16_t cs, int16_t sram_cs, int16_t busy,
          SPIClass *spi, thinkinkmode_t mode) -> Adafruit_EPD * {
         auto *d = new ThinkInk_290_Grayscale4_EAAMFGN(dc, rst, cs, sram_cs,
                                                       busy, spi);
         d->begin(mode);
         return d;
       }},
  };
  return adafruitEPDFactory;
}

/*!
    @brief  Callback for bitmap feed messages. Forwards the payload to the
            instance registered by initMqtt().
    @param  data  The message payload: a base64-encoded BMP. Adafruit_MQTT
                  nul-terminates this, so it can be treated as a C string.
    @param  len   Payload length, in bytes.
*/
void Adafruit_Marquee::cbBitmapMsg(char *data, uint16_t len) {
  if (!_instance || !data || len == 0)
    return;
  _instance->decodeb64Bmp(data, len);
}

/*!
    @brief  Callback for sleep feed messages.
    @param  data  The message payload.
    @param  len   Payload length, in bytes.
*/
void Adafruit_Marquee::cbSleepMsg(char *data, uint16_t len) {
  if (!data || len == 0)
    return;
  MQ_DEBUG_PRINT("Sleep feed received <- ");
  MQ_DEBUG_PRINTLN(data);
}

/*!
 * @brief Creates a new instance of the Adafruit Marquee client.
 */
Adafruit_Marquee::Adafruit_Marquee() {
  _display = nullptr;
  _mqtt = nullptr;
  _last_ping = 0;
  _last_wifi_attempt = 0;
  _last_mqtt_attempt = 0;
  _mqtt_retry_ms = MQ_MQTT_RETRY_MS;
  _sub_bmp = nullptr;
  _sub_sleep = nullptr;
  _pending_bmp = nullptr;
  _pending_len = 0;
  _width = 0;
  _height = 0;
  _thinkInkMode = THINKINK_MONO;
  _begin_status = SUCCESS;
  _ssid = nullptr;
  _pass = nullptr;
  _aio_username = nullptr;
  _aio_key = nullptr;
  _device_name = nullptr;
  _pin_cs = -1;
  _pin_dc = -1;
  _pin_rst = -1;
  _pin_busy = -1;
  _pin_sram_cs = -1;
  _rotation = 0;
  fs_formatted = false;
  fs_changed = true;
}

/*!
 * @brief Destructor.
 */
Adafruit_Marquee::~Adafruit_Marquee() {
  if (_display) {
    delete _display;
    _display = nullptr;
  }
  if (_pending_bmp) {
    free(_pending_bmp);
    _pending_bmp = nullptr;
  }
  _pending_len = 0;
  delete _sub_bmp;
  _sub_bmp = nullptr;
  delete _sub_sleep;
  _sub_sleep = nullptr;
  // Order matters: the subscriptions above hold a back-pointer to the client,
  // and the client writes through the socket.
  delete _mqtt;
  _mqtt = nullptr;
  if (_instance == this) {
    _instance = nullptr;
  }
}

/*!
 * @brief Initializes the Marquee client.
 * @returns SUCCESS if initialization succeeded, otherwise the
 *          mq_begin_status_t describing the failure.
 */
mq_begin_status_t Adafruit_Marquee::begin() {

  // Detach USB *before* touching the flash, mirroring Wippersnapper_FS
  TinyUSBDevice.detach();
  delay(500);

  // Attempt to init. the flash chip and the file system on it
  if (!initFilesystem()) {
    // Still bring MSC up so the flash is reachable from the host to be fixed
    initUSBMSC();
    _begin_status = ERR_FS_UNFORMATTED;
    return _begin_status;
  }

  // Reattach FS
  initUSBMSC();

  // Attempt to open and parse the marquee config file
  File32 cfg = fatfs.open("/cfg-marquee.json", O_RDONLY);
  if (!cfg) {
    _begin_status = ERR_FS_NO_CFG_FILE;
    return _begin_status;
  }

  DeserializationError error = deserializeJson(_cfg_doc, cfg);
  cfg.close();
  if (error) {
    _begin_status = ERR_JSON_DESERIALIZATION;
    return _begin_status;
  }

  JsonObject display = _cfg_doc["display"];
  const char *display_panel = display["panel"];
  _rotation = display["rotation"] | 0;

  const char *display_mode = display["mode"];
  if (!parseThinkInkMode(display_mode)) {
    _begin_status = ERR_TI_MODE_UNSUPPORTED;
    return _begin_status;
  }

  // Attempt to parse SPI interface
  JsonObject interface = _cfg_doc["interface"];
  const char *interface_type = interface["type"];
  if (!interface_type || (strcmp(interface_type, "spi_epd") != 0 &&
                          strcmp(interface_type, "builtin") != 0)) {
    _begin_status = ERR_IFACE_UNSUPPORTED;
    return _begin_status;
  }

  JsonObject pins = interface["pins"];
  _pin_cs = pins["cs"] | -1;
  _pin_dc = pins["dc"] | -1;
  _pin_rst = pins["reset"] | -1;
  _pin_busy = pins["busy"] | -1;
  _pin_sram_cs = pins["sram_cs"] | -1;

  if (!createEPD(display_panel)) {
    _begin_status = ERR_EPD_PANEL_UNSUPPORTED;
    return _begin_status;
  }

  _display->setRotation(_rotation);
  _display->setTextSize(3);
  _display->setTextColor(EPD_BLACK);
  _display->setTextWrap(false);
  _height = _display->height();
  _width = _display->width();
  _display->clearBuffer();
  // TODO: Only fully refresh/clear the EPD on a cold-boot, not when it wakes
  // from sleep
  /*     if (!didBootFromSleep())
        _display->display(); */

  // Parse network and Adafruit IO credentials
  _ssid = _cfg_doc["network"]["wifi_ssid"];
  _pass = _cfg_doc["network"]["wifi_password"];
  _aio_username = _cfg_doc["adafruit_io"]["username"];
  _aio_key = _cfg_doc["adafruit_io"]["key"];
  _device_name = _cfg_doc["name"];
  // Validate
  if (!_ssid || !_pass || !_aio_username || !_aio_key || !_device_name) {
    _begin_status = ERR_JSON_DESERIALIZATION;
    return _begin_status;
  }

  _begin_status = SUCCESS;
  return _begin_status;
}

/*!
    @brief  Initializes the flash chip and mounts the FAT filesystem on it.
    @return True if the flash came up and the filesystem mounted, else False.
*/
bool Adafruit_Marquee::initFilesystem() {
  if (!flash.begin()) {
    return false;
  }

  fs_formatted = fatfs.begin(&flash);
  return fs_formatted;
}

/*!
    @brief  Brings up the USB MSC endpoint, attaches the USB device
*/
void Adafruit_Marquee::initUSBMSC() {
  usb_msc.setID("Adafruit", "External Flash", "1.0");
  usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
  usb_msc.setCapacity(flash.size() / 512, 512);
  usb_msc.setUnitReady(true);
  usb_msc.begin();

  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
  }
  TinyUSBDevice.attach();

  // Give time for host to enumerate
  delay(500);
}

/*!
 * @brief Builds the MQTT client, via the subclass, and the feed
 *        subscriptions over it.
 * @returns True if initialization succeeded, False otherwise.
 */
bool Adafruit_Marquee::initMqtt() {
  _instance = this;

  // Attempt to create the MQTT client
  setupMQTTClient();
  if (!_mqtt) {
    MQ_DEBUG_PRINTLN("[mqtt] ERROR: could not create the MQTT client");
    return false;
  }
  if (_mqtt->bufferSize() == 0) {
    MQ_DEBUG_PRINTF("[mqtt] ERROR: could not allocate %u byte packet buffer\n",
                    (unsigned)MQ_MQTT_BUFFER_LEN);
    return false;
  }

  if (!_mqtt->setKeepAliveInterval(MQ_MQTT_KEEPALIVE_SEC)) {
    MQ_DEBUG_PRINTLN("[mqtt] ERROR: could not set the keepalive interval");
    return false;
  }

  // Initialize Adafruit IO feed names
  snprintf(_feed_name_bmp, sizeof(_feed_name_bmp), "%s.bitmap", _device_name);
  snprintf(_topic_bmp, sizeof(_topic_bmp), "%s/f/%s/csv", _aio_username,
           _feed_name_bmp);
  _sub_bmp = new Adafruit_MQTT_Subscribe(_mqtt, _topic_bmp, 0, MQ_BITMAP_SUB_LEN);
  if (!_sub_bmp || !_sub_bmp->lastread) {
    MQ_DEBUG_PRINTLN("[bmp] ERROR: Couldn't create the bitmap feed");
    return false;
  }
  _sub_bmp->setCallback(cbBitmapMsg);

  snprintf(_feed_name_sleep, sizeof(_feed_name_sleep), "%s.sleep",
           _device_name);
  snprintf(_topic_sleep, sizeof(_topic_sleep), "%s/f/%s/csv", _aio_username,
           _feed_name_sleep);
  _sub_sleep = new Adafruit_MQTT_Subscribe(_mqtt, _topic_sleep);
  if (!_sub_sleep) {
    MQ_DEBUG_PRINTLN("[sleep] ERROR: could not create the sleep feed");
    return false;
  }
  _sub_sleep->setCallback(cbSleepMsg);

  // Attempt to register subscriptions
  if (!_mqtt->subscribe(_sub_bmp) || !_mqtt->subscribe(_sub_sleep)) {
    MQ_DEBUG_PRINTLN("Failed to register MQTT subscriptions");
    return false;
  }

  MQ_DEBUG_PRINT("Subscribed to BMP feed: ");
  MQ_DEBUG_PRINTLN(_topic_bmp);
  MQ_DEBUG_PRINT("Subscribed to sleep feed: ");
  MQ_DEBUG_PRINTLN(_topic_sleep);
  return true;
}

/*!
    @brief  Attempts to connect to the WiFi network
    @param  timeout  Maximum time to wait for a connection, in milliseconds.
    @return True once the platform reports an association, False otherwise
*/
bool Adafruit_Marquee::initWifi(unsigned long timeout) {
  if (!_ssid || strlen(_ssid) == 0) {
    MQ_DEBUG_PRINTLN("[wifi] ERROR: SSID not found in config file!");
    return false;
  }

  MQ_DEBUG_PRINTF("[wifi] connecting to '%s'\n", _ssid);
  _connect();
  delay(timeout);
  _last_wifi_attempt = millis();

  if (!networkConnected()) {
    MQ_DEBUG_PRINTLN("[wifi] ERROR: timed out, could not connect to WiFi network");
    return false;
  }

  return true;
}

/*!
    @brief  Opens the MQTT session. Credentials went in at construction, so
            this is the no-argument connect().
    @return True on CONNACK success, else False.
*/
bool Adafruit_Marquee::connectMqtt() {
  _last_mqtt_attempt = millis();
  int8_t rc = _mqtt->connect();
  if (rc == 0) {
    _mqtt_retry_ms = MQ_MQTT_RETRY_MS;
    return true;
  }

  MQ_DEBUG_PRINT("[mqtt] connect failed: ");
  MQ_DEBUG_PRINTLN(_mqtt->connectErrorString(rc));
  _mqtt_retry_ms = MQ_MQTT_RETRY_MS;
  return false;
}

/*!
 * @brief Connects to WiFi and the Adafruit IO MQTT broker.
 * @param timeout The maximum time to wait for a connection, in milliseconds.
 * @returns True if connection succeeded, otherwise false.
 */
bool Adafruit_Marquee::connect(unsigned long timeout) {
  if (!initMqtt()) {
    MQ_DEBUG_PRINTLN("Failed to initialize the MQTT client and feeds");
    return false;
  }

  if (!initWifi(timeout)) {
    return false;
  }

  if (!connectMqtt()) {
    return false;
  }

  // Force the bitmap feed to publish its last data point
  char topic[sizeof(_topic_bmp)];
  snprintf(topic, sizeof(topic), "%s/f/%s/csv/get", _aio_username,
           _feed_name_bmp);
  Adafruit_MQTT_Publish pub_get(_mqtt, topic);
  pub_get.publish("\0");

  return true;
}

/*!
    @brief  Draws the bitmap queued by the bitmap feed callback, if any.
*/
void Adafruit_Marquee::drawBitmap() {
  if (!_pending_bmp || !_display)
    return;

  _display->clearBuffer();

  uint32_t t_decode_start = millis();
  ImageReturnCode rc =
      _reader.drawBMP(_pending_bmp, _pending_len, *_display, 0, 0);
  uint32_t t_decode = millis() - t_decode_start;
  (void)t_decode; // only read by the debug print below

  if (rc != IMAGE_SUCCESS) {
    MQ_DEBUG_PRINTF("[display] ERROR: drawBMP rc: %d\n", (int)rc);
  } else {
    uint32_t t_refresh_start = millis();
    _display->display();
    (void)t_refresh_start;
    MQ_DEBUG_PRINTF("[display] decode ms: %u refresh ms: %u\n",
                    (unsigned)t_decode, (unsigned)(millis() - t_refresh_start));
  }

  // Free the bitmap buffer and clear flags
  free(_pending_bmp);
  _pending_bmp = nullptr;
  _pending_len = 0;
}

/*!
    @brief  Keeps WiFi and the MQTT session up and pumps incoming packets.
*/
void Adafruit_Marquee::handleConnection() {
  if (!_mqtt)
    return;
  // Is the WiFi network still connected?
  if (!networkConnected()) {
    if (millis() - _last_wifi_attempt >= MQ_WIFI_RETRY_MS) {
      MQ_DEBUG_PRINTLN("[wifi] disconnected, re-associating");
      _last_wifi_attempt = millis();
      _connect();
    }
    // Returns to the loop - retires later
    return;
  }

  // Is the MQTT session still connected?
  if (!_mqtt->connected()) {
    if (millis() - _last_mqtt_attempt < _mqtt_retry_ms) {
      return;
    }
    MQ_DEBUG_PRINTLN("[mqtt] disconnected, reconnecting");
    if (!connectMqtt()) {
      return;
    }
    return;
  }

  // Attempt to process incoming packets
  _mqtt->processPackets(100);

  // Attempt to ping the broker within the keepalive interval.
  if (millis() - _last_ping >= (MQ_MQTT_KEEPALIVE_SEC * 1000UL) / 4) {
    _last_ping = millis();
    _mqtt->ping();
  }
}

/*!
    @brief  Blocking application loop for handling network state, drawing to the
   display and sleep modes.
*/
void Adafruit_Marquee::run() {
  // Keeps the WiFi and MQTT session up, dispatches any incoming feed messages
  handleConnection();

  // Draw any bitmap queued by the bitmap feed callback
  drawBitmap();
  // TODO: Handle sleep feed messages
}

/*!
    @brief  Finds and initializes the display panel from the configured
            panel identifier.
    @param  panel  The EPD panel name (e.g., "290-grayscale4-FPC7519").
    @return True if the panel was found and initialized, False otherwise.
*/
bool Adafruit_Marquee::createEPD(const char *panel) {
  if (!panel) {
    return false;
  }

  // Look up the panel identifier in the factory table and create the instance
  const Adafruit_EPDFactory &adafruitEPDFactory = getAdafruitEPDFactory();
  Adafruit_EPDFactory::const_iterator it = adafruitEPDFactory.find(panel);
  if (it == adafruitEPDFactory.end()) {
    for (Adafruit_EPDFactory::const_iterator k = adafruitEPDFactory.begin();
         k != adafruitEPDFactory.end(); ++k) {
    }
    return false;
  }

  // Creates the panel instance using the factory function
  _display = it->second(_pin_dc, _pin_rst, _pin_cs, _pin_sram_cs, _pin_busy,
                        &SPI, _thinkInkMode);
  if (_display == nullptr) {
    return false;
  }

  return true;
}

/*!
 * @brief Parses the ThinkInk mode from the Marquee config file.
 * @param mode The ThinkInk mode string to parse.
 * @returns true if the mode was successfully parsed, otherwise false.
 */
bool Adafruit_Marquee::parseThinkInkMode(const char *mode) {
  if (!mode)
    return false;

  if (strcmp(mode, "THINKINK_MONO") == 0) {
    _thinkInkMode = THINKINK_MONO;
  } else if (strcmp(mode, "THINKINK_TRICOLOR") == 0) {
    _thinkInkMode = THINKINK_TRICOLOR;
  } else if (strcmp(mode, "THINKINK_GRAYSCALE4") == 0) {
    _thinkInkMode = THINKINK_GRAYSCALE4;
  } else if (strcmp(mode, "THINKINK_MONO_PARTIAL") == 0) {
    _thinkInkMode = THINKINK_MONO_PARTIAL;
  } else if (strcmp(mode, "THINKINK_QUADCOLOR") == 0) {
    _thinkInkMode = THINKINK_QUADCOLOR;
  } else {
    return false;
  }
  return true;
}

/*!
    @brief  Decodes and saves a base64-encoded BMP, next run() draws it to the
   display.
    @param  b64      Desired b64-encoded payload to decode.
    @param  b64_len  Expected length of the payload.
    @return True if a bitmap was decoded and queued, False otherwise.
*/
bool Adafruit_Marquee::decodeb64Bmp(const char *b64, size_t b64_len) {
  if (!b64 || b64_len == 0) {
    MQ_DEBUG_PRINTLN("[bmp] ERROR: null payload or zero length");
    return false;
  }

  // First, decode the payload's length without allocating a buffer
  size_t decoded_len =
      decode_base64_length((const unsigned char *)b64, (unsigned int)b64_len);
  if (decoded_len < MIN_SZ_BMP_HEADER || decoded_len > MQ_MQTT_BUFFER_LEN) {
    MQ_DEBUG_PRINTF("[bmp] ERROR: implausible decoded length: %u\n",
                    (unsigned)decoded_len);
    return false;
  }

  // Then, attempt to allocate a buffer for the decoded BMP
  uint8_t *buf = (uint8_t *)ps_malloc(decoded_len);
  if (!buf) {
    buf = (uint8_t *)malloc(decoded_len);
  }
  // Did allocation fail?
  if (!buf) {
    MQ_DEBUG_PRINTF("[bmp] ERROR: alloc of %u bytes failed\n",
                    (unsigned)decoded_len);
    return false;
  }

  // Attempt to decode the base64 payload into thebuffer
  unsigned int write_len =
      decode_base64((const unsigned char *)b64, (unsigned int)b64_len, buf);
  if (write_len != decoded_len) {
    MQ_DEBUG_PRINTF("[bmp] ERROR: decoded %u of %u bytes\n", write_len,
                    (unsigned)decoded_len);
    free(buf);
    return false;
  }

  // Store and queue the decoded BMP for the next run() to draw
  if (_pending_bmp) {
    free(_pending_bmp);
  }
  _pending_bmp = buf;
  _pending_len = decoded_len;

  MQ_DEBUG_PRINTF("[bmp] queued: %u b64 chars -> %u bytes\n", (unsigned)b64_len,
                  (unsigned)decoded_len);
  return true;
}
