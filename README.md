# Adafruit Marquee Library [![Build CI](https://github.com/adafruit/Adafruit_Marquee/actions/workflows/githubci.yml/badge.svg)](https://github.com/adafruit/Adafruit_Marquee/actions)[![Documentation](https://github.com/adafruit/ci-arduino/blob/master/assets/doxygen_badge.svg)](http://adafruit.github.io/Adafruit_Marquee/html/index.html)

NOTE: This library is **not** ready for public consumption yet. Stay tuned!

Arduino client library for Marquee. Adafruit Marquee allows you to visually create interfaces for e-paper displays without writing any code or storing images, fonts, or layouts on the device. 

**How Marquee Works**: The interface is created in the Marquee web app, and stored on [Adafruit IO](https://io.adafruit.com) feeds you create. The device fetches the interface from the cloud, and renders it on the e-paper display. Marquee also handles sleeping the device to save power, and waking it up to fetch new content when needed.

Adafruit invests time and resources providing this open source code, please support Adafruit and open-source hardware by purchasing products from Adafruit!

Written by Brent Rubell for Adafruit Industries.
MIT license, all text above must be included in any redistribution

### Supported Hardware
* [Adafruit MagTag - 2.9" Grayscale E-Ink WiFi Display - 2025 Edition with SSD1680](https://www.adafruit.com/product/4800)
* [Adafruit Feather ESP32-S3 (with 2MB PSRAM)](https://www.adafruit.com/product/5477) and [Adafruit 2.13" Monochrome eInk / ePaper Display FeatherWing](https://www.adafruit.com/product/4195)
* [Xteink X4 Pro Pocket eReader](https://www.xteink.com/products/xteink-x4-pro-pocket-ereader)


### Supported Architectures
| Architecture | Network adapter | Sleep | Status |
| --- | --- | --- | --- |
| ESP32 / ESP32-S2 / ESP32-S3 | `Adafruit_Marquee_ESP32` (WiFi) | Deep and light, timer wake | Full Support |

### Local Development

Marquee is open-source and welcomes contributions! If you want to build and test the library locally, you can use [PlatformIO](https://platformio.org/).


#### Adding a New Board to Marquee
Adding a new board to Marquee should be done via adding to the `platformio.ini` file, under the `[env]` section. 

This repo includes a `platformio.ini` file using the [pioarduino](https://github.com/pioarduino/platform-espressif32)
community fork of `platform-espressif32`, which tracks the Arduino-ESP32 3.x core.

`examples/` in `platformio.ini` selects which example is built - the library's own
`src/` is compiled alongside it.

#### Adding a new E-Paper Display to Marquee

Marquee is designed to support any e-paper display that is compatible with the [Adafruit EPD library](https://github.com/adafruit/Adafruit_EPD). To add a new display, you will need to add the display's driver to the `src/Adafruit_Marquee.cpp`'s `Adafruit_EPDFactory` class. The factory class is responsible for creating the correct display driver based on the `display_type` parameter passed to the `begin()` method.
