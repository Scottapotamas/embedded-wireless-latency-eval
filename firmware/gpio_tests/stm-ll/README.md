# STM32F429 LL EXTI Timing Test

This is a super-minimal test program that waits for the trigger signal via EXTI interrupt and then sets an IO pin high.

This is for comparison against Arduino and STCubeMX HAL implementation.

- PA0 is driven with a 3.3V trigger pulse from my sig-gen.
- PB0 is a 3.3V output signal (also connected to the nucleo's onboard green LED)

## Deps

I use CMake with CLion for development/builds, so this project is slightly opinionated.

Requires [`stm32-cmake`](https://github.com/ObKo/stm32-cmake) in this folder to get the build system setup etc.

I set my CMake options to use my system's arm gcc toolchain `-DSTM32_TOOLCHAIN_PATH=/usr/bin/ -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-g++ -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-gcc`.

With CLion's build settings, I set the build generator to "Let CMake Decide".