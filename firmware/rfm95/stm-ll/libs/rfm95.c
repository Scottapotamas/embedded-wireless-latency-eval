#include "rfm95.h"
#include "rfm95_private_defines.h"

/* -------------------------------------------------------------------------- */

static uint8_t read_byte(uint8_t addr);
static int write_byte(uint8_t addr, uint8_t data);

static void calculate_rssi( void );

/* -------------------------------------------------------------------------- */

pwl_rfm9X_reg_rwr_fptr_t _write_func;
pwl_rfm9X_reg_rwr_fptr_t _read_func;
pwl_rfm9X_enable_irq_t _enable_user_irq_func;
pwl_rfm9X_ms_delay_t     _delay_func;

uint8_t  _buffer[PWL_RFM9X_RX_BUFFER_LEN];
uint8_t  _rxlength;
volatile lora_mode_t _mode;
volatile int16_t  _rssi;
uint32_t _freq;
static bool rx_valid;

volatile bool pending_irq[RFM95_INTERRUPT_DIO_NUM] = { 0 };

/* -------------------------------------------------------------------------- */

static uint8_t read_byte( uint8_t addr )
{
    uint8_t data;
    if( _read_func(addr, &data, 1) == 0 )
    {
        return data;
    }

    return 0xFF;
}

static int write_byte( uint8_t addr, uint8_t data )
{
    int rval = _write_func(addr | 0x80, &data, 1);
    return rval;
}

/* -------------------------------------------------------------------------- */

rfm95_status_t rfm95_setup_library( pwl_rfm9X_reg_rwr_fptr_t read_cb,
                                    pwl_rfm9X_reg_rwr_fptr_t write_cb,
                                    pwl_rfm9X_enable_irq_t en_irq_cb,
                                    pwl_rfm9X_ms_delay_t delay_cb )
{
    // Setup callbacks
    if( read_cb )
    {
        _read_func = read_cb;
    }
    else
    {
        return RFM95_STATUS_ERROR;
    }

    if( write_cb )
    {
        _write_func = write_cb;
    }
    else
    {
        return RFM95_STATUS_ERROR;
    }

    if( en_irq_cb )
    {
        _enable_user_irq_func = en_irq_cb;
    }
    else
    {
        return RFM95_STATUS_ERROR;
    }

    if( delay_cb )
    {
        _delay_func = delay_cb;
    }
    else
    {
        return RFM95_STATUS_ERROR;
    }

    return RFM95_STATUS_OK;

}

/* -------------------------------------------------------------------------- */

