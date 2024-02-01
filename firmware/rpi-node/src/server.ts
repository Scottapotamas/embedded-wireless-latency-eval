import gpio from 'rpi-gpio'
import WebSocket from 'ws'

import { payload } from './payload'

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

  // Setup the pin function
  const writePinHighLow = () => {
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
        // console.log(`wrote high then low`)
      })
    })
  }

  // Setup WS
  const wss = new WebSocket.Server({ port: 8080 })

  // Hold the latest client outside of the connection closure
  let client: WebSocket | null = null

  wss.on('connection', function connection(ws, req) {
    const ip = req.socket.remoteAddress
    console.log(`connection detected from ${ip}, setting as client`)

    // Set the client to the latest connection received
    client = ws

    ws.on('error', console.error)

    ws.on('close', () => {
      console.log(`connection closed`)
      client = null
    })

    // When the server receives messages
    ws.on('message', data => {
      let type = -1
      let buf = data as Buffer

      // Validate the packet matches
      if (Buffer.isBuffer(data)) {
        // this is the expected route
        type = 0
      } else if (Array.isArray(data) && data.every(buf => Buffer.isBuffer(buf))) {
        // The TS types say it can sometimes be an array of buffers?
        // concat them so we can do the comparison.
        buf = Buffer.concat(data)

        type = 1
      } else {
        console.log(`unknown data was returned by WS:`, data)

        return
      }

      if (Buffer.compare(buf, payload) === 0) {
        writePinHighLow()
        // console.log(`(received data type ${type}), matched expected payload`)
      } else {
        console.log(`received data type ${type} but data didn't match, received:`, data)
      }
    })
  })

  // Setup on change handler
  gpio.on('change', function (channel, value) {
    // Don't bother checking which channel it was, immediately write to WS

    if (client) {
      client.send(payload, err => {
        if (err !== null) {
          console.error(`Failed to send data:`, err)
          return
        }
        // console.log(`sent data over WS`)
      })
    } else {
      console.log(`received GPIO but no WS client has connected`)
    }
  })

  console.log(`ready`)
}

main()
