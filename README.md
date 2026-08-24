# Adafruit Marquee Library [![Build CI](https://github.com/adafruit/Adafruit_Marquee/actions/workflows/githubci.yml/badge.svg)](https://github.com/adafruit/Adafruit_Marquee/actions)[![Documentation](https://github.com/adafruit/ci-arduino/blob/master/assets/doxygen_badge.svg)](http://adafruit.github.io/Adafruit_Marquee/html/index.html)

This is a library for the Adafruit Marquee Feature of Adafruit.io.

Adafruit invests time and resources providing this open source code, please support Adafruit and open-source hardware by purchasing products from Adafruit!

Written by Brent Rubell for Adafruit Industries.
MIT license, all text above must be included in any redistribution

## Local Development (PlatformIO)

This repo includes a `platformio.ini` file using the [pioarduino](https://github.com/pioarduino/platform-espressif32)
community fork of `platform-espressif32`, which tracks the Arduino-ESP32 3.x core.

```sh
pio run                        # Build the example in src_dir
pio run -t upload -t monitor   # Flash the Adafruit MagTag and open the serial monitor
```

`src_dir` in `platformio.ini` selects which example is built - the library's own
`src/` is compiled alongside it.
