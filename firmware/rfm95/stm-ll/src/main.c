/* -------------------------------------------------------------------------- */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

#include "stm32f4xx_ll_cortex.h"
#include <stm32f4xx_ll_rcc.h>
#include <stm32f4xx_ll_system.h>
#include <stm32f4xx_ll_bus.h>
#include <stm32f4xx_ll_pwr.h>
#include <stm32f4xx_ll_utils.h>

#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_spi.h"

#include "rfm95.h"

/* -------------------------------------------------------------------------- */

//#define PAYLOAD_12B
//#define PAYLOAD_128B
 #define PAYLOAD_1024B

/* -------------------------------------------------------------------------- */

void hal_core_init( void );
void hal_core_clock_configure( void );
void portAssertHandler( const char *file,
                        unsigned    line,
                        const char *fmt,
                        ... );

/* -------------------------------------------------------------------------- */

void setup_dwt( void );
void setup_gpio_output( void );
void setup_gpio_input( void );
void setup_rfm95_io( void );
void setup_spi( void );

/* -------------------------------------------------------------------------- */

static void crc16(uint8_t data, uint16_t *crc);
volatile bool trigger_pending = false;

#define CRC_SEED (0xFFFFu)
uint32_t bytes_to_send = 0;
uint32_t bytes_sent = 0;
uint32_t bytes_read = 0;
uint16_t working_crc = CRC_SEED;
uint16_t payload_crc = 0x00;

#if defined(PAYLOAD_12B)
    uint8_t test_payload[12] = {
            0x00,
            0x01, 0x02, 0x03, 0x04, 0x05,
            0x06, 0x07, 0x08, 0x09, 0x0A,
            0x0B,
    };
#elif defined(PAYLOAD_128B)
    uint8_t test_payload[128] = {
            0x00,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
            0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
            0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
            0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
            0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
            0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C,
            0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
            0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
            0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
            0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64,
            0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E,
            0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
            0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
    };