rfm95_status_t rfm95_init_radio(  uint32_t center_frequency_hz,
                                  uint8_t tx_power_dbm,
                                  lora_bw_t bandwidth_channel,
                                  lora_cr_t coding_rate,
                                  lora_sf_t spreading_factor )
{
    // Does it match the expected version number?
    if( get_version() != RFM95_VERSION)
    {
        return RFM95_STATUS_ERROR;
    }

    // Must go into sleep mode first to set the LORA bit
    set_mode( RFM9X_MODE_SLEEP );
    set_mode( RFM9X_LORA_MODE_SLEEP );

    // If the radio is there, then we should be able to read mode as standby
    if( get_mode() != RFM9X_LORA_MODE_SLEEP )
    {
        return RFM95_STATUS_ERROR;
    }

    // Clear any pending interrupts and set masks
    // IO5 sends a 10Mhz squarewave until this is configured
    set_irq( RFM95_INTERRUPT_DIO1, RFM95_IRQ_RXDONE );

    // Call userspace to enable IRQ handling
    if( _enable_user_irq_func )
    {
        _enable_user_irq_func();
    }

    // Set FIFO addresses
    write_byte(RFM9X_REG_FifoTxBaseAddr, 0x80);
    write_byte(RFM9X_REG_FifoRxBaseAddr, 0);

    // TODO: Consider allowing the upstream user to set this
    set_max_payload_length(64);

    // Configure the radio

    // Set custom sync word
    write_byte(RFM9X_REG_SyncWord, 0x42);


    set_chirp_config( bandwidth_channel, coding_rate, spreading_factor );
    set_preamble();
    set_lna();
    set_center_frequency(center_frequency_hz);
    set_power_amp(tx_power_dbm);

    // Go back to sleep
    set_mode( RFM9X_LORA_MODE_SLEEP );

    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */


void rfm95_on_interrupt(rfm95_interrupt_t interrupt)
{
    pending_irq[interrupt] = true;
}

/* -------------------------------------------------------------------------- */

// Allows user-space to spend time in response to IRQ
void handle_pending_interrupts( void )
{
    if( pending_irq[RFM95_INTERRUPT_DIO0] && _mode == RFM9X_LORA_MODE_TX )
    {
        pending_irq[RFM95_INTERRUPT_DIO0] = false;
        set_mode(RFM9X_LORA_MODE_SLEEP);
    }

    if( pending_irq[RFM95_INTERRUPT_DIO0] && _mode == RFM9X_LORA_MODE_RX_CONTINUOUS )
    {
//        pending_irq[RFM95_INTERRUPT_DIO0] = false;
//        poll();
    }

    // TODO: consider doing something that doesn't talk as much?
    if( pending_irq[RFM95_INTERRUPT_DIO0] || pending_irq[RFM95_INTERRUPT_DIO1] || pending_irq[RFM95_INTERRUPT_DIO5] )
    {
//        poll();
    }
}

/* -------------------------------------------------------------------------- */

rfm95_status_t set_irq( rfm95_interrupt_t dio, rfm95_irq_type irq_type )
{
    // RFM9X_REG_DioMapping1 provides IO 0-3
    // RFM9X_REG_DioMapping2 provides IO 4,5 and Clkout frequency controls

    if( dio == RFM95_INTERRUPT_DIO1 )
    {
        switch(irq_type)
        {
            case RFM95_IRQ_TXDONE:
                write_byte(RFM9X_REG_DioMapping1, 0x40 );
                break;

            case RFM95_IRQ_RXDONE:
                write_byte( RFM9X_REG_DioMapping1, 0x00 );
                break;

            default:
                return RFM95_STATUS_ERROR;
            break;
        }
    }
    else
    {
        return RFM95_STATUS_ERROR;
    }

    return RFM95_STATUS_OK;
}

rfm95_status_t clear_irq( void )
{
    write_byte(RFM9X_REG_IrqFlags,0xFF );
    pending_irq[RFM95_INTERRUPT_DIO0] = false;
    pending_irq[RFM95_INTERRUPT_DIO1] = false;
    pending_irq[RFM95_INTERRUPT_DIO5] = false;

    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */

rfm95_status_t invert_tx_iq( void )
{
    // Set IQ registers according to AN1200.24.

    #define RFM95_REGISTER_INVERT_IQ_1_TX 0x27
    #define RFM95_REGISTER_INVERT_IQ_2_TX 0x1d

    write_byte(RFM9X_REG_InvertIQ,RFM95_REGISTER_INVERT_IQ_1_TX );
    write_byte(RFM9X_REG_InvertIQ2, RFM95_REGISTER_INVERT_IQ_2_TX);

    return RFM95_STATUS_OK;
}

rfm95_status_t invert_rx_iq( void )
{
    // Set IQ registers according to AN1200.24.

    // TODO cleanup registers
    #define RFM95_REGISTER_INVERT_IQ_1_RX 0x67
    #define RFM95_REGISTER_INVERT_IQ_2_RX 0x19

    write_byte(RFM9X_REG_InvertIQ, RFM95_REGISTER_INVERT_IQ_1_RX);
    write_byte(RFM9X_REG_InvertIQ2, RFM95_REGISTER_INVERT_IQ_2_RX);

    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */

rfm95_status_t set_preamble( void )
{
    // Preamble set to 8 + 4.25 = 12.25 symbols.
    write_byte(RFM9X_REG_PreambleMsb, 0x00 );
    write_byte(RFM9X_REG_PreambleLsb, 0x08 );

    // TODO make this user configurable

    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */

rfm95_status_t set_lna( void )
{
    // Set LNA to the highest gain with 150% boost.
    write_byte(RFM9X_REG_Lna, 0x23);

    // TODO make this user configurable

    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */

rfm95_status_t set_chirp_config( lora_bw_t bandwidth_channel,
                                 lora_cr_t coding_rate,
                                 lora_sf_t spreading_factor )
{
    uint8_t config = 0;

    config  = (uint8_t) bandwidth_channel << RFM9X_LORA_BW_BitPos;
    config |= (uint8_t) coding_rate << RFM9X_LORA_CR_BitPos;
    // Enable ImplicitHeaderModeOn with high lowest bit
//    config |= 0x01;
    write_byte(RFM9X_REG_ModemConfig1, config);

    config = 0;
    config  = (uint8_t) spreading_factor << RFM9X_LORA_SF_BitPos;
    config |= 0x00 << 3;  // TxContinuousMode when 1, normal when 0
    config |= 0x01 << 2;  // RxPayloadCrcOn when 1, disable when 0
    //config |= 0x00;       // Lowest two bytes are RX SymbTimeout MSB
    write_byte(RFM9X_REG_ModemConfig2, config);

    config = 0;
    // Top 4 bytes unused
    config |= 0x00 << 3;  // LowDataRateOptimize when 1 (needed for >16ms symbol len), normal when 0
    config |= 0x01 << 2;  // AgcAutoOn when 1, set by LnaGain register when 0
    write_byte(RFM9X_REG_ModemConfig3, config);

    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */

rfm95_status_t set_center_frequency(uint32_t Hz)
{
    // Per manual, fRF = (fosc * Frf)  / 2^19
    // (2 ^ 19) = 524288
    uint64_t frf = ((uint64_t)Hz << 19) / PWL_RFM9X_BASE_CLOCK_FREQENCY;

    write_byte(RFM9X_REG_FrfMsb, (frf >> 16) & 0xFF);
    write_byte(RFM9X_REG_FrfMid, (frf >>  8) & 0xFF);
    write_byte(RFM9X_REG_FrfLsb, (frf >>  0) & 0xFF);
    _freq = Hz;

    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */

rfm95_status_t set_power_amp( uint8_t dBm )
{
    if (dBm < 2 || dBm > 20)
    {
        return RFM95_STATUS_ERROR;
    }

    uint8_t pa_dac = 0;
    rfm95_pa_config_byte_t pa_config = { 0 };
    pa_config.max_power = 7;
    pa_config.pa_select = 1;

    if (dBm >= 2 && dBm <= 17)
    {
        pa_config.output_power = (dBm - 2);
        pa_dac = RFM9X_REG_PA_DAC_LOW_POWER;
    }
    else if (dBm == 20)
    {
        pa_config.output_power = 15;
        pa_dac = RFM9X_REG_PA_DAC_HIGH_POWER;
    }

    write_byte(RFM9X_REG_PaConfig, pa_config.buffer);
    write_byte(RFM9X_REG_PaDac, pa_dac);

    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
rfm95_status_t set_max_payload_length( uint8_t bytes )
{
    write_byte(RFM9X_REG_MaxPayloadLength, bytes);
    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */

rfm95_status_t set_mode( lora_mode_t mode )
{
    if( mode == RFM9X_LORA_MODE_RX )
    {
        rx_valid = false;
    }

    _mode = mode;

    if( mode != RFM9X_LORA_MODE_INVALID )
    {
        write_byte(RFM9X_REG_OpMode, mode);
    }

    return RFM95_STATUS_OK;
}

/* -------------------------------------------------------------------------- */

lora_mode_t get_mode( void )
{
    lora_mode_t mode = read_byte(RFM9X_REG_OpMode) & 0x87;  // mask for lora bit and lowest 3 bits

    // & 0x07; // mask for lowest 3 bits
    _mode = mode;   // TODO: remove this?
    return mode;
}

/* -------------------------------------------------------------------------- */

int16_t get_rssi( void )
{
    return _rssi;
}

static void calculate_rssi( void )
{
    int8_t   snr = 0;
    int32_t  tmp = 0;

    // Calculate RSSI
    tmp = (int32_t) read_byte(RFM9X_REG_PktRssiValue);
    snr = ((int8_t) read_byte(RFM9X_REG_PktSnrValue)) >> 2;

    if( snr >= 0 )
    {
        tmp = (tmp << 4) / 15;
    }
    else
    {
        tmp += snr;
    }

    // LF output
    if( _freq <= 525000000 )
    {
        _rssi = (int8_t) (tmp - 164);
    }
    else
    {
        _rssi = (int8_t) (tmp - 157);
    }
}

/* -------------------------------------------------------------------------- */

uint8_t get_version( void )
{
    uint8_t version = read_byte(RFM9X_REG_Version);
    return version;
}

/* -------------------------------------------------------------------------- */












int poll( )
{
    uint8_t  iflags;

    iflags = read_byte(RFM9X_REG_IrqFlags);
    write_byte(RFM9X_REG_IrqFlags, iflags);

    iflags &= PWL_RFM9X_INTERRUPT_STATUS_MASK;
    if( !iflags )
    {
        return PWL_RFM9X_POLL_NO_STATUS;
    }

    switch( _mode )
    {
        case RFM9X_LORA_MODE_RX_CONTINUOUS:
            if( (iflags & PWL_RFM9X_RX_DONE_INTERRUPT_FLAG) )
            {
                // It is an error condition to be in this if statement
                // since while in the RX, any interrupt that gets us here
                // should include the RX Done flag.

                // Return to standby and restart the RX
                set_mode(RFM9X_LORA_MODE_STDBY);
                set_mode(RFM9X_LORA_MODE_RX_CONTINUOUS);
                return PWL_RFM9X_POLL_RX_ERROR;
            }

            // RX is done... check that we have a valid CRC
            // If not, then restart the RX process and wait for another packet
            if( ((read_byte(RFM9X_REG_HopChannel) & 0b01000000) == 0)
                || (iflags & PWL_RFM9X_RX_CRC_ERROR_FLAG) )
            {
                set_mode(RFM9X_LORA_MODE_STDBY);
                set_mode(RFM9X_LORA_MODE_RX_CONTINUOUS);
                return PWL_RFM9X_POLL_RX_ERROR;
            }

            _rxlength = read_byte(RFM9X_REG_RxNbBytes);
            uint8_t caddr = read_byte(RFM9X_REG_FifoRxCurrentAddr);
            write_byte(RFM9X_REG_FifoAddrPtr, caddr);
            _read_func(RFM9X_REG_Fifo, _buffer, _rxlength);

            set_mode(RFM9X_LORA_MODE_STDBY);

            calculate_rssi();

            rx_valid = true;

            return PWL_RFM9X_POLL_RX_READY;
            break;

        case RFM9X_LORA_MODE_TX:
            if ((iflags & 0b00001000) == 0b00001000)
            {
                set_mode(RFM9X_LORA_MODE_STDBY);
                return PWL_RFM9X_POLL_TX_DONE;
            }
            break;
    }

    return PWL_RFM9X_POLL_NO_STATUS;
}

bool rx_data_ready()
{
    if( !rx_valid )
    {
        if( poll() == PWL_RFM9X_POLL_NO_STATUS )
        {
            if( (_mode != RFM9X_LORA_MODE_RX_CONTINUOUS) && (_mode != RFM9X_LORA_MODE_FSRX) )
            {
                set_mode(RFM9X_LORA_MODE_RX_CONTINUOUS);
            }
        }
    }

    return rx_valid;
}

uint8_t receive( uint8_t* buf, uint8_t* len )
{
    if( rx_data_ready() )
    {
        uint8_t idx = 0;
        while (idx < *len && idx < _rxlength)
        {
            buf[idx] = _buffer[idx];
            ++idx;
        }
        *len = idx;
        rx_valid = false;
        return idx;
    }

    return 0;
}


rfm95_status_t send( uint8_t* data, uint8_t len )
{
    if( !data || len == 0 )
    {
        return RFM95_STATUS_ERROR;
    }

    // Clear TX interrupts
    set_irq(RFM95_INTERRUPT_DIO1, RFM95_IRQ_TXDONE);
    clear_irq();

    set_mode( RFM9X_LORA_MODE_STDBY );

    // Wait for module to be ready
    while(!pending_irq[RFM95_INTERRUPT_DIO5])
    {
        // Wait here
        asm("NOP");
    }

    write_byte(RFM9X_REG_FifoAddrPtr, 0x80);
    write_byte(RFM9X_REG_PayloadLength, len);

    // Ensure the command has write bit set high as this uses the users 'bulk write' callback directly
    _write_func(RFM9X_REG_Fifo | 0x80, data, len);

    set_mode( RFM9X_LORA_MODE_TX );

    return RFM95_STATUS_OK;
}

