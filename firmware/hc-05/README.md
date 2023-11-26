# HC-05 Tests

The firmware used for these tests was the same as the UART test firmware.

The HC-05 runs at 38400 baud for AT commands, and at 9600 for data transfer by default

My modules report firmware version `2.0-20100601`

## AT Configuration

Connected via UART-TTL adapter at 38400 while holding EN button during power up.

CR+LF on enter should be configured in the serial terminal.

Check the native baud rate:

AT+UART?

Find the slave address:

AT+ROLE? should return role 0
AT+ADDR? should retrun an address 98d3:b1:fe609a

Configure the master to bind with the slave:

AT+ROLE=1
AT+CMODE=0
AT+BIND=98d3,b1,f3609a


# References

[Martyn Currey](https://www.martyncurrey.com/arduino-with-hc-05-bluetooth-module-at-mode/) wrote an accurate and useful blog post on AT configuration of these modules.
