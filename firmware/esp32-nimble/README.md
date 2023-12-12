# ESP32 NimBLE BLE Test Firmware

Firmware and logs for ESP32 NimBLE BLE latency tests.

- I ran tests with builds set to -O2 (performance) via menuconfig.

IO Setup:

- IO19 is driven with a 3.3V trigger pulse from my sig-gen.
- IO18 is a 3.3V output signal

## Firwmare

I run my development environment via docker

`docker run -i --privileged --rm -v $PWD:/project -w /project -it espressif/idf:release-v5.1`

I used `idf.py menuconfig` to set:

- the compiler optimisation level to 'performance' for o2. 
- NimBLE internal logging output verbosity to `Warning` in "Component Config -> Bluetooth -> NimBLE Options NimBLE Host log verbosity (Warning logs)"

Use `idf.py build` and/or `idf.py -p /dev/ttyUSB0 flash` to build and flash to hardware.

# References

Espressif IDF SPP example
