/*!
 * @file flash_config.h
 *
 * Selects the on-board flash transport for the current board.
 *
 * Derived from the Adafruit TinyUSB example:
 * examples/MassStorage/msc_external_flash/flash_config.h
 */
#ifndef FLASH_CONFIG_H_
#define FLASH_CONFIG_H_

#ifndef ADAFRUIT_MARQUEE_INTERNAL
#error "flash_config.h is internal to Adafruit_Marquee: including it defines a second flashTransport. Include Adafruit_Marquee.h and use Adafruit_Marquee::flash instead."
#endif

#include "Adafruit_SPIFlash.h"
#include "SPI.h"

// Un-comment to use Marquee with custom SPI and SS e.g with FRAM breakout
// #define CUSTOM_CS   A5
// #define CUSTOM_SPI  SPI
#if defined(CUSTOM_CS) && defined(CUSTOM_SPI)
Adafruit_FlashTransport_SPI flashTransport(CUSTOM_CS, CUSTOM_SPI);
#elif defined(ARDUINO_ARCH_ESP32)
// ESP32 use same flash device that store code for file system.
// SPIFlash will parse partition.cvs to detect FATFS partition to use
Adafruit_FlashTransport_ESP32 flashTransport;
#elif defined(ARDUINO_ARCH_RP2040)
// RP2040 use same flash device that store code for file system. Therefore we
// only need to specify start address and size (no need SPI or SS)
// By default (start=0, size=0), values that match file system setting in
// 'Tools->Flash Size' menu selection will be used.
Adafruit_FlashTransport_RP2040 flashTransport;
// To be compatible with CircuitPython partition scheme (start_address = 1 MB,
// size = total flash - 1 MB) use const value (CPY_START_ADDR, CPY_SIZE) or
// subclass Adafruit_FlashTransport_RP2040_CPY. Un-comment either of the
// following line:
//  Adafruit_FlashTransport_RP2040
//    flashTransport(Adafruit_FlashTransport_RP2040::CPY_START_ADDR,
//                   Adafruit_FlashTransport_RP2040::CPY_SIZE);
//  Adafruit_FlashTransport_RP2040_CPY flashTransport;
#else
// On-board external flash (QSPI or SPI) macros should already
// defined in your board variant if supported
// - EXTERNAL_FLASH_USE_QSPI
// - EXTERNAL_FLASH_USE_CS/EXTERNAL_FLASH_USE_SPI
#if defined(EXTERNAL_FLASH_USE_QSPI)
Adafruit_FlashTransport_QSPI flashTransport;
#elif defined(EXTERNAL_FLASH_USE_SPI)
Adafruit_FlashTransport_SPI flashTransport(EXTERNAL_FLASH_USE_CS,
                                           EXTERNAL_FLASH_USE_SPI);
#else
#error No (Q)SPI flash are defined for your board !
#endif
#endif

#endif // FLASH_CONFIG_H_