#elif defined(PAYLOAD_1024B)
    uint8_t test_payload[1024] = {
            0x00,
            // Sequential set of 255 bytes
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
            0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
            0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
            0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
            0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
            0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C,
            0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
            0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
            0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
            0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64,
            0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E,
            0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
            0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82,
            0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C,
            0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
            0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0,
            0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA,
            0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4,
            0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE,
            0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
            0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2,
            0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC,
            0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
            0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0,
            0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,
            0xFB, 0xFC, 0xFD, 0xFE, 0xFF,

            // Next run of 255 bytes consumes 510 bytes
            0x01, 0x01, 0x01, 0x02, 0x01, 0x03, 0x01, 0x04,0x01, 0x05,
            0x01, 0x06, 0x01, 0x07, 0x01, 0x08, 0x01, 0x09,0x01, 0x0A,
            0x01, 0x0B, 0x01, 0x0C, 0x01, 0x0D, 0x01, 0x0E,0x01, 0x0F,
            0x01, 0x10, 0x01, 0x11, 0x01, 0x12, 0x01, 0x13,0x01, 0x14,
            0x01, 0x15, 0x01, 0x16, 0x01, 0x17, 0x01, 0x18,0x01, 0x19,
            0x01, 0x1A, 0x01, 0x1B, 0x01, 0x1C, 0x01, 0x1D,0x01, 0x1E,
            0x01, 0x1F, 0x01, 0x20, 0x01, 0x21, 0x01, 0x22,0x01, 0x23,
            0x01, 0x24, 0x01, 0x25, 0x01, 0x26, 0x01, 0x27,0x01, 0x28,
            0x01, 0x29, 0x01, 0x2A, 0x01, 0x2B, 0x01, 0x2C,0x01, 0x2D,
            0x01, 0x2E, 0x01, 0x2F, 0x01, 0x30, 0x01, 0x31,0x01, 0x32,
            0x01, 0x33, 0x01, 0x34, 0x01, 0x35, 0x01, 0x36,0x01, 0x37,
            0x01, 0x38, 0x01, 0x39, 0x01, 0x3A, 0x01, 0x3B,0x01, 0x3C,
            0x01, 0x3D, 0x01, 0x3E, 0x01, 0x3F, 0x01, 0x40,0x01, 0x41,
            0x01, 0x42, 0x01, 0x43, 0x01, 0x44, 0x01, 0x45,0x01, 0x46,
            0x01, 0x47, 0x01, 0x48, 0x01, 0x49, 0x01, 0x4A,0x01, 0x4B,
            0x01, 0x4C, 0x01, 0x4D, 0x01, 0x4E, 0x01, 0x4F,0x01, 0x50,
            0x01, 0x51, 0x01, 0x52, 0x01, 0x53, 0x01, 0x54,0x01, 0x55,
            0x01, 0x56, 0x01, 0x57, 0x01, 0x58, 0x01, 0x59,0x01, 0x5A,
            0x01, 0x5B, 0x01, 0x5C, 0x01, 0x5D, 0x01, 0x5E,0x01, 0x5F,
            0x01, 0x60, 0x01, 0x61, 0x01, 0x62, 0x01, 0x63,0x01, 0x64,
            0x01, 0x65, 0x01, 0x66, 0x01, 0x67, 0x01, 0x68,0x01, 0x69,
            0x01, 0x6A, 0x01, 0x6B, 0x01, 0x6C, 0x01, 0x6D,0x01, 0x6E,
            0x01, 0x6F, 0x01, 0x70, 0x01, 0x71, 0x01, 0x72,0x01, 0x73,
            0x01, 0x74, 0x01, 0x75, 0x01, 0x76, 0x01, 0x77,0x01, 0x78,
            0x01, 0x79, 0x01, 0x7A, 0x01, 0x7B, 0x01, 0x7C,0x01, 0x7D,
            0x01, 0x7E, 0x01, 0x7F, 0x01, 0x80, 0x01, 0x81,0x01, 0x82,
            0x01, 0x83, 0x01, 0x84, 0x01, 0x85, 0x01, 0x86,0x01, 0x87,
            0x01, 0x88, 0x01, 0x89, 0x01, 0x8A, 0x01, 0x8B,0x01, 0x8C,
            0x01, 0x8D, 0x01, 0x8E, 0x01, 0x8F, 0x01, 0x90,0x01, 0x91,
            0x01, 0x92, 0x01, 0x93, 0x01, 0x94, 0x01, 0x95,0x01, 0x96,
            0x01, 0x97, 0x01, 0x98, 0x01, 0x99, 0x01, 0x9A,0x01, 0x9B,
            0x01, 0x9C, 0x01, 0x9D, 0x01, 0x9E, 0x01, 0x9F,0x01, 0xA0,
            0x01, 0xA1, 0x01, 0xA2, 0x01, 0xA3, 0x01, 0xA4,0x01, 0xA5,
            0x01, 0xA6, 0x01, 0xA7, 0x01, 0xA8, 0x01, 0xA9,0x01, 0xAA,
            0x01, 0xAB, 0x01, 0xAC, 0x01, 0xAD, 0x01, 0xAE,0x01, 0xAF,
            0x01, 0xB0, 0x01, 0xB1, 0x01, 0xB2, 0x01, 0xB3,0x01, 0xB4,
            0x01, 0xB5, 0x01, 0xB6, 0x01, 0xB7, 0x01, 0xB8,0x01, 0xB9,
            0x01, 0xBA, 0x01, 0xBB, 0x01, 0xBC, 0x01, 0xBD,0x01, 0xBE,
            0x01, 0xBF, 0x01, 0xC0, 0x01, 0xC1, 0x01, 0xC2,0x01, 0xC3,
            0x01, 0xC4, 0x01, 0xC5, 0x01, 0xC6, 0x01, 0xC7,0x01, 0xC8,
            0x01, 0xC9, 0x01, 0xCA, 0x01, 0xCB, 0x01, 0xCC,0x01, 0xCD,
            0x01, 0xCE, 0x01, 0xCF, 0x01, 0xD0, 0x01, 0xD1,0x01, 0xD2,
            0x01, 0xD3, 0x01, 0xD4, 0x01, 0xD5, 0x01, 0xD6,0x01, 0xD7,
            0x01, 0xD8, 0x01, 0xD9, 0x01, 0xDA, 0x01, 0xDB,0x01, 0xDC,
            0x01, 0xDD, 0x01, 0xDE, 0x01, 0xDF, 0x01, 0xE0,0x01, 0xE1,
            0x01, 0xE2, 0x01, 0xE3, 0x01, 0xE4, 0x01, 0xE5,0x01, 0xE6,
            0x01, 0xE7, 0x01, 0xE8, 0x01, 0xE9, 0x01, 0xEA,0x01, 0xEB,
            0x01, 0xEC, 0x01, 0xED, 0x01, 0xEE, 0x01, 0xEF,0x01, 0xF0,
            0x01, 0xF1, 0x01, 0xF2, 0x01, 0xF3, 0x01, 0xF4,0x01, 0xF5,
            0x01, 0xF6, 0x01, 0xF7, 0x01, 0xF8, 0x01, 0xF9,0x01, 0xFA,
            0x01, 0xFB, 0x01, 0xFC, 0x01, 0xFD, 0x01, 0xFE,  0x01, 0xFF,

            // 1+255+510 bytes are done, this is the remaining 258B for total of 1024
            0x02, 0x01, 0x02, 0x02, 0x02, 0x03, 0x02, 0x04, 0x02, 0x05,
            0x02, 0x06, 0x02, 0x07, 0x02, 0x08, 0x02, 0x09, 0x02, 0x0A,
            0x02, 0x0B, 0x02, 0x0C, 0x02, 0x0D, 0x02, 0x0E, 0x02, 0x0F,
            0x02, 0x10, 0x02, 0x11, 0x02, 0x12, 0x02, 0x13, 0x02, 0x14,
            0x02, 0x15, 0x02, 0x16, 0x02, 0x17, 0x02, 0x18, 0x02, 0x19,
            0x02, 0x1A, 0x02, 0x1B, 0x02, 0x1C, 0x02, 0x1D, 0x02, 0x1E,
            0x02, 0x1F, 0x02, 0x20, 0x02, 0x21, 0x02, 0x22, 0x02, 0x23,
            0x02, 0x24, 0x02, 0x25, 0x02, 0x26, 0x02, 0x27, 0x02, 0x28,
            0x02, 0x29, 0x02, 0x2A, 0x02, 0x2B, 0x02, 0x2C, 0x02, 0x2D,
            0x02, 0x2E, 0x02, 0x2F, 0x02, 0x30, 0x02, 0x31, 0x02, 0x32,
            0x02, 0x33, 0x02, 0x34, 0x02, 0x35, 0x02, 0x36, 0x02, 0x37,
            0x02, 0x38, 0x02, 0x39, 0x02, 0x3A, 0x02, 0x3B, 0x02, 0x3C,
            0x02, 0x3D, 0x02, 0x3E, 0x02, 0x3F, 0x02, 0x40, 0x02, 0x41,
            0x02, 0x42, 0x02, 0x43, 0x02, 0x44, 0x02, 0x45, 0x02, 0x46,
            0x02, 0x47, 0x02, 0x48, 0x02, 0x49, 0x02, 0x4A, 0x02, 0x4B,
            0x02, 0x4C, 0x02, 0x4D, 0x02, 0x4E, 0x02, 0x4F, 0x02, 0x50,
            0x02, 0x51, 0x02, 0x52, 0x02, 0x53, 0x02, 0x54, 0x02, 0x55,
            0x02, 0x56, 0x02, 0x57, 0x02, 0x58, 0x02, 0x59, 0x02, 0x5A,
            0x02, 0x5B, 0x02, 0x5C, 0x02, 0x5D, 0x02, 0x5E, 0x02, 0x5F,
            0x02, 0x60, 0x02, 0x61, 0x02, 0x62, 0x02, 0x63, 0x02, 0x64,
            0x02, 0x65, 0x02, 0x66, 0x02, 0x67, 0x02, 0x68, 0x02, 0x69,
            0x02, 0x6A, 0x02, 0x6B, 0x02, 0x6C, 0x02, 0x6D, 0x02, 0x6E,
            0x02, 0x6F, 0x02, 0x70, 0x02, 0x71, 0x02, 0x72, 0x02, 0x73,
            0x02, 0x74, 0x02, 0x75, 0x02, 0x76, 0x02, 0x77, 0x02, 0x78,
            0x02, 0x79, 0x02, 0x7A, 0x02, 0x7B, 0x02, 0x7C, 0x02, 0x7D,
            0x02, 0x7E, 0x02, 0x7F, 0x02, 0x80, 0x02, 0x81
    };
