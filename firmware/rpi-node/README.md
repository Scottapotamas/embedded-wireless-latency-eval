# rpi-gpio-latency-test

Used NVM to install Node 20.11.0.
Make sure corepack is enabled.

`yarn install` to install everything.

IO connections are against Pins 11 (input) and 15 (output).

Change the websockets address in client.ts to match the server's IP and port.

## Simple test

`yarn simple` to run the simple GPIO test. Signal on pin is processed and another pin goes high then low.

## WS

Server / client receives the pin, sends the data via WS to the other, the other triggers pin high then low.

`yarn server` to start the server. `yarn client` to start the client.

# Test Variations

I tested with ethernet and Wifi-only connections for comparison.