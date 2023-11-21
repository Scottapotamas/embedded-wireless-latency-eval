# Test firmware for NRF24L01+ using LL SPI on STM32F429

Minimal test firmware to test NRF24 communication latency.

IO Setup:

- PA0 is driven with a 3.3V trigger pulse from my sig-gen.
- PB0 is a 3.3V output signal (also connected to the nucleo's onboard green LED)
- The NRF24 modules requires some IO and uses SPI1:
  - PB4 for Chip enable
  - PA4 for Chip select
  - PB3 for IRQ
  - PA5 for SPI CLK
  - PA6 for SPI MISO
  - PA7 for SPI MOSI

## Deps

I use CMake with CLion for development/builds, so this project is slightly opinionated.

Requires [`stm32-cmake`](https://github.com/ObKo/stm32-cmake) in this folder to get the build system setup etc.

I set my CMake options to use my system's arm gcc toolchain `-DSTM32_TOOLCHAIN_PATH=/usr/bin/ -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-g++ -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-gcc`.

With CLion's build settings, I set the build generator to "Let CMake Decide".

# Acknowledgements

I started trying to build from [Ilia Motornyi's `nrf24l01-lib`](https://github.com/elmot/nrf24l01-lib) (Unlicense licensed) but had a suite of issues.
[Eunhye Seok's `stm32_hal_nrf24l01p`](https://github.com/mokhwasomssi/stm32_hal_nrf24l01p) was modified to use STM LL.