#endif

/* -------------------------------------------------------------------------- */

static uint32_t spi_read_cb(uint8_t reg_addr, uint8_t *buffer, uint32_t length);
static uint32_t spi_write_cb(uint8_t reg_addr, uint8_t *buffer, uint32_t length);
static void enable_irq_cb( void );

volatile uint8_t rx_tmp[512] = { 0 };
uint8_t bytes_held = 0;

/* -------------------------------------------------------------------------- */

static inline void spi_cs_low( void )
{
    LL_GPIO_ResetOutputPin( GPIOA, LL_GPIO_PIN_4 );
}

static inline void spi_cs_high( void )
{
    LL_GPIO_SetOutputPin( GPIOA, LL_GPIO_PIN_4 );
}

static inline uint8_t spi_ll_rw(uint8_t data)
{
    LL_SPI_Enable(SPI1);
    // Wait until TX buffer is empty
    while (LL_SPI_IsActiveFlag_BSY(SPI1));
    while (!LL_SPI_IsActiveFlag_TXE(SPI1));
    LL_SPI_TransmitData8(SPI1, data);
    while (!LL_SPI_IsActiveFlag_RXNE(SPI1));

    return LL_SPI_ReceiveData8(SPI1);
}

static uint32_t spi_read_cb(uint8_t reg_addr, uint8_t *buffer, uint32_t length)
{
    spi_cs_low();
    spi_ll_rw((uint8_t)reg_addr );

    while( length-- )
    {
        *buffer++ = spi_ll_rw( 0x00 );
    }

    spi_cs_high();
    return 0;
}

