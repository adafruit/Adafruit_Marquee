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
#include <CRC.h>

Adafruit_Marquee *Adafruit_Marquee::_instance = nullptr; ///< Pointer to the instance that the MQTT callbacks dispatch to

// For ESP32 sleep modes
#ifdef ARDUINO_ARCH_ESP32
#include <esp_sleep.h>
#include <esp_wifi.h>
static RTC_DATA_ATTR uint32_t prvBmpCrc; ///< CRC32 of the bitmap, stored before sleep
#endif // ARDUINO_ARCH_ESP32

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

static Adafruit_USBD_MSC usb_msc; ///< USB Mass Storage device object



bool Adafruit_Marquee::fs_formatted; ///< True when the filesystem is formatted, False otherwise
volatile bool Adafruit_Marquee::fs_changed; ///< True when the filesystem is changed by the host over USB MSC, False otherwise

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
 * @brief Creates a new instance of the Adafruit Marquee client.
 */
Adafruit_Marquee::Adafruit_Marquee() {
  _display = nullptr;
  _device_name = nullptr;

  _mqtt = nullptr;
  _last_ping = 0;
  _last_wifi_attempt = 0;
  _last_mqtt_attempt = 0;
  _mqtt_retry_ms = MQ_MQTT_RETRY_MS;
  _sub_bmp = nullptr;
  _sub_sleep = nullptr;
  _pending_bmp = nullptr;
  _pending_bmp_len = 0;
  _pending_crc = 0;
  _ssid = nullptr;
  _pass = nullptr;
  _aio_username = nullptr;
  _aio_key = nullptr;

  _thinkInkMode = THINKINK_MONO;
  _begin_status = SUCCESS;
  _pin_cs = -1;
  _pin_dc = -1;
  _pin_rst = -1;
  _pin_busy = -1;
  _pin_sram_cs = -1;
  _rotation = 0;
  _width = 0;
  _height = 0;

  fs_formatted = false;
  fs_changed = true;

  _sleep_mode = SLEEP_MODE_NONE;
  _sleep_alarm = SLEEP_ALARM_NONE;
  _is_sleep_pending = false;
  _sleep_time = 60; // default to 60 seconds to avoid rapid wake/sleep cycling
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
  _pending_bmp_len = 0;
  delete _sub_bmp;
  _sub_bmp = nullptr;
  delete _sub_sleep;
  _sub_sleep = nullptr;
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

  if (parseDisplayCfg(cfg) != SUCCESS) {
    return _begin_status;
  }

  const char *display_panel = _cfg_doc["display"]["panel"];
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
  // An EPD holds its last image with the power off, so a cold boot comes up
  // showing whatever was on the panel beforehand - push the cleared buffer out
  // to wipe it. A wake from sleep is resuming the image we deliberately left
  // up, so skip the refresh there and let run() redraw only on a new bitmap.
  if (!didWakeFromSleep()) {
    _display->display();
  }

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
  snprintf(_topic_sleep, sizeof(_topic_sleep), "%s/f/%s", _aio_username,
           _feed_name_sleep);
  _sub_sleep = new Adafruit_MQTT_Subscribe(_mqtt, _topic_sleep);
  if (!_sub_sleep) {
    MQ_DEBUG_PRINTLN("[sleep] ERROR: could not create the sleep feed");
    return false;
  }
  _sub_sleep->setCallback(cbSleepMsg);

  // Build status feed (publish only)
  snprintf(_feed_name_status, sizeof(_feed_name_status), "%s.status",
           _device_name);
  snprintf(_topic_status, sizeof(_topic_status), "%s/f/%s", _aio_username,
           _feed_name_status);

  // Attempt to register subscriptions
  if (!_mqtt->subscribe(_sub_bmp) || !_mqtt->subscribe(_sub_sleep)) {
    MQ_DEBUG_PRINTLN("Failed to register MQTT subscriptions");
    return false;
  }

  MQ_DEBUG_PRINT("Subscribed to BMP feed: ");
  MQ_DEBUG_PRINTLN(_topic_bmp);
  MQ_DEBUG_PRINT("Subscribed to sleep feed: ");
  MQ_DEBUG_PRINTLN(_topic_sleep);
  MQ_DEBUG_PRINT("Publishing status to: ");
  MQ_DEBUG_PRINTLN(_topic_status);
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

  // Attempt to connect to WiFi
  MQ_DEBUG_PRINTF("[wifi] connecting to '%s'\n", _ssid);
  _connect();
  unsigned long start = millis();
  while (!isNetConnected() && millis() - start < timeout) {
    delay(MQ_WIFI_POLL_MS);
  }
  _last_wifi_attempt = millis();

  if (!isNetConnected()) {
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
  _mqtt_retry_ms = MQ_MQTT_RETRY_MS;
  if (rc != 0) {
    MQ_DEBUG_PRINT("[mqtt] connect failed: ");
    MQ_DEBUG_PRINTLN(_mqtt->connectErrorString(rc));
    return false;
  }

  // Publish to the status feed that the device is awake + why it is awake
  JsonDocument doc;
  doc["state"] = "awake";
  doc["wake_reason"] = wakeupReason();
  char payload[128];
  size_t len = serializeJson(doc, payload, sizeof(payload));
  if (len == 0 || len >= sizeof(payload)) {
    MQ_DEBUG_PRINTLN("[status] ERROR: could not serialize the awake payload");
  } else {
    publishStatus(payload);
  }

  // Ask for IO to republish the last data point on the bitmap feed
  getFromFeed(_topic_bmp);

  // If we're waking from sleep, the device needs to know how it'll enter sleep again
  if (didWakeFromSleep()) {
    MQ_DEBUG_PRINTLN("[sleep] Device woke from sleep!");
    getFromFeed(_topic_sleep);
  }

  return true;
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

  // connectMqtt() also asks the feeds to republish, so there is nothing left
  // for this function to do once the session is up.
  return connectMqtt();
}

/*!
    @brief  Asks a feed to republish its last data point.
    @param  topic  Subscribed topic to republish from.
*/
void Adafruit_Marquee::getFromFeed(const char *topic) {
  if (!_mqtt || !topic || topic[0] == '\0') {
    return;
  }

  // Ask the feed to republish its last stored data point
  char get_topic[sizeof(_topic_bmp) + 8];
  snprintf(get_topic, sizeof(get_topic), "%s/get", topic);
  Adafruit_MQTT_Publish pub_get(_mqtt, get_topic);
  pub_get.publish("\0");
}

/*!
    @brief  Publishes a payload to the device's status feed.
    @param  payload  The message to publish.
    @return True if the publish succeeded, False otherwise.
*/
bool Adafruit_Marquee::publishStatus(const char *payload) {
  if (!_mqtt || _topic_status[0] == '\0') {
    MQ_DEBUG_PRINTLN("[status] ERROR: Cannot publish, MQTT not ready");
    return false;
  }
  if (!_mqtt->connected()) {
    MQ_DEBUG_PRINTLN("[status] ERROR: Cannot publish, MQTT not connected");
    return false;
  }

  Adafruit_MQTT_Publish pub_status(_mqtt, _topic_status);
  if (!pub_status.publish(payload)) {
    MQ_DEBUG_PRINTLN("[status] ERROR: Publish failed");
    return false;
  }

  MQ_DEBUG_PRINT("[status] PUBLISHED -> ");
  MQ_DEBUG_PRINTLN(payload);
  return true;
}

/*!
    @brief  Reads the CRC32 of the bitmap drawn to the panel (prior to sleep)
    @param  crc  If this library returns True, set to the CRC32 of the b64 payload.
    @return True if a CRC was successfully stored, False otherwise.
*/
bool Adafruit_Marquee::loadPrvBmpCRC(uint32_t &crc) {
#ifdef ARDUINO_ARCH_ESP32
  if (prvBmpCrc == 0)
    return false;
  crc = prvBmpCrc;
  return true;
#else
  return false;
#endif // ARDUINO_ARCH_ESP32
}

/*!
    @brief  Records the CRC32 of the bitmap drawn to the panel.
    @param  crc  CRC32 of the base64 payload that produced the bitmap.
*/
void Adafruit_Marquee::storeBmpCRC(uint32_t crc) {
#ifdef ARDUINO_ARCH_ESP32
  prvBmpCrc = crc;
#else
  (void)crc;
#endif // ARDUINO_ARCH_ESP32
}


/*!
    @brief  Draws the bitmap queued by the bitmap feed callback, if any.
*/
void Adafruit_Marquee::drawBitmap() {
  if (!_pending_bmp || !_display)
    return;
  MQ_DEBUG_PRINT("[display] Drawing to the panel...");
  // Clear the display buffer before drawing the new bitmap
  _display->clearBuffer();
  uint32_t t_decode_start = millis();
  ImageReturnCode rc =
      _reader.drawBMP(_pending_bmp, _pending_bmp_len, *_display, 0, 0);
  uint32_t t_decode = millis() - t_decode_start;
  if (rc != IMAGE_SUCCESS) {
    MQ_DEBUG_PRINTF("[display] ERROR: drawBMP rc: %d\n", (int)rc);
  } else {
    uint32_t t_refresh_start = millis();
    _display->display();
    (void)t_refresh_start;
    MQ_DEBUG_PRINTF("[display] decode ms: %u refresh ms: %u\n",
                    (unsigned)t_decode, (unsigned)(millis() - t_refresh_start));
    storeBmpCRC(_pending_crc);
  }

  // Free the bitmap buffer and clear flags
  free(_pending_bmp);
  _pending_bmp = nullptr;
  _pending_bmp_len = 0;
  MQ_DEBUG_PRINTLN("[display] Draw complete!");
}

/*!
    @brief  Keeps WiFi and the MQTT session up and pumps incoming packets.
*/
void Adafruit_Marquee::handleConnection() {
  if (!_mqtt)
    return;
  // Is the WiFi network still connected?
  if (!isNetConnected()) {
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
  // Sleep, if the sleep feed asked for it. On deep sleep this does not return.
  handleSleep();
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
    @brief  Callback for sleep feed messages, parses and stores the sleep feed's JSON data into the class instance.
    @param  data  The message payload.
    @param  len   Payload length, in bytes.
*/
void Adafruit_Marquee::cbSleepMsg(char *data, uint16_t len) {
  if (!_instance || !data || len == 0)
    return;
  MQ_DEBUG_PRINT("Sleep feed received <- ");
  MQ_DEBUG_PRINTLN(data);

  // Parse and store the sleep feed JSON in the class' instance
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, len);
  if (error) {
    MQ_DEBUG_PRINT("Sleep feed JSON parse failed");
    MQ_DEBUG_PRINTLN(error.c_str());
    return;
  }

  if (doc["alarm_type"] == "timer") {
    _instance->_sleep_alarm = SLEEP_ALARM_TIMER;
  } else {
    _instance->_sleep_alarm = SLEEP_ALARM_NONE;
  }

  if (doc["sleep_mode"] == "deep") {
    _instance->_sleep_mode = SLEEP_MODE_DEEP;
  } else if (doc["sleep_mode"] == "light") {
    _instance->_sleep_mode = SLEEP_MODE_LIGHT;
  } else {
    _instance->_sleep_mode = SLEEP_MODE_NONE;
  }
  _instance->_sleep_time = doc["sleep_time"] | 60;
  _instance->_is_sleep_pending = true;
}

/*!
    @brief  Deserializes the Marquee config file and parses out the display
            and display interface configuration.
    @param  cfg  The opened Marquee config file. Closed before returning.
    @return SUCCESS if the display configuration was parsed, otherwise the
            mq_begin_status_t describing the failure.
*/
mq_begin_status_t Adafruit_Marquee::parseDisplayCfg(File32 &cfg) {
  DeserializationError error = deserializeJson(_cfg_doc, cfg);
  cfg.close();
  if (error) {
    _begin_status = ERR_JSON_DESERIALIZATION;
    return _begin_status;
  }

  JsonObject display = _cfg_doc["display"];
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

  _begin_status = SUCCESS;
  return _begin_status;
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

  // Calculate the CRC of the payload 
  uint32_t crc = calcCRC32((const uint8_t *)b64, b64_len);
  // Compare the payload's CRC to the last drawn bitmap's CRC
  uint32_t prv_crc;
  if (loadPrvBmpCRC(prv_crc) && prv_crc == crc) {
    MQ_DEBUG_PRINTF("[bmp] unchanged (crc 0x%08lX), skipping redraw\n",
                    (unsigned long)crc);
    return true;
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
  _pending_bmp_len = decoded_len;
  _pending_crc = crc;

  MQ_DEBUG_PRINTF("[bmp] decoded %u bytes, queued for draw\n", (unsigned)decoded_len);
  return true;
}

// Sleep API
#ifdef ARDUINO_ARCH_ESP32

/*!
    @brief  Returns why the ESP32 woke from its previous sleep.
    @return The wakeup reason
*/
const char *Adafruit_Marquee::wakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_EXT0:
    return "ext0"; // external signal (RTC_IO)
  case ESP_SLEEP_WAKEUP_EXT1:
    return "ext1"; // external signal (RTC_CNTL)
  case ESP_SLEEP_WAKEUP_TIMER:
    return "timer";
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    return "touchpad";
  case ESP_SLEEP_WAKEUP_ULP:
    return "ulp";
  case ESP_SLEEP_WAKEUP_GPIO:
    return "gpio";
  case ESP_SLEEP_WAKEUP_UART:
    return "uart";
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    return "cold_boot"; // not a wake from sleep
  default:
    MQ_DEBUG_PRINTF("[sleep] woke for an unhandled reason: %d\n",
                    (int)wakeup_reason);
    return "unknown";
  }
}

/*!
    @brief  Reports whether this boot is a wake from sleep rather than a
            power-on or reset boot.
    @return True if the chip woke from a sleep wakeup source, False otherwise.
*/
bool Adafruit_Marquee::didWakeFromSleep() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  return wakeup_reason != ESP_SLEEP_WAKEUP_UNDEFINED;
}

