# ESP32 BLE Test Firmware

Firmware and logs for ESP32 Bluedroid BLE latency tests.


- I ran tests with builds set to -O2 (performance) via menuconfig.

IO Setup:

- IO19 is driven with a 3.3V trigger pulse from my sig-gen.
- IO18 is a 3.3V output signal

## Firwmare

I run my development environment via docker

`docker run -i --privileged --rm -v $PWD:/project -w /project -it espressif/idf:release-v5.1`

I used `idf.py menuconfig` to set the compiler optimisation level to 'performance' for o3.

Use `idf.py build` and/or `idf.py -p /dev/ttyUSB0 flash` to build and flash to hardware.

### Config



# References





# TODO

Remove uart tasks and redirect input/output as events into benchmark task


gap_event_handler and esp_gap_cb are similar and don't conflict, could condense server cases into existing client functionality

gatts_event_handler and esp_gattc_cb do nearly the same thing, could be condensed into one function

