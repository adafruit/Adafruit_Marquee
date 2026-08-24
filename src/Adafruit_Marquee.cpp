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

#include "Adafruit_TinyUSB.h"
#include "SdFat_Adafruit_Fork.h"

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
bool Adafruit_Marquee::fs_formatted = false;

// Set to true when the PC writes to flash
volatile bool Adafruit_Marquee::fs_changed = true;

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
 * @brief Creates a new instance of the Adafruit Marquee client.
 */
Adafruit_Marquee::Adafruit_Marquee() {
}

/*!
 * @brief Destructor.
 */
Adafruit_Marquee::~Adafruit_Marquee() {}

/*!
 * @brief Initializes the Marquee client.
 * @returns SUCCESS if initialization succeeded, otherwise the
 *          mq_begin_status_t describing the failure.
 */
mq_begin_status_t Adafruit_Marquee::begin() {
  // Configure the USB Mass Storage device
  flash.begin();
  usb_msc.setID("Adafruit", "External Flash", "1.0");
  usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
  usb_msc.setCapacity(flash.size()/512, 512);
  usb_msc.setUnitReady(true);
  usb_msc.begin();
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  // Attempt to init. file system on the flash
  fs_formatted = fatfs.begin(&flash);
  if (!fs_formatted) {
    _begin_status = ERR_FS_UNFORMATTED;
    return _begin_status;
  }


  File32 cfg = fatfs.open("/marquee-cfg.json", O_RDONLY);
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

  // Attempt to parse the Marquee config file's contents
  int cfg_version = _cfg_doc["cfg_version"]; // 2
  const char* name = _cfg_doc["name"]; // "Office"
  JsonObject display = _cfg_doc["display"];
  const char* display_driver = display["driver"]; // "SSD1680"
  const char* display_panel = display["panel"]; // "adafruit-magtag"
  int display_width = display["width"]; // 128
  int display_height = display["height"]; // 296
  int display_rotation = display["rotation"]; // 3
  const char* display_mode = display["mode"]; // "mono"
  const char* interface_type = _cfg_doc["interface"]["type"]; // "builtin"
  const char* network_wifi_ssid = _cfg_doc["network"]["wifi_ssid"]; // "YOUR_SSID"
  const char* network_wifi_password = _cfg_doc["network"]["wifi_password"]; // "YOUR_WIFI_PASSWORD"
  const char* adafruit_io_username = _cfg_doc["adafruit_io"]["username"]; // "YOUR_AIO_USERNAME"
  const char* adafruit_io_key = _cfg_doc["adafruit_io"]["key"]; // "YOUR_AIO_KEY"

  _begin_status = SUCCESS;
  return _begin_status;
}