static uint32_t spi_write_cb(uint8_t reg_addr, uint8_t *buffer, uint32_t length)
{
    spi_cs_low();
    spi_ll_rw((uint8_t)reg_addr | 0x80u);
    for (uint32_t i = 0; i < length; i++)
    {
        spi_ll_rw(buffer[i] );
    }

    spi_cs_high();
    return 0;
}

static void enable_irq_cb( void )
{
    // IRQ config
    NVIC_SetPriority(EXTI3_IRQn, NVIC_EncodePriority(
            NVIC_GetPriorityGrouping(),
            0,
            0
    ));
    NVIC_SetPriority(EXTI9_5_IRQn, NVIC_EncodePriority(
            NVIC_GetPriorityGrouping(),
            0,
            0
    ));

    NVIC_EnableIRQ(EXTI3_IRQn);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}

static void rx_data_cb( uint8_t *data, uint8_t length )
{
    // copy here
    memcpy(rx_tmp, data, sizeof(rx_tmp) );
    bytes_held = length;
}

/* -------------------------------------------------------------------------- */

int main(void)
{
    hal_core_init();
    hal_core_clock_configure();

    setup_dwt();
    setup_gpio_output();
    setup_gpio_input();

    // RFM9x init/setup
    setup_rfm95_io();
    setup_spi();

    // Work out the correct CRC for the active payload
    working_crc = CRC_SEED;
    for( uint32_t i = 0; i < sizeof(test_payload); i++ )
    {
        crc16( test_payload[i], &working_crc );
    }

    payload_crc = working_crc;
    working_crc = CRC_SEED;

    // Radio setup
    rfm95_setup_library( &spi_read_cb,
                         &spi_write_cb,
                         &enable_irq_cb,
                         &rx_data_cb,
                         &LL_mDelay );

    // Strobe the reset pin
    LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_4);
    LL_mDelay( 1 );
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_4);

    // Wait for module in case this is a fresh power-on
    LL_mDelay( 10 );

    rfm95_status_t status;
    status = rfm95_init_radio(915000000,
                              20,
                              RFM9X_LORA_BW_250k,
                              RFM9X_LORA_CR_4_5,
                              RFM9X_LORA_SF_128);

    if( status == RFM95_STATUS_ERROR )
    {
        while(1)
        {
            // Blink to show error state
            LL_GPIO_TogglePin( GPIOB, LL_GPIO_PIN_0 );
            LL_mDelay(100);
        }
    }

    LL_mDelay(20);

    // todo consider a better way to ask the library to make this change
    //      or just remove indirection
    // Start a continuous read
    start_rx_with_irq();
    rfm95_poll_status_t irq_status = RFM9X_POLL_RX_ERROR;

    while(1)
    {
        irq_status = handle_pending_interrupts();

        // TX complete IRQ
        if( irq_status == RFM9X_POLL_TX_DONE )
        {
            bytes_sent += bytes_to_send;    // Previous burst was OK, increment position

            // Send the next part of the test payload if needed
            if(bytes_sent < sizeof(test_payload) )
            {
                bytes_to_send = sizeof(test_payload) - bytes_sent;
                if( bytes_to_send > RFM9X_MAX_TX_LEN )
                {
                    bytes_to_send = RFM9X_MAX_TX_LEN;
                }

                // Send the data
                send((uint8_t *)&test_payload[bytes_sent], bytes_to_send );
            }
            else
            {
                // Reset for next fresh packet
                bytes_sent = 0;
            }
        }

        // Check inbound data for valid test payload sequences
        if( bytes_held )
        {
            for( uint8_t i = 0; i < bytes_held; i++ )
            {
                // Reset the "parser"
                if(rx_tmp[i] == 0x00 )
                {
                    bytes_read = 0;
                    working_crc = CRC_SEED;
                }

                crc16( rx_tmp[i], &working_crc );
                bytes_read++;

                // Identify the end of the packet via expected length and correct CRC
                if( bytes_read == sizeof(test_payload) && working_crc == payload_crc )
                {
                    LL_GPIO_SetOutputPin( GPIOB, LL_GPIO_PIN_0 );
                }

            }

            bytes_held = 0;
            memset(rx_tmp, 0, sizeof(rx_tmp));

            LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_0 );
            start_rx_with_irq();
        }

        // Send a packet when triggered
        if(trigger_pending)
        {
            // Copy the first slice into the transmit buffer
            bytes_to_send = sizeof(test_payload);
            if( bytes_to_send > RFM9X_MAX_TX_LEN )
            {
                bytes_to_send = RFM9X_MAX_TX_LEN;
            }

            send((uint8_t *)&test_payload[bytes_sent], bytes_to_send );

//            LL_GPIO_SetOutputPin( GPIOB, LL_GPIO_PIN_0 );
            trigger_pending = false;
        }
        else
        {
            // GPIO low
//            LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_0 );
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------- */

static void crc16(uint8_t data, uint16_t *crc)
{
    *crc  = (uint8_t)(*crc >> 8) | (*crc << 8);
    *crc ^= data;
    *crc ^= (uint8_t)(*crc & 0xff) >> 4;
    *crc ^= (*crc << 8) << 4;
    *crc ^= ((*crc & 0xff) << 4) << 1;
}

/* -------------------------------------------------------------------------- */

void setup_dwt( void )
{
    //Enable the DWT timer
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* -------------------------------------------------------------------------- */

void setup_gpio_output( void )
{
    LL_PWR_DisableWakeUpPin( LL_PWR_WAKEUP_PIN1 );

    // PB0
    LL_AHB1_GRP1_EnableClock( LL_AHB1_GRP1_PERIPH_GPIOB );

    LL_GPIO_SetPinMode( GPIOB, LL_GPIO_PIN_0, LL_GPIO_MODE_OUTPUT );
    LL_GPIO_SetPinSpeed( GPIOB, LL_GPIO_PIN_0, LL_GPIO_SPEED_FREQ_LOW );
    LL_GPIO_SetPinOutputType( GPIOB, LL_GPIO_PIN_0, LL_GPIO_OUTPUT_PUSHPULL );
    LL_GPIO_SetPinPull( GPIOB, LL_GPIO_PIN_0, LL_GPIO_PULL_NO );
    LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_0 );
}

/* -------------------------------------------------------------------------- */

void setup_gpio_input( void )
{
    // PA0 as input
    LL_AHB1_GRP1_EnableClock( LL_AHB1_GRP1_PERIPH_GPIOA );

    LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_0, LL_GPIO_MODE_INPUT );
    LL_GPIO_SetPinSpeed( GPIOA, LL_GPIO_PIN_0, LL_GPIO_SPEED_FREQ_LOW );
    LL_GPIO_SetPinOutputType( GPIOA, LL_GPIO_PIN_0, LL_GPIO_MODE_INPUT );
    LL_GPIO_SetPinPull( GPIOA, LL_GPIO_PIN_0, LL_GPIO_PULL_NO );
    LL_GPIO_ResetOutputPin( GPIOA, LL_GPIO_PIN_0 );

    // EXTI0 setup
    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_0);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_0);

    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTA, LL_SYSCFG_EXTI_LINE0);

    // IRQ config
    NVIC_SetPriority(EXTI0_IRQn, NVIC_EncodePriority(
            NVIC_GetPriorityGrouping(),
            0,
            0
            ));
    NVIC_EnableIRQ(EXTI0_IRQn);

}

