# STM32F429 LL UART Timing Tests

Minimal test firmware to compare polled, interrupt and DMA backed UART transfer implementations.

This is only meant to serve to highlight differences in handling/latency and not:

- peripheral handling efficiency
- efficiency (both power, and in code)
- performance with higher baud links

## Deps

I use CMake with CLion for development/builds, so this project is slightly opinionated.

Requires [`stm32-cmake`](https://github.com/ObKo/stm32-cmake) in this folder to get the build system setup etc.

I set my CMake options to use my system's arm gcc toolchain `-DSTM32_TOOLCHAIN_PATH=/usr/bin/ -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-g++ -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-gcc`.

With CLion's build settings, I set the build generator to "Let CMake Decide".