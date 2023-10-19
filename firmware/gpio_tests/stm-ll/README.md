# STM32F429 LL EXTI Timing Test

This is a super-minimal test program that waits for the trigger signal via EXTI interrupt and then sets an IO pin high.

This is for comparison against Arduino and STCubeMX HAL implementation.

## Deps

I use CMake with CLion for development/builds, so this project is slightly opinionated.

Requires `stm32-cmake` in this folder to get the build system setup etc.

I set my CMake options `-DSTM32_TOOLCHAIN_PATH=/usr/bin/ -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-g++ -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/arm-none-eabi-gcc`.
With CLion's build settings, I set the build generator to "Let CMake Decide".