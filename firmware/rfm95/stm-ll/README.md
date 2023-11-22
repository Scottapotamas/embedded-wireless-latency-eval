# Test firmware for RFM95 using LL SPI on STM32F429

Minimal test firmware to test direct device-device communication latency.

IO Setup:

- PA0 is driven with a 3.3V trigger pulse from my sig-gen.
- PB0 is a 3.3V output signal (also connected to the nucleo's onboard green LED)
- The RFM95 modules requires some IO and uses SPI1:
  - PB4 for Chip enable
  - PA4 for Chip select
  - PB3 for IRQ
  - PA5 for SPI CLK
  - PA6 for SPI MISO
  - PA7 for SPI MOSI
  - TODO: any additional GPIO pins

## Deps

I use CMake with CLion for development/builds, so this project is slightly opinionated.

Requires [`stm32-cmake`](https://github.com/ObKo/stm32-cmake) in this folder to get the build system setup etc.

I set my CMake options to use my system's arm gcc toolchain `-DSTM32_TOOLCHAIN_PATH=/usr/bin/ -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-g++ -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-gcc`.

With CLion's build settings, I set the build generator to "Let CMake Decide".

# Acknowledgements

