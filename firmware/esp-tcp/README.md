# ESP32 TCP Tests

Builds on the [ESP-IDF examples for TCP server/client](https://github.com/espressif/esp-idf/tree/master/examples/protocols/sockets/tcp_server).

IO Setup:

- IO19 is driven with a 3.3V trigger pulse from my sig-gen.
- IO18 is a 3.3V output signal

## Firwmare

I run my development environment via docker

`docker run -i --privileged --rm -v $PWD:/project -w /project -it espressif/idf:release-v5.1`

I used `idf.py menuconfig` to set the compiler optimisation level to 'performance' for o2.

Also provide WiFi AP SSID/credentials in the Example Connection Configuration menu.

Use `idf.py build` and/or `idf.py -p /dev/ttyUSB0 flash` to build and flash to hardware.

