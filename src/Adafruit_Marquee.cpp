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

#include <functional>
#include <map>
#include <string>

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

      };
  return adafruitEPDFactory;
}

/*!
 * @brief Creates a new instance of the Adafruit Marquee client.
 */
Adafruit_Marquee::Adafruit_Marquee() {
  _display = nullptr;
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
}

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

  // Attempt to parse the marquee config file
  JsonObject display = _cfg_doc["display"];
  const char *display_panel = display["panel"];
  _rotation = display["rotation"] | 0;

  if (!parseThinkInkMode(display["mode"])) {
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
  // TODO: Maybe refactor setrotation and other config stuff out of this function?
  _display->setRotation(_rotation);

  // Parse network and Adafruit IO credentials
  _ssid = _cfg_doc["network"]["wifi_ssid"];
  _pass = _cfg_doc["network"]["wifi_password"];
  _aio_username = _cfg_doc["adafruit_io"]["username"];
  _aio_key = _cfg_doc["adafruit_io"]["key"];
  _io = new AdafruitIO_WiFi(_aio_username, _aio_key, _ssid, _pass);

  _begin_status = SUCCESS;
  return _begin_status;
}

/*!
 * @brief Connects to the Adafruit IO service.
 * @param timeout The maximum time to wait for a connection, in milliseconds.
 * @returns True if connection succeeded, otherwise false.
 */
bool Adafruit_Marquee::connect(unsigned long timeout) {
  if (!_io)
    return false;

  // Attempt to connect to Adafruit IO within the timeout period
  _io->connect();
  unsigned long start = millis();
  while (_io->status() < AIO_CONNECTED) {
    if (millis() - start >= timeout) {
      return false;
    }
    delay(500);
  }
  return true;
}


/*!
    @brief  Finds and initializes the display panel from the configured
            panel identifier.
    @param  panel  The EPD panel name (e.g., "290-grayscale4-FPC7519").
    @return True if the panel was found and initialized, False otherwise.
*/
bool Adafruit_Marquee::createEPD(const char *panel) {
  if (!panel)
    return false;

  // Look up the panel identifier in the factory table and create the instance
  const Adafruit_EPDFactory &adafruitEPDFactory = getAdafruitEPDFactory();
  Adafruit_EPDFactory::const_iterator it = adafruitEPDFactory.find(panel);
  if (it == adafruitEPDFactory.end())
    return false;

  // Creates the panel instance using the factory function
  _display = it->second(_pin_dc, _pin_rst, _pin_cs, _pin_sram_cs, _pin_busy,
                        &SPI, _thinkInkMode);
  if (_display == nullptr)
    return false;

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