/* -------------------------------------------------------------------------- */

void setup_rfm95_io( void )
{
    // PB4 - Reset
    LL_AHB1_GRP1_EnableClock( LL_AHB1_GRP1_PERIPH_GPIOB );

    LL_GPIO_SetPinMode( GPIOB, LL_GPIO_PIN_4, LL_GPIO_MODE_OUTPUT );
    LL_GPIO_SetPinSpeed( GPIOB, LL_GPIO_PIN_4, LL_GPIO_SPEED_FREQ_LOW );
    LL_GPIO_SetPinOutputType( GPIOB, LL_GPIO_PIN_4, LL_GPIO_OUTPUT_PUSHPULL );
    LL_GPIO_SetPinPull( GPIOB, LL_GPIO_PIN_4, LL_GPIO_PULL_NO );
    LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_4 );

    // PB3 - IRQ pin
    LL_GPIO_SetPinMode( GPIOB, LL_GPIO_PIN_3, LL_GPIO_MODE_INPUT );
    LL_GPIO_SetPinSpeed( GPIOB, LL_GPIO_PIN_3, LL_GPIO_SPEED_FREQ_MEDIUM );
    LL_GPIO_SetPinPull( GPIOB, LL_GPIO_PIN_3, LL_GPIO_PULL_NO );
    LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_3 );

    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_3);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_3);
    LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_3);

    // PB8 - IRQ
    LL_GPIO_SetPinMode( GPIOB, LL_GPIO_PIN_8, LL_GPIO_MODE_INPUT );
    LL_GPIO_SetPinSpeed( GPIOB, LL_GPIO_PIN_8, LL_GPIO_SPEED_FREQ_MEDIUM );
    LL_GPIO_SetPinPull( GPIOB, LL_GPIO_PIN_8, LL_GPIO_PULL_NO );
    LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_8 );

    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_8);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_8);
    LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_8);

    // PB9 - IRQ
    LL_GPIO_SetPinMode( GPIOB, LL_GPIO_PIN_9, LL_GPIO_MODE_INPUT );
    LL_GPIO_SetPinSpeed( GPIOB, LL_GPIO_PIN_9, LL_GPIO_SPEED_FREQ_MEDIUM );
    LL_GPIO_SetPinPull( GPIOB, LL_GPIO_PIN_9, LL_GPIO_PULL_NO );
    LL_GPIO_ResetOutputPin( GPIOB, LL_GPIO_PIN_9 );

    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_9);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_9);
    LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_9);


    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTB, LL_SYSCFG_EXTI_LINE3);
    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTB, LL_SYSCFG_EXTI_LINE8);
    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTB, LL_SYSCFG_EXTI_LINE9);

    // NVIC IRQ are setup in callback from library setup as the module defuaults to 10Mhz clock on DIO5
}

