# ESPNow

Firmware and logs for ESP32 ESPNow latency tests.

This is based on Espressif's example espnow project. I simplified it and made some changes:

- a pair of boards can learn each other's mac addresses via one-time broadcast at startup
- When the trigger pin condition is met _and_ a pair exists, blindly send the payload packet
- If an inbound payload packet arrives, parse it and check data for validity as per uart firmware etc

This firmware doesn't handle error cases particularly well, and isn't particularly refined.
It's intended to work well enough in lab conditions (ideal) and little else.

## Firwmare

Developed using CLion, and I run my IDF development environment via docker container:

`docker run -i --privileged --rm -v $PWD:/project -w /project -it espressif/idf:release-v4.4`