/*!
    @brief  Enables the ESP32 timer wakeup source.
    @param  wakeup_time_sec  Expected number of seconds to wait before waking up.
    @return True if the timer wakeup was successfully enabled, False otherwise.
*/
bool Adafruit_Marquee::enableTimerWakeup(uint64_t wakeup_time_sec) {
  esp_err_t err = esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000ULL);
  if (err != ESP_OK) {
    MQ_DEBUG_PRINTF("[sleep] ERROR: could not enable timer wakeup: %d\n",
                    (int)err);
    return false;
  }
  return true;
}

/*!
    @brief  Tears down MQTT session and USB before entering sleep.
*/
void Adafruit_Marquee::disconnectBeforeSleep() {
  flash.syncDevice();
  _mqtt->disconnect();
  MQ_DEBUG_FLUSH();
  TinyUSBDevice.detach();
  delay(10);
}

#else // !ARDUINO_ARCH_ESP32

/*!
    @brief  Returns why the chip came out of its last sleep.
    @return "unknown", always.
*/
const char *Adafruit_Marquee::wakeupReason() { return "unknown"; }

/*!
    @brief  Whether this boot is a wake from sleep rather than a cold-boot.
    @return False, always.
*/
bool Adafruit_Marquee::didWakeFromSleep() { return false; }