/* -------------------------------------------------------------------------- */

void setup_spi( void )
{
    // IO setup in alternate function mode
    LL_AHB1_GRP1_EnableClock( LL_AHB1_GRP1_PERIPH_GPIOA );

    // PA5 for SPI CLK
    LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_5, LL_GPIO_MODE_OUTPUT );
    LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_5, LL_GPIO_MODE_ALTERNATE );
    LL_GPIO_SetAFPin_0_7( GPIOA, LL_GPIO_PIN_5, LL_GPIO_AF_5 );
    LL_GPIO_SetPinSpeed( GPIOA, LL_GPIO_PIN_5, LL_GPIO_SPEED_FREQ_VERY_HIGH );
    LL_GPIO_SetPinOutputType( GPIOA, LL_GPIO_PIN_5, LL_GPIO_OUTPUT_PUSHPULL );
    LL_GPIO_SetPinPull( GPIOA, LL_GPIO_PIN_5, LL_GPIO_PULL_NO );

    // PA6 for SPI MISO
    LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_6, LL_GPIO_MODE_OUTPUT );
    LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_6, LL_GPIO_MODE_ALTERNATE );
    LL_GPIO_SetAFPin_0_7( GPIOA, LL_GPIO_PIN_6, LL_GPIO_AF_5 );
    LL_GPIO_SetPinSpeed( GPIOA, LL_GPIO_PIN_6, LL_GPIO_SPEED_FREQ_VERY_HIGH );
    LL_GPIO_SetPinOutputType( GPIOA, LL_GPIO_PIN_6, LL_GPIO_OUTPUT_PUSHPULL );
    LL_GPIO_SetPinPull( GPIOA, LL_GPIO_PIN_6, LL_GPIO_PULL_NO );

    // PA7 for SPI MOSI
    LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_7, LL_GPIO_MODE_OUTPUT );
    LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_7, LL_GPIO_MODE_ALTERNATE );
    LL_GPIO_SetAFPin_0_7( GPIOA, LL_GPIO_PIN_7, LL_GPIO_AF_5 );
    LL_GPIO_SetPinSpeed( GPIOA, LL_GPIO_PIN_7, LL_GPIO_SPEED_FREQ_VERY_HIGH );
    LL_GPIO_SetPinOutputType( GPIOA, LL_GPIO_PIN_7, LL_GPIO_OUTPUT_PUSHPULL );
    LL_GPIO_SetPinPull( GPIOA, LL_GPIO_PIN_7, LL_GPIO_PULL_NO );

    // PA4 for Chip select
    LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_4, LL_GPIO_MODE_OUTPUT );
    LL_GPIO_SetPinSpeed( GPIOA, LL_GPIO_PIN_4, LL_GPIO_SPEED_FREQ_HIGH );
    LL_GPIO_SetPinOutputType( GPIOA, LL_GPIO_PIN_4, LL_GPIO_OUTPUT_PUSHPULL );
    LL_GPIO_SetPinPull( GPIOA, LL_GPIO_PIN_4, LL_GPIO_PULL_NO );
    LL_GPIO_SetOutputPin( GPIOA, LL_GPIO_PIN_4 );

    // SPI Setup
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);

    LL_SPI_SetTransferDirection( SPI1, LL_SPI_FULL_DUPLEX );
    LL_SPI_SetMode( SPI1, LL_SPI_MODE_MASTER );
    LL_SPI_SetDataWidth( SPI1, LL_SPI_DATAWIDTH_8BIT );
    LL_SPI_SetClockPolarity( SPI1, LL_SPI_POLARITY_LOW );
    LL_SPI_SetClockPhase( SPI1, LL_SPI_PHASE_1EDGE );
    LL_SPI_SetNSSMode( SPI1, LL_SPI_NSS_SOFT );
    LL_SPI_SetBaudRatePrescaler(SPI1, LL_SPI_BAUDRATEPRESCALER_DIV8 );
    LL_SPI_SetTransferBitOrder( SPI1, LL_SPI_MSB_FIRST );
    LL_SPI_DisableCRC( SPI1 );
    LL_SPI_SetCRCPolynomial( SPI1, 10 );
    LL_SPI_SetStandard( SPI1, LL_SPI_PROTOCOL_MOTOROLA );

    LL_SPI_Enable(SPI1);
}

