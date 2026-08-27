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

// Set in initMQTT(). Adafruit_MQTT_Subscribe::setCallback takes a plain
// function pointer, so the feed callbacks are static members that trampoline
// through this. A sketch only ever constructs one Adafruit_Marquee.
Adafruit_Marquee *Adafruit_Marquee::_instance = nullptr;

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
static int32_t msc_read_cb (uint32_t lba, void* buffer, uint32_t bufsize) {
  // Note: SPIFLash Block API: readBlocks/writeBlocks/syncBlocks
  // already include 4K sector caching internally. We don't need to cache it, yahhhh!!
  return flash.readBlocks(lba, (uint8_t*) buffer, bufsize/512) ? bufsize : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and 
// return number of written bytes (must be multiple of block size)
static int32_t msc_write_cb (uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
  // Note: SPIFLash Block API: readBlocks/writeBlocks/syncBlocks
  // already include 4K sector caching internally. We don't need to cache it, yahhhh!!
  return flash.writeBlocks(lba, buffer, bufsize/512) ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
static void msc_flush_cb (void) {
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
  static const Adafruit_EPDFactory adafruitEPDFactory =
      {
          {"290-grayscale4-FPC7519",
           [](int16_t dc, int16_t rst, int16_t cs, int16_t sram_cs,
              int16_t busy, SPIClass *spi,
              thinkinkmode_t mode) -> Adafruit_EPD * {
             auto *d = new ThinkInk_290_Grayscale4_FPC7519(
                 dc, rst, cs, sram_cs, busy, spi);
             d->begin(mode);
             return d;
           }},
          {"213-tricolor-MFGNR",
           [](int16_t dc, int16_t rst, int16_t cs, int16_t sram_cs,
              int16_t busy, SPIClass *spi,
              thinkinkmode_t mode) -> Adafruit_EPD * {
             auto *d = new ThinkInk_213_Tricolor_MFGNR(
                 dc, rst, cs, sram_cs, busy, spi);
             d->begin(mode);
             return d;
           }},
          {"290-grayscale4-EAAMFGN",
           [](int16_t dc, int16_t rst, int16_t cs, int16_t sram_cs,
              int16_t busy, SPIClass *spi,
              thinkinkmode_t mode) -> Adafruit_EPD * {
             auto *d = new ThinkInk_290_Grayscale4_EAAMFGN(
                 dc, rst, cs, sram_cs, busy, spi);
             d->begin(mode);
             return d;
           }},
          {"magtag-2025",
           [](int16_t dc, int16_t rst, int16_t cs, int16_t sram_cs,
              int16_t busy, SPIClass *spi,
              thinkinkmode_t mode) -> Adafruit_EPD * {
             auto *d = new ThinkInk_290_Grayscale4_EAAMFGN(
                 dc, rst, cs, sram_cs, busy, spi);
             d->begin(mode);
             return d;
           }},
      };
  return adafruitEPDFactory;
}

/*!
    @brief  Callback for bitmap feed messages. Forwards the payload to the
            instance registered by initMQTT().
    @param  data  The message payload: a base64-encoded BMP. Adafruit_MQTT
                  nul-terminates this, so it can be treated as a C string.
    @param  len   Payload length, in bytes.
*/
void Adafruit_Marquee::cbBitmapMsg(char *data, uint16_t len) {
  MQ_DEBUG_PRINTF("[trace] cbBitmapMsg: len=%u inst=%p data=%p\n",
                  (unsigned)len, _instance, data);
  Serial.flush();
  if (!_instance || !data || len == 0)
    return;
  _instance->queueBitmapBase64(data, len);
  MQ_TRACE("cbBitmapMsg: returned");
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
  _pending_draw = false;
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
  _pending_draw = false;
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

  MQ_TRACE("begin: enter");

  // Detach USB *before* touching the flash, mirroring Wippersnapper_FS
  TinyUSBDevice.detach();
  delay(500);

  MQ_TRACE("begin: usb detached");

  // Attempt to init. the flash chip and the file system on it
  if (!initFilesystem()) {
    // Still bring MSC up so the flash is reachable from the host to be fixed
    initUSBMSC();
    _begin_status = ERR_FS_UNFORMATTED;
    return _begin_status;
  }

  MQ_TRACE("begin: fs mounted");

  // Reattach FS
  initUSBMSC();

  MQ_TRACE("begin: msc up");

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

  MQ_TRACE("begin: cfg parsed");

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


  MQ_TRACE("begin: pre-createEPD");

  if (!createEPD(display_panel)) {
    _begin_status = ERR_EPD_PANEL_UNSUPPORTED;
    return _begin_status;
  }

  MQ_TRACE("begin: epd created");

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

  MQ_TRACE("begin: display ready");

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

  MQ_TRACE("begin: done");
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
bool Adafruit_Marquee::initMQTT() {
  // Publish ourselves to the static feed callbacks before subscribing, so a
  // message arriving on the first processPackets() has somewhere to land.
  _instance = this;

  MQ_TRACE("initMQTT: enter");

  // Build the client here rather than in a constructor: Adafruit_MQTT keeps the
  // credential pointers it is handed, and begin() only just filled them in.
  setupMQTTClient();
  if (!_mqtt) {
    MQ_DEBUG_PRINTLN("[mqtt] ERROR: could not create the MQTT client");
    return false;
  }

  // Zero means the allocation failed. Nothing downstream is safe in that state
  // - connect(), publish() and ping() all write through the buffer, and ping()
  // takes no size argument to guard itself with - so stop here.
  if (_mqtt->bufferSize() == 0) {
    MQ_DEBUG_PRINTF("[mqtt] ERROR: could not allocate %u byte packet buffer\n",
                    (unsigned)MQ_MQTT_BUFFER_LEN);
    return false;
  }
  MQ_TRACE("initMQTT: client built");

  // Initialize Adafruit IO feed names
  snprintf(_feed_name_bmp, sizeof(_feed_name_bmp), "%s.bitmap", _device_name);
  snprintf(_topic_bmp, sizeof(_topic_bmp), "%s/f/%s/csv", _aio_username,
           _feed_name_bmp);
  snprintf(_feed_name_sleep, sizeof(_feed_name_sleep), "%s.sleep",
           _device_name);
  snprintf(_topic_sleep, sizeof(_topic_sleep), "%s/f/%s/csv", _aio_username,
           _feed_name_sleep);

  // Only the bitmap subscription needs the large payload buffer; the sleep
  // subscription takes the library default so it costs a few hundred bytes.
  _sub_bmp =
      new Adafruit_MQTT_Subscribe(_mqtt, _topic_bmp, 0, MQ_BITMAP_SUB_LEN);
  _sub_sleep = new Adafruit_MQTT_Subscribe(_mqtt, _topic_sleep);
  if (!_sub_bmp || !_sub_sleep)
    return false;
  if (_sub_bmp->lastread == nullptr) {
    MQ_DEBUG_PRINTF("[bmp] ERROR: could not allocate %d byte payload buffer\n",
                    MQ_BITMAP_SUB_LEN);
    return false;
  }

  // Both sizes, out loud, at boot: the whole bitmap path depends on them and a
  // failed allocation is otherwise silent until a payload truncates.
  MQ_DEBUG_PRINTF("[bmp] sub payload buffer: %u bytes, packet buffer: %u\n",
                  (unsigned)_sub_bmp->lastread_max,
                  (unsigned)_mqtt->bufferSize());

  MQ_TRACE("initMQTT: subs allocated");

  _sub_bmp->setCallback(cbBitmapMsg);
  _sub_sleep->setCallback(cbSleepMsg);

  // subscribe() only registers into the subscriptions[] array; the SUBSCRIBE
  // packets are sent by Adafruit_MQTT::connect(), which runs after this. That
  // also means they are re-sent on every reconnect, so nothing has to be
  // re-registered when the link drops.
  if (!_mqtt->subscribe(_sub_bmp) || !_mqtt->subscribe(_sub_sleep)) {
    MQ_DEBUG_PRINTLN("Failed to register MQTT subscriptions");
    return false;
  }

  MQ_TRACE("initMQTT: subs registered");

  MQ_DEBUG_PRINT("Subscribing to feed: ");
  MQ_DEBUG_PRINTLN(_topic_bmp);
  MQ_DEBUG_PRINT("Subscribing to feed: ");
  MQ_DEBUG_PRINTLN(_topic_sleep);
  return true;
}

/*!
    @brief  Associates with the configured WiFi network, blocking until it is
            up or the timeout expires.
    @param  timeout  Maximum time to wait for an association, in milliseconds.
    @return True once the platform reports an association, else False.
*/
bool Adafruit_Marquee::connectWiFi(unsigned long timeout) {
  if (!_ssid || strlen(_ssid) == 0) {
    MQ_DEBUG_PRINTLN("[wifi] ERROR: no SSID configured");
    return false;
  }

  MQ_DEBUG_PRINTF("[wifi] connecting to '%s'\n", _ssid);
  Serial.flush();
  _connect();
  _last_wifi_attempt = millis();
  MQ_TRACE("connectWiFi: post-begin");

  // Settle before the first check, not after it. _connect() disconnects on
  // the way in, and networkConnected() can still report the old association for
  // a short window afterwards - polling it immediately would return success
  // for a link that is actually down.
  unsigned long start = millis();
  for (;;) {
    delay(250);
    if (networkConnected())
      break;
    if (millis() - start >= timeout) {
      MQ_DEBUG_PRINTF("[wifi] TIMEOUT after %lums, status=%d\n",
                      millis() - start, networkStatus());
      return false;
    }
  }

  MQ_TRACE("connectWiFi: connected");
  return true;
}

/*!
    @brief  Opens the MQTT session. Credentials went in at construction, so
            this is the no-argument connect().
    @return True on CONNACK success, else False.
*/
bool Adafruit_Marquee::connectMQTT() {
  MQ_TRACE("connectMQTT: enter");
  _last_mqtt_attempt = millis();
  int8_t rc = _mqtt->connect();
  if (rc == 0) {
    // A previous rejection may have stretched the retry interval; a successful
    // CONNACK means the credentials are fine, so put it back.
    _mqtt_retry_ms = MQ_MQTT_RETRY_MS;
    MQ_TRACE("connectMQTT: connected");
    return true;
  }

  MQ_DEBUG_PRINT("[mqtt] connect failed: ");
  MQ_DEBUG_PRINTLN(_mqtt->connectErrorString(rc));
  // 1 wrong protocol, 2 client id rejected, 4 bad user/pass, 5 unauthorized.
  // None of these start working on their own, so back off hard rather than
  // hammering the broker every MQ_MQTT_RETRY_MS with the same rejected CONNECT.
  if (rc == 1 || rc == 2 || rc == 4 || rc == 5) {
    _mqtt_retry_ms = MQ_MQTT_FATAL_RETRY_MS;
  } else {
    _mqtt_retry_ms = MQ_MQTT_RETRY_MS;
  }
  return false;
}

/*!
    @brief  Asks the broker to re-send the bitmap feed's last value.

    Publishing to the feed's /get topic is Adafruit IO's "send me the current
    value" convention. Note Adafruit_MQTT_Publish has no (const char *, length)
    overload - passing a length here would bind to publish(const char *, bool
    retain) and publish a *retained* empty message to the feed instead.

    The topic and the publisher are both built here rather than kept as
    members. Adafruit_MQTT_Publish only holds the client, a topic pointer and
    the QoS, and publish() uses the topic immediately - so there is nothing to
    keep alive between calls, and the /get topic does not need a second copy of
    what _feed_name_bmp already carries. This runs once per (re)connect.
*/
void Adafruit_Marquee::requestBitmap() {
  if (!_mqtt || !_aio_username)
    return;

  char topic[sizeof(_topic_bmp)];
  snprintf(topic, sizeof(topic), "%s/f/%s/csv/get", _aio_username,
           _feed_name_bmp);

  Adafruit_MQTT_Publish pub_get(_mqtt, topic);
  MQ_TRACE("requestBitmap: pre publish");
  pub_get.publish("\0");
  MQ_TRACE("requestBitmap: post publish");
}

/*!
 * @brief Connects to WiFi and the Adafruit IO MQTT broker.
 * @param timeout The maximum time to wait for a connection, in milliseconds.
 * @returns True if connection succeeded, otherwise false.
 */
bool Adafruit_Marquee::connect(unsigned long timeout) {
  MQ_TRACE("connect: enter");
  if (!initMQTT()) {
    MQ_DEBUG_PRINTLN("Failed to initialize the MQTT client and feeds");
    return false;
  }
  MQ_TRACE("connect: initMQTT ok");

  if (!connectWiFi(timeout)) {
    return false;
  }

  if (!connectMQTT()) {
    return false;
  }

  requestBitmap();
  return true;
}



/*!
    @brief  Draws the bitmap queued by the bitmap feed callback, if any.

    Runs here rather than in the callback because the callback is dispatched
    from inside Adafruit_MQTT::processPackets(), mid-loop over the socket, and a
    full EPD refresh blocks for seconds. Deferring keeps the packet pump
    responsive, gives the buffer a single owner with one free() site, and
    coalesces frames: if two arrive before either is drawn, the stale one is
    dropped instead of causing a visible double refresh. (Keepalive is not the
    concern - MQTT_CONN_KEEPALIVE is 300s against a few seconds of refresh.)
*/
void Adafruit_Marquee::servicePendingDraw() {
  if (!_pending_draw || !_pending_bmp)
    return;

  // Take ownership locally and clear the slot up front, so a bitmap draw()
  // rejects is not retried on every loop() forever.
  uint8_t *bmp = _pending_bmp;
  size_t len = _pending_len;
  _pending_bmp = nullptr;
  _pending_len = 0;
  _pending_draw = false;

  MQ_DEBUG_PRINTF("[trace] servicePendingDraw: bmp=%p len=%u\n", bmp,
                  (unsigned)len);
  Serial.flush();
  if (!draw(bmp, len)) {
    MQ_DEBUG_PRINTLN("[bmp] ERROR: draw() rejected the bitmap");
  }
  MQ_TRACE("servicePendingDraw: drawn");
  free(bmp);
  MQ_TRACE("servicePendingDraw: freed");
}

/*!
    @brief  Keeps WiFi and the MQTT session up and pumps incoming packets.

    Non-blocking by design: a retry that is not due yet returns immediately
    rather than waiting, so a down network never starves the EPD draw path or
    the USB MSC endpoint. The cost is that recovery is only as prompt as the
    caller's loop.
*/
void Adafruit_Marquee::maintainConnection() {
  if (!networkConnected()) {
    if (millis() - _last_wifi_attempt >= MQ_WIFI_RETRY_MS) {
      MQ_DEBUG_PRINTLN("[wifi] disconnected, re-associating");
      _last_wifi_attempt = millis();
      _connect();
    }
    // Nothing else can make progress without a link. Come back next loop().
    return;
  }

  if (!_mqtt->connected()) {
    if (millis() - _last_mqtt_attempt < _mqtt_retry_ms) {
      return;
    }
    MQ_DEBUG_PRINTLN("[mqtt] disconnected, reconnecting");
    if (!connectMQTT()) {
      return;
    }
    // connect() re-sent every SUBSCRIBE, but the broker will not re-push the
    // feed's last value on its own - so ask for it. Without this the panel
    // keeps whatever it drew before the drop until the feed next changes.
    requestBitmap();
    return;
  }

  _mqtt->processPackets(MQ_PACKET_READ_MS);

  if (millis() - _last_ping >= MQ_PING_INTERVAL_MS) {
    _last_ping = millis();
    _mqtt->ping();
  }
}

void Adafruit_Marquee::run() {
  if (!_mqtt)
    return;

  // Heartbeat, so a hang or a silent reboot mid-loop is distinguishable from
  // "connected and idle waiting for a feed value".
  static unsigned long prv_beat = 0;
  if (millis() - prv_beat > 5000) {
    prv_beat = millis();
    MQ_DEBUG_PRINTF("[trace] run: wifi=%d mqtt=%d\n", networkStatus(),
                    (int)_mqtt->connected());
    Serial.flush();
  }

  servicePendingDraw();
  maintainConnection();
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
    // Distinct from a construction failure: the identifier in the config file
    // has no entry in the factory table. Log the known keys so a typo (or a
    // firmware built before the panel was added) is obvious from the trace.
    for (Adafruit_EPDFactory::const_iterator k = adafruitEPDFactory.begin();
         k != adafruitEPDFactory.end(); ++k) {
    }
    return false;
  }

  // Creates the panel instance using the factory function. Note this also
  // calls the panel's begin(), which resets the hardware and busy-waits - a
  // wrong busy/cs pin shows up as a hang between here and the next trace.
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
bool Adafruit_Marquee::parseThinkInkMode(const char* mode) {
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
    @brief  Validates and base64-decodes a BMP payload, queueing it to be drawn
            on the next run().

    Transport-agnostic: it takes any base64 string and does not care how that
    string arrived. The length is passed in rather than derived with strlen()
    so the payload is not walked an extra time; the caller already knows it
    (for an MQTT subscription it is Adafruit_MQTT_Subscribe::datalen).

    @param  b64      Base64-encoded BMP file. Must still be nul-terminated:
                     decode_base64_length() reads input[0] before testing its
                     bound, so it looks one byte past b64_len and relies on
                     finding a non-base64 byte there.
    @param  b64_len  Length of b64 in characters, excluding the terminator.
    @return True if a bitmap was decoded and queued, False otherwise.
*/
bool Adafruit_Marquee::queueBitmapBase64(const char *b64, size_t b64_len) {
  if (!b64) {
    MQ_DEBUG_PRINTLN("[bmp] ERROR: null payload");
    return false;
  }

  MQ_TRACE("queueBitmap: enter");
  MQ_DEBUG_PRINTF("[trace] queueBitmap: len=%u\n", (unsigned)b64_len);
  Serial.flush();
  if (b64_len < MQ_MIN_BMP_B64_LEN) {
    MQ_DEBUG_PRINTF("[bmp] ERROR: payload too short: %u chars\n",
                    (unsigned)b64_len);
    return false;
  }

  // decode_base64_length() walks until the first non-base64 character, so '='
  // padding and trailing whitespace both terminate it correctly. It reads
  // input[0] before testing the bound, so it is only safe on a nul-terminated
  // string - which is exactly the contract of this method.
  size_t decoded_len =
      decode_base64_length((const unsigned char *)b64, (unsigned int)b64_len);
  if (decoded_len < MIN_SZ_BMP_HEADER || decoded_len > MQ_MAX_BMP_BYTES) {
    MQ_DEBUG_PRINTF("[bmp] ERROR: implausible decoded length: %u\n",
                    (unsigned)decoded_len);
    return false;
  }

  // Prefer SPIRAM: the decoded file dwarfs anything worth taking out of the
  // internal heap that the WiFi/TLS stack shares. Fall back for no-PSRAM parts.
  MQ_DEBUG_PRINTF("[trace] queueBitmap: decoded_len=%u, allocating\n",
                  (unsigned)decoded_len);
  Serial.flush();
  uint8_t *buf = (uint8_t *)ps_malloc(decoded_len);
  if (!buf) {
    buf = (uint8_t *)malloc(decoded_len);
  }
  if (!buf) {
    MQ_DEBUG_PRINTF("[bmp] ERROR: alloc of %u bytes failed\n",
                    (unsigned)decoded_len);
    return false;
  }

  MQ_DEBUG_PRINTF("[trace] queueBitmap: buf=%p, decoding\n", buf);
  Serial.flush();
  unsigned int written =
      decode_base64((const unsigned char *)b64, (unsigned int)b64_len, buf);
  MQ_TRACE("queueBitmap: decoded");
  if (written != decoded_len) {
    MQ_DEBUG_PRINTF("[bmp] ERROR: decoded %u of %u bytes\n", written,
                    (unsigned)decoded_len);
    free(buf);
    return false;
  }

  // Hand off to run(); do NOT draw here - see servicePendingDraw(). Newest
  // frame wins: drop anything not yet drawn.
  if (_pending_bmp) {
    free(_pending_bmp);
  }
  _pending_bmp = buf;
  _pending_len = decoded_len;
  _pending_draw = true;

  MQ_DEBUG_PRINTF("[bmp] queued: %u b64 chars -> %u bytes\n",
                  (unsigned)b64_len, (unsigned)decoded_len);
  return true;
}

/*!
    @brief  Draws an assembled canvas to the display.
    @param  bmp  Pointer to the complete BMP file bytes.
    @param  len  Length of the BMP buffer, in bytes.
    @return True if accepted, False otherwise.

    Everything about BMP semantics - signature, bit depth, compression, palette,
    row order, and bounding the pixel read against len - is validated by
    Adafruit_ImageReader_EPD::coreBMP() and surfaced as the ImageReturnCode
    logged below. Do not duplicate those checks here.
*/
bool Adafruit_Marquee::draw(const uint8_t *bmp, size_t len) {
  if (!_display)
    return false;
  if (!bmp || len == 0) {
    MQ_DEBUG_PRINTLN("[display] ERROR: Empty canvas buffer!");
    return false;
  }
  MQ_TRACE("draw: enter");
  _display->clearBuffer();
  MQ_TRACE("draw: buffer cleared");

  // Draw the BMP to the display
  uint32_t t_decode_start = millis();
  ImageReturnCode rc = _reader.drawBMP(bmp, len, *_display, 0, 0);
  MQ_TRACE("draw: drawBMP returned");
  uint32_t t_decode = millis() - t_decode_start;
  (void)t_decode; // only read by the debug trace below
  if (rc != IMAGE_SUCCESS) {
    MQ_DEBUG_PRINTF("[display] ERROR: drawBMP rc: %d\n", (int)rc);
    return false;
  }

  uint32_t t_refresh_start = millis();
  MQ_TRACE("draw: pre display()");
  _display->display();
  MQ_TRACE("draw: post display()");
  (void)t_refresh_start;
  MQ_DEBUG_PRINTF("[display] decode ms: %u refresh ms: %u\n",
                  (unsigned)t_decode,
                  (unsigned)(millis() - t_refresh_start));
  return true;
}

