#ifndef RFM95_H
#define RFM95_H

#include <stdint.h>
#include <stdbool.h>

#include "rfm95_defines.h"

// User callbacks for SPI register read/write functions
typedef uint32_t (*rfm9X_reg_rwr_fptr_t)(uint8_t reg_addr, uint8_t *reg_data, uint32_t len);

// Prototype definition for the required delay function
typedef void (*rfm9X_ms_delay_t)(uint32_t ms_count);

typedef void (*rfm9X_enable_irq_t)(void);

typedef void (*rfm9X_rx_data_cb_t)(uint8_t*, uint8_t);

// The radio can handle packet sizes up to 255 bytes long
// If you don't intend to send packets that long you can
// adjust/define this to a smaller value that will save
// some memory space.
#if !defined(RFM9X_RX_BUFFER_LEN)
#define RFM9X_RX_BUFFER_LEN 256
#endif

#if !defined(RFM9X_MAX_TX_LEN)
#define RFM9X_MAX_TX_LEN 255
#endif

// Generally this doesn't need to be adjusted, but just in
// case someone has a radio with a different clock.
#if !defined(RFM9X_BASE_CLOCK_FREQENCY)
#define RFM9X_BASE_CLOCK_FREQENCY (32000000)
#endif

typedef enum
{
    RFM9X_POLL_RX_ERROR = 0x00,
    RFM9X_POLL_NO_STATUS,
    RFM9X_POLL_RX_READY,
    RFM9X_POLL_TX_DONE
} rfm95_poll_status_t;

typedef enum
{
    RFM95_INTERRUPT_DIO0,
    RFM95_INTERRUPT_DIO1,
    RFM95_INTERRUPT_DIO5,
    RFM95_INTERRUPT_DIO_NUM
} rfm95_interrupt_t;


// Setup the callbacks and library internals.
rfm95_status_t rfm95_setup_library( rfm9X_reg_rwr_fptr_t read_cb,
                                    rfm9X_reg_rwr_fptr_t write_cb,
                                    rfm9X_enable_irq_t en_irq_cb,
                                    rfm9X_rx_data_cb_t rx_data_cb,
                                    rfm9X_ms_delay_t delay_cb );

// Call init with the following LoRa radio parameters.
// Hz is the target carrier/center frequency.  (i.e. 915000000)
// tx_power_dbm must be between 2 and 20.
rfm95_status_t rfm95_init_radio(  uint32_t center_frequency_hz,
                                  uint8_t tx_power_dbm,
                                  lora_bw_t bandwidth_channel,
                                  lora_cr_t coding_rate,
                                  lora_sf_t spreading_factor );

void rfm95_on_interrupt(rfm95_interrupt_t interrupt);

rfm95_poll_status_t handle_pending_interrupts( void );

rfm95_status_t set_irq( rfm95_interrupt_t dio, rfm95_irq_type irq_type );

rfm95_status_t clear_irq( void );

rfm95_status_t invert_tx_iq( void );

rfm95_status_t invert_rx_iq( void );

rfm95_status_t set_preamble( void );

rfm95_status_t set_lna( void );

// Set the chirp bandwidth, coding and spreading factor settings
rfm95_status_t set_chirp_config( lora_bw_t bandwidth_channel,
                                 lora_cr_t coding_rate,
                                 lora_sf_t spreading_factor );

// Use set_center_frequency to adjust the carrier frequency after initialization if desired
rfm95_status_t set_center_frequency( uint32_t Hz );

// Use set_power_amp to adjust the TX power after initialization if desired
// dBm must be between 5 and 20
rfm95_status_t set_power_amp( uint8_t dBm );

// Set modem max payload length
rfm95_status_t set_max_payload_length( uint8_t bytes );

// Use set_mode to select one of the operating modes given by lora_mode_t
rfm95_status_t set_mode( lora_mode_t mode );

// Read back the current mode from the radio
lora_mode_t get_mode( void );

// Return the Receive Signal Strength Indicator (approximated by the radio)
int16_t get_rssi( void );

// Ask the radio what it's version number is
uint8_t get_version( void );






// Instead of interrupt driven, this driver is polled.
// poll() must be called regularly when waiting for RX data in RFM9X_LORA_MODE_RX_CONTINUOUS
// mode.  It can also be called regularly during TX to determine when TX is complete.
// Once RX data is identified by polling call get_rx_data to copy the data to
// your local buffer.
// Returns one of the defined "RFM9X_POLL_..." status values
rfm95_poll_status_t poll( void );

// Sets the mode to RXContinuous and polls IRQ status once
// Assumes IRQ handler is able to run the poll() when DI0 fires
rfm95_poll_status_t start_rx_with_irq( void );

// Transmit the given data
rfm95_status_t send( uint8_t *data, uint8_t len );


#endif //RFM95_H