# ESP32 802.15.4 Test Firmware

Firmware and logs for ESP32-C6 based latency tests.

IO Setup:

- IO19 is driven with a 3.3V trigger pulse from my sig-gen.
- IO18 is a 3.3V output signal

## Firwmare

Developed using CLion, and I run my IDF development environment via docker container:

`docker run -i --privileged --rm -v $PWD:/project -w /project -it espressif/idf:release-v4.4`

`idf.py set-target esp32c6`

I used `idf.py menuconfig` to set the compiler optimisation level to 'performance' for o2.

There's a 'throughput optimisation' setting in "Component Config/IEEE 802.15.4" menu that I also experimented with enabling.
