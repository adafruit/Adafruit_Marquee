/*!
 * @file Adafruit_Marquee_WiFi.h
 *
 * This file includes network adapters during compile-time.
 *
 * MIT license, all text here must be included in any redistribution.
 */

#ifndef ADAFRUIT_MARQUEE_WIFI_H
#define ADAFRUIT_MARQUEE_WIFI_H

#if defined(ARDUINO_ARCH_ESP32)

#include "networking/Adafruit_Marquee_ESP32.h"
/** ESP32's networking adapter */
typedef Adafruit_Marquee_ESP32 Adafruit_Marquee_WiFi;
#else
#error "Adafruit_Marquee has no network adapter for this architecture."
#endif

#endif // ADAFRUIT_MARQUEE_WIFI_H
