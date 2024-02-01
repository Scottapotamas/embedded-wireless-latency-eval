import gpio from 'rpi-gpio'

// pins: 17/27/22
// Unsure if this is the correct mapping for rpi-gpio - it's not
// https://elinux.org/RPi_Low-level_peripherals

const enum CONFIG {
  PIN_IN = 11,
  PIN_OUT = 15,
}

function main() {
  console.log(`setting up... IN pin: ${CONFIG.PIN_IN}, OUT pin: ${CONFIG.PIN_OUT}`)

  // Setup IN
  gpio.setup(CONFIG.PIN_IN, gpio.DIR_IN, gpio.EDGE_RISING, err => {
    if (err !== null) {
      console.error(`Failed to setup IN:`, err)
    }
  })

  // Setup OUT
  gpio.setup(CONFIG.PIN_OUT, gpio.DIR_OUT, err => {
    if (err !== null) {
      console.error(`Failed to setup OUT:`, err)
    }
  })

  // Setup on change handler
  gpio.on('change', function (channel, value) {
    // Don't bother checking which channel it was, immediately write
    gpio.write(CONFIG.PIN_OUT, true, err => {
      if (err !== null) {
        console.error(`Failed to write HIGH:`, err)
      }

      // After that, write it low again
      gpio.write(CONFIG.PIN_OUT, false, err => {
        if (err !== null) {
          console.error(`Failed to write LOW:`, err)
        }

        // Do the console log now, after the latency sensitive bit
        // console.log(`channel ${channel}: ${value}, wrote high then low.`)
      })
    })
  })

  console.log(`ready`)
}

main()
