/*!
 * @file Adafruit_Marquee.h
 *
 * Adafruit Marquee client for Arduino.
 *
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing products
 * from Adafruit!
 *
 * Written by Brent Rubell for Adafruit Industries.
 *
 * MIT license, all text here must be included in any redistribution.
 */
#ifndef ADAFRUIT_MARQUEE_H
#define ADAFRUIT_MARQUEE_H

#include "Arduino.h"
#include <Adafruit_ThinkInk.h>
#include <AdafruitIO_WiFi.h>
#include <ArduinoJson.h>

typedef enum {
  SUCCESS = 0,
  ERR_FS_UNFORMATTED = -1,
  ERR_FS_NO_CFG_FILE = -2,
  ERR_JSON_DESERIALIZATION = -3,
} mq_begin_status_t;

/*!
 * @brief Client for the Adafruit IO Marquee feature.
 */
class Adafruit_Marquee {
public:
  Adafruit_Marquee();
  ~Adafruit_Marquee();
  mq_begin_status_t begin();

  static bool fs_formatted;
  static volatile bool fs_changed;
private:
  mq_begin_status_t _begin_status;
  JsonDocument _cfg_doc;
};

#endif // ADAFRUIT_MARQUEE_H
