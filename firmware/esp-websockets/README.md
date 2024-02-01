# ESP32 Websockets Tests

Builds on the [ESP-IDF examples for websockets echo](https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server/ws_echo_server).

IO Setup:

- IO19 is driven with a 3.3V trigger pulse from my sig-gen.
- IO18 is a 3.3V output signal

## Performance Tweaks

- Disable WiFi power saving (WIFI_PS_NONE=1) on server and client.
- Ensure CONFIG_ESP_WIFI_IRAM_OPT, CONFIG_LWIP_IRAM_OPTIMIZATION are enabled 

## Firwmare

I run my development environment via docker

`docker run -i --privileged --rm -v $PWD:/project -w /project -it espressif/idf:release-v5.1`

I used `idf.py menuconfig` to set the compiler optimisation level to 'performance' for o2, as well as IRAM configs mentioned above.

`CONFIG_HTTPD_WS_SUPPORT` must be enabled in the HTTP component config.

`idf.py add-dependency "espressif/esp_websocket_client^1.0.0"` was needed to add the client component, which is then in `/managed_components`

Also provide WiFi AP SSID/credentials in the Example Connection Configuration menu.

Use `idf.py build` and/or `idf.py -p /dev/ttyUSB0 flash` to build and flash to hardware.

