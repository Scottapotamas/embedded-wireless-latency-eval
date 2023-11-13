# ESP32 GPIO Tests

Meant to provide basic implementation for comparison against STM32 GPIO interrupt and LED toggle performance.

## Firwmare

I run my development environment via docker

`docker run -i --privileged --rm -v $PWD:/project -w /project -it espressif/idf:release-v5.1`

I used `idf.py menuconfig` to set the compiler optimisation level to 'performance' for o3.

Use `idf.py build` and/or `idf.py -p /dev/ttyUSB0 flash` to build and flash to hardware.