#endif // ARDUINO_ARCH_ESP32

/*!
    @brief  Acts on a sleep request queued by the sleep feed callback. On deep
            sleep this does not return; the chip resets on wake.
*/
void Adafruit_Marquee::handleSleep() {
  if (!_is_sleep_pending) {
    return;
  }

#ifdef ARDUINO_ARCH_ESP32
  if (_sleep_mode != SLEEP_MODE_DEEP && _sleep_mode != SLEEP_MODE_LIGHT) {
    MQ_DEBUG_PRINTLN("[sleep] ERROR: unsupported sleep mode, staying awake!");
    _is_sleep_pending = false;
    return;
  }

  // NOTE/TODO: _sleep_alarm is parsed but not used yet. We only support wake from timer.
  if (!enableTimerWakeup(_sleep_time)) {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    _is_sleep_pending = false;
    return;
  }

  // Publish the sleeping status to the status feed before entering sleep
  JsonDocument doc;
  doc["state"] = "sleeping";
  doc["sleep_time"] = _sleep_time;
  doc["alarm_type"] = (_sleep_alarm == SLEEP_ALARM_TIMER) ? "timer" : "none";

  char payload[128];
  size_t len = serializeJson(doc, payload, sizeof(payload));
  if (len == 0 || len >= sizeof(payload)) {
    // Announcing the sleep is informational, so a payload that will not
    // serialize is no reason to stay awake and burn battery.
    MQ_DEBUG_PRINTLN(
        "[status] ERROR: could not serialize the sleeping payload");
  } else {
    publishStatus(payload);
  }

  MQ_DEBUG_PRINTLN("[sleep] Disconnecting MQTT and USB before entering sleep");
  disconnectBeforeSleep();

  if (_sleep_mode == SLEEP_MODE_DEEP) {
    MQ_DEBUG_PRINTF("[sleep] entering deep sleep for %llu s\n", (unsigned long long)_sleep_time);
    esp_deep_sleep_start();
    // Not reached: the chip resets on wake.
  } else {
    // The guard above leaves light sleep as the only remaining mode.
    MQ_DEBUG_PRINTF("[sleep] entering light sleep for %llu s\n", (unsigned long long)_sleep_time);
    esp_err_t rc = esp_light_sleep_start();

    // Re-enumerate USB
    TinyUSBDevice.attach();
    delay(500);

    if (rc != ESP_OK) {
      MQ_DEBUG_PRINTF("[sleep] ERROR: could not enter light sleep: %d\n",
                      (int)rc);
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
      _is_sleep_pending = false;
      return;
    }

    MQ_DEBUG_PRINTF("[sleep] wake reason: %s\n", wakeupReason());

    // Reconnect network on the next handleConnection() call
    _last_mqtt_attempt = millis() - _mqtt_retry_ms;
    _last_ping = millis();
  }
#else
  MQ_DEBUG_PRINTLN("[sleep] ERROR: sleep is not implemented for this platform");
#endif // ARDUINO_ARCH_ESP32

  _is_sleep_pending = false;
}
