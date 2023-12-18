# ESP32 UDP Tests

A flat copy of the TCP test firwmare with modifications made to socket connections for UDP behaviour.

Unlike the TCP test, the same firmware runs on both devices. The hard-coded IP needs to be set correctly though.

IO Setup:

- IO19 is driven with a 3.3V trigger pulse from my sig-gen.
- IO18 is a 3.3V output signal

## Performance Tweaks

- Disable WiFi power saving (WIFI_PS_NONE=1).
- Ensure CONFIG_ESP_WIFI_IRAM_OPT, CONFIG_LWIP_IRAM_OPTIMIZATION are enabled 

## Firwmare

I run my development environment via docker

`docker run -i --privileged --rm -v $PWD:/project -w /project -it espressif/idf:release-v5.1`

I used `idf.py menuconfig` to set the compiler optimisation level to 'performance' for o2, as well as IRAM configs mentioned above.

Also provide WiFi AP SSID/credentials in the Example Connection Configuration menu.

Use `idf.py build` and/or `idf.py -p /dev/ttyUSB0 flash` to build and flash to hardware.

