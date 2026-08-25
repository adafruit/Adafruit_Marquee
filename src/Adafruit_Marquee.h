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
  ~Adafruit_Marquee();
  mq_begin_status_t begin();
  bool connect(unsigned long timeout = 30000);
  void run();

  static bool fs_formatted;
  static volatile bool fs_changed;
private:
  mq_begin_status_t _begin_status;
  JsonDocument _cfg_doc;
  bool initFilesystem();
  void initUSBMSC();
  bool createEPD(const char *panel);
  bool parseThinkInkMode(const char* mode);
  Adafruit_EPD *_display; ///< Pointer to the EPD display object
  thinkinkmode_t _thinkInkMode; ///< ThinkInk mode for the display
  int16_t _pin_cs; ///< Chip select pin for EPD
  int16_t _pin_dc; ///< Data/Command pin for EPD
  int16_t _pin_rst; ///< Reset pin for EPD
  int16_t _pin_busy; ///< Busy pin for EPD
  int16_t _pin_sram_cs; ///< SRAM chip select pin for EPD
  uint8_t _rotation; ///< Display rotation (0-3)
  // Networking
  const char* _ssid; ///< WiFi SSID
  const char* _pass; ///< WiFi password
  // Adafruit IO
  const char* _aio_username; ///< Adafruit IO username
  const char* _aio_key; ///< Adafruit IO key
  const char *_device_name; ///< Device name for Adafruit IO
  AdafruitIO_WiFi *_io; ///< Pointer to the Adafruit IO WiFi client
  AdafruitIO_Feed *_bmp; ///< Pointer to the Adafruit IO feed to subscribe to for bitmap data
  AdafruitIO_Feed *_sleep; ///< Pointer to the Adafruit IO feed to subscribe to for sleep info
};

#endif // ADAFRUIT_MARQUEE_H