/* -------------------------------------------------------------------------- */

void SysTick_Handler(void)
{

}

// Trigger IO IRQ
void EXTI0_IRQHandler(void)
{
    if(LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_0))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_0);
        trigger_pending = true;
    }
}

// SPI IRQ handling
void EXTI3_IRQHandler(void)
{
    if(LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_3))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_3);
        rfm95_on_interrupt( RFM95_INTERRUPT_DIO0 );
    }
}

void EXTI9_5_IRQHandler(void)
{
    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_8))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_8);
        rfm95_on_interrupt(RFM95_INTERRUPT_DIO1 );
    }

    if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_9))
    {
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_9);
        rfm95_on_interrupt( RFM95_INTERRUPT_DIO5 );
    }
}

/* -------------------------------------------------------------------------- */

void hal_core_init( void )
{
    LL_FLASH_EnableInstCache();
    LL_FLASH_EnableDataCache();
    LL_FLASH_EnablePrefetch();

    LL_APB2_GRP1_EnableClock( LL_APB2_GRP1_PERIPH_SYSCFG );
    LL_APB1_GRP1_EnableClock( LL_APB1_GRP1_PERIPH_PWR );

    NVIC_SetPriority( MemoryManagement_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( BusFault_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( UsageFault_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( SVCall_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( DebugMonitor_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( PendSV_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
    NVIC_SetPriority( SysTick_IRQn, NVIC_EncodePriority( NVIC_GetPriorityGrouping(), 0, 0 ) );
}


// Startup the internal and external clocks, set PLL etc
void hal_core_clock_configure( void )
{
    LL_FLASH_SetLatency( LL_FLASH_LATENCY_5 );

    if( LL_FLASH_GetLatency() != LL_FLASH_LATENCY_5 )
    {
        portAssertHandler("main.c", __LINE__, 0);
    }

    LL_PWR_SetRegulVoltageScaling( LL_PWR_REGU_VOLTAGE_SCALE1 );
    LL_PWR_DisableOverDriveMode();
    LL_RCC_HSE_EnableBypass();

    LL_RCC_HSE_Enable();
    while( LL_RCC_HSE_IsReady() != 1 )
    {
    }

//    LL_RCC_LSI_Enable();
//    while( LL_RCC_LSI_IsReady() != 1 )
//    {
//    }

    LL_RCC_PLL_ConfigDomain_SYS( LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_4, 168, LL_RCC_PLLP_DIV_2 );
    LL_RCC_PLL_ConfigDomain_48M( LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_4, 168, LL_RCC_PLLQ_DIV_7 );
    LL_RCC_PLL_Enable();
    while( LL_RCC_PLL_IsReady() != 1 )
    {
    }
    while( LL_PWR_IsActiveFlag_VOS() == 0 )
    {
    }

    LL_RCC_SetAHBPrescaler( LL_RCC_SYSCLK_DIV_1 );
    LL_RCC_SetAPB1Prescaler( LL_RCC_APB1_DIV_4 );
    LL_RCC_SetAPB2Prescaler( LL_RCC_APB2_DIV_2 );

    LL_RCC_SetSysClkSource( LL_RCC_SYS_CLKSOURCE_PLL );
    while( LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL )
    {
    }

    LL_Init1msTick( 168000000 );
    LL_SetSystemCoreClock( 168000000 );
//    LL_RCC_SetTIMPrescaler( LL_RCC_TIM_PRESCALER_TWICE );

    LL_SYSTICK_EnableIT();
}

void portAssertHandler( const char *file,
                        unsigned    line,
                        const char *fmt,
                        ... )
{
    va_list  args;

    // Forward directly to the 'in-memory cache' handler function
    va_start( args, fmt );
    // Read/handle file/line strings here
    va_end( args );

    // Wait for the watch dog to bite
    for( ;; )
    {
        asm("NOP");
    }

}

/* -------------------------------------------------------------------------- */