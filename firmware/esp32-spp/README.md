# ESP32 SPP Test Firmware

Firmware and logs for ESP32 Bluedroid Classic SPP latency tests.

- I've merged initiator and acceptor codebases into one program. Use the define at the top to choose.
- This example only does legacy pairing with hard-coded pin-code of 1234.
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

Run `idf.py menuconfig` and check that SPP is enabled (included sdkconfig.defaults should do this):

`Component config --> Bluetooth --> Bluedroid Options --> SPP`

# References

Espressif's [`bt_spp_initiator`](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/bluedroid/classic_bt/bt_spp_initiator) and acceptor example projects.