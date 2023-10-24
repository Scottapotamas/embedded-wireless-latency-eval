#include <string.h>

#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include <stm32f4xx_ll_rcc.h>
#include <stm32f4xx_ll_bus.h>

#include "uart.h"
#include "fifo.h"

#define UART5_BAUD (115200)

#define HAL_UART_TX_FIFO_SIZE 128
#define HAL_UART_RX_FIFO_SIZE 128
#define HAL_UART_RX_DMA_BUFFER_SIZE 32

// User-space buffers are serviced outside IRQ
fifo_t   tx_fifo = { 0 };
uint8_t  tx_buffer[HAL_UART_TX_FIFO_SIZE];
uint16_t tx_sneak_bytes;

fifo_t  rx_fifo = { 0 };
uint8_t rx_buffer[HAL_UART_RX_FIFO_SIZE];

#ifdef UART_DMA
// Raw DMA buffer,
volatile uint8_t dma_rx_buffer[HAL_UART_RX_DMA_BUFFER_SIZE];
uint32_t dma_rx_pos = 0;
#endif

static void hal_uart_start_tx( void );
static void hal_usart_rx_handler( void );

// This function is responsible for setting up the UART5 in any selected operating mode
void uart_init( void )
{
    // Cleanup periperhal, Disable DMA streams
    LL_USART_DeInit( UART5 );
    LL_DMA_DeInit( DMA1, LL_DMA_STREAM_0 );
    LL_DMA_DeInit( DMA1, LL_DMA_STREAM_7 );

    // Prepare buffers
    memset( tx_buffer, 0, sizeof(tx_buffer) );
    memset( rx_buffer, 0, sizeof(rx_fifo) );
#ifdef UART_DMA
    memset( (uint8_t*)dma_rx_buffer, 0, sizeof(dma_rx_buffer) );
#endif
    fifo_init( &tx_fifo, &tx_buffer[0], HAL_UART_TX_FIFO_SIZE );
    fifo_init( &rx_fifo, &rx_buffer[0], HAL_UART_RX_FIFO_SIZE );

    // IO is common across implementations
    // PD2 RX
    LL_AHB1_GRP1_EnableClock( LL_AHB1_GRP1_PERIPH_GPIOD );
    LL_GPIO_SetPinMode( GPIOD, LL_GPIO_PIN_2, LL_GPIO_MODE_ALTERNATE );
    LL_GPIO_SetPinSpeed( GPIOD, LL_GPIO_PIN_2, LL_GPIO_SPEED_FREQ_HIGH );
    LL_GPIO_SetPinPull( GPIOD, LL_GPIO_PIN_2, LL_GPIO_PULL_NO );
    LL_GPIO_SetAFPin_0_7( GPIOD, LL_GPIO_PIN_2, LL_GPIO_AF_8 );

    // PC12 TX
    LL_AHB1_GRP1_EnableClock( LL_AHB1_GRP1_PERIPH_GPIOC );
    LL_GPIO_SetPinMode( GPIOC, LL_GPIO_PIN_12, LL_GPIO_MODE_ALTERNATE );
    LL_GPIO_SetPinSpeed( GPIOC, LL_GPIO_PIN_12, LL_GPIO_SPEED_FREQ_HIGH );
    LL_GPIO_SetPinPull( GPIOC, LL_GPIO_PIN_12, LL_GPIO_PULL_NO );
    LL_GPIO_SetAFPin_8_15( GPIOC, LL_GPIO_PIN_12, LL_GPIO_AF_8 );


#ifdef UART_DMA
    LL_AHB1_GRP1_EnableClock( LL_AHB1_GRP1_PERIPH_DMA1 );

    // UART5 uses:
    //      dma_peripheral = DMA1;
    //      dma_stream_tx  = LL_DMA_STREAM_7;
    //      dma_channel_tx = LL_DMA_CHANNEL_4;
    //      dma_stream_rx  = LL_DMA_STREAM_0;
    //      dma_channel_rx = LL_DMA_CHANNEL_4;

    /* TX Init */
    LL_DMA_SetChannelSelection( DMA1, LL_DMA_STREAM_7, LL_DMA_CHANNEL_4 );
    LL_DMA_SetDataTransferDirection( DMA1, LL_DMA_STREAM_7, LL_DMA_DIRECTION_MEMORY_TO_PERIPH );
    LL_DMA_SetStreamPriorityLevel( DMA1, LL_DMA_STREAM_7, LL_DMA_PRIORITY_LOW );
    LL_DMA_SetMode( DMA1, LL_DMA_STREAM_7, LL_DMA_MODE_NORMAL );
    LL_DMA_SetPeriphIncMode( DMA1, LL_DMA_STREAM_7, LL_DMA_PERIPH_NOINCREMENT );
    LL_DMA_SetMemoryIncMode( DMA1, LL_DMA_STREAM_7, LL_DMA_MEMORY_INCREMENT );
    LL_DMA_SetPeriphSize( DMA1, LL_DMA_STREAM_7, LL_DMA_PDATAALIGN_BYTE );
    LL_DMA_SetMemorySize( DMA1, LL_DMA_STREAM_7, LL_DMA_MDATAALIGN_BYTE );
    LL_DMA_DisableFifoMode( DMA1, LL_DMA_STREAM_7 );

    LL_DMA_SetPeriphAddress( DMA1, LL_DMA_STREAM_7, (uint32_t)&UART5->DR );
    LL_DMA_EnableIT_TC( DMA1, LL_DMA_STREAM_7 ); /* Enable TX TC interrupt */

    NVIC_SetPriority( DMA1_Stream7_IRQn, NVIC_EncodePriority(
            NVIC_GetPriorityGrouping(),
            1,
            0 )
    );
    NVIC_EnableIRQ( DMA1_Stream7_IRQn );

    /* RX Init */
    LL_DMA_SetChannelSelection( DMA1, LL_DMA_STREAM_0, LL_DMA_CHANNEL_4 );
    LL_DMA_SetDataTransferDirection( DMA1, LL_DMA_STREAM_0, LL_DMA_DIRECTION_PERIPH_TO_MEMORY );
    LL_DMA_SetStreamPriorityLevel( DMA1, LL_DMA_STREAM_0, LL_DMA_PRIORITY_LOW );
    LL_DMA_SetMode( DMA1, LL_DMA_STREAM_0, LL_DMA_MODE_CIRCULAR );
    LL_DMA_SetPeriphIncMode( DMA1, LL_DMA_STREAM_0, LL_DMA_PERIPH_NOINCREMENT );
    LL_DMA_SetMemoryIncMode( DMA1, LL_DMA_STREAM_0, LL_DMA_MEMORY_INCREMENT );
    LL_DMA_SetPeriphSize( DMA1, LL_DMA_STREAM_0, LL_DMA_PDATAALIGN_BYTE );
    LL_DMA_SetMemorySize( DMA1, LL_DMA_STREAM_0, LL_DMA_MDATAALIGN_BYTE );
    LL_DMA_DisableFifoMode( DMA1, LL_DMA_STREAM_0 );

    LL_DMA_SetPeriphAddress( DMA1, LL_DMA_STREAM_0, (uint32_t)&UART5->DR );
    LL_DMA_SetMemoryAddress( DMA1, LL_DMA_STREAM_0, (uint32_t)&dma_rx_buffer );
    LL_DMA_SetDataLength( DMA1, LL_DMA_STREAM_0, HAL_UART_RX_DMA_BUFFER_SIZE );

    /* Enable HT & TC interrupts */
    LL_DMA_EnableIT_HT( DMA1, LL_DMA_STREAM_0 );
    LL_DMA_EnableIT_TC( DMA1, LL_DMA_STREAM_0 );

    NVIC_SetPriority( DMA1_Stream0_IRQn, 2 );
    NVIC_EnableIRQ( DMA1_Stream0_IRQn );
#endif

    // Common UART config
    LL_APB1_GRP1_EnableClock( LL_APB1_GRP1_PERIPH_UART5 );

    LL_RCC_ClocksTypeDef rcc_clocks = { 0 };
    uint32_t             periphclk  = 0;
    LL_RCC_GetSystemClocksFreq( &rcc_clocks );

    // UART5 is on PCLK1
    periphclk = rcc_clocks.PCLK1_Frequency;
    LL_USART_SetBaudRate( UART5, periphclk, LL_USART_OVERSAMPLING_16, UART5_BAUD );
    LL_USART_SetDataWidth( UART5, LL_USART_DATAWIDTH_8B );
    LL_USART_SetStopBitsLength( UART5, LL_USART_STOPBITS_1 );
    LL_USART_SetParity( UART5, LL_USART_PARITY_NONE );
    LL_USART_SetTransferDirection( UART5, LL_USART_DIRECTION_TX_RX );
    LL_USART_SetHWFlowCtrl( UART5, LL_USART_HWCONTROL_NONE );
    LL_USART_SetOverSampling( UART5, LL_USART_OVERSAMPLING_16 );
    LL_USART_ConfigAsyncMode( UART5 );

    // USART interrupt priorities
    NVIC_SetPriority( UART5_IRQn, 3 );
    NVIC_EnableIRQ( UART5_IRQn );

#ifdef UART_POLL
    LL_USART_Enable( UART5 );

#endif

#ifdef UART_IRQ
    LL_USART_EnableIT_TXE(UART5);
    LL_USART_EnableIT_RXNE(UART5);

    LL_USART_Enable( UART5 );
#endif

#ifdef UART_DMA
    LL_USART_EnableDMAReq_TX( UART5 );
    LL_USART_EnableDMAReq_RX( UART5 );
    LL_USART_EnableIT_IDLE( UART5 );

    LL_USART_Enable( UART5 );
    LL_DMA_EnableStream( DMA1, LL_DMA_STREAM_0 );    // rx stream
#endif

    // Manually transmit a byte
//    LL_USART_TransmitData9(UART5, 0xAA);

}

/* -------------------------------------------------------------------------- */

uint32_t hal_uart_write( const uint8_t *data, uint32_t length )
{
    uint32_t sent = 0;

    if( fifo_free( &tx_fifo ) >= length )
    {
        sent = fifo_write( &tx_fifo, data, length );
        hal_uart_start_tx();
    }

    return sent;
}

/* -------------------------------------------------------------------------- */

uint32_t hal_uart_rx_data_available( void )
{
#ifdef UART_POLL
    hal_usart_rx_handler();
#endif
    return fifo_used( &rx_fifo );
}

/* -------------------------------------------------------------------------- */

uint8_t hal_uart_rx_get( void )
{
    uint8_t c = 0;
    fifo_read( &rx_fifo, &c, 1 );
    return c;
}

/* -------------------------------------------------------------------------- */

uint32_t hal_uart_read( uint8_t *data, uint32_t maxlength )
{
    uint32_t   len;
    len = fifo_read( &rx_fifo, data, maxlength );
    return len;
}

/* ------------------------------------------------------------------*/

static void hal_uart_start_tx( void )
{
    uint32_t primask;
    primask = __get_PRIMASK();
    __disable_irq();

#ifdef UART_POLL
    uint8_t byte = 0;

    while( fifo_read( &tx_fifo, &byte, 1 ) )
    {
        // Send it
        LL_USART_TransmitData9(UART5, byte);

        // Poll until it's complete - this is blocking behaviour
        while( !LL_USART_IsActiveFlag_TXE(UART5) )
        {
            hal_usart_rx_handler();
        }
    }
#endif

#ifdef UART_IRQ
    uint8_t byte = 0;

    if( fifo_read( &tx_fifo, &byte, 1 ) )
    {
        // Send it
        LL_USART_TransmitData9(UART5, byte);
        LL_USART_EnableIT_TXE(UART5);
    }
#endif

#ifdef UART_DMA
    /* If transfer is not ongoing */
    if( !LL_DMA_IsEnabledStream( DMA1, LL_DMA_STREAM_7 ) )
    {
        // Accept up to the max DMA buffer's capacity from the stream
        // StreamBuffer -> buffer for DMA -> UART TX
        tx_sneak_bytes = fifo_used_linear( &tx_fifo );

        // Limit maximum size to transmit at a time
        if( tx_sneak_bytes > 32 )
        {
            tx_sneak_bytes = 32;
        }

        // Configure DMA with the data
        if( tx_sneak_bytes > 0 )
        {
            void *ptr = fifo_get_tail_ptr( &tx_fifo, tx_sneak_bytes );

            LL_DMA_SetDataLength( DMA1, LL_DMA_STREAM_7, tx_sneak_bytes );
            LL_DMA_SetMemoryAddress( DMA1, LL_DMA_STREAM_7, (uint32_t)ptr );

            LL_DMA_ClearFlag_TC7( DMA1 );
            LL_DMA_ClearFlag_HT7( DMA1 );
            LL_DMA_ClearFlag_DME7( DMA1 );
            LL_DMA_ClearFlag_FE7( DMA1 );
            LL_DMA_ClearFlag_TE7( DMA1 );

            // Start transfer
            LL_DMA_EnableStream( DMA1, LL_DMA_STREAM_7 );
        }
    }
#endif

    __set_PRIMASK( primask );
}

/* ------------------------------------------------------------------*/

static void hal_usart_rx_handler( void )
{
#ifdef UART_POLL
    if( LL_USART_IsActiveFlag_RXNE(UART5) )
    {
        LL_USART_ClearFlag_RXNE(UART5);
        uint8_t rx_byte = (uint8_t)LL_USART_ReceiveData9(UART5);
        fifo_put(&rx_fifo, rx_byte);
    }
#endif

#ifdef UART_IRQ
    uint8_t rx_byte = (uint8_t)LL_USART_ReceiveData9(UART5);
    fifo_put(&rx_fifo, rx_byte);
#endif

#ifdef UART_DMA
    // Tracks data handled by RX DMA and passes data off for higher-level storage/parsing etc.
    // Called when the RX DMA interrupts for half or full buffer fire, and when line-idle occurs

    // Calculate current head index
    uint32_t current_pos = HAL_UART_RX_DMA_BUFFER_SIZE - LL_DMA_GetDataLength( DMA1, LL_DMA_STREAM_0 );

    // Has DMA given us new data?
    if( current_pos != dma_rx_pos )
    {
        // Data hasn't hit the end yet
        if( current_pos > dma_rx_pos )
        {
            fifo_write( &rx_fifo, (const uint8_t *)&dma_rx_buffer[dma_rx_pos], current_pos - dma_rx_pos );
        }
        else    // circular buffer overflowed
        {
            // Read to the end of the buffer
            fifo_write( &rx_fifo, (const uint8_t *)&dma_rx_buffer[dma_rx_pos], sizeof(dma_rx_buffer) - dma_rx_pos );

            // Read from the start of the buffer to the current head
            if( current_pos > 0 )
            {
                fifo_write( &rx_fifo, (const uint8_t *)&dma_rx_buffer[0], current_pos );
            }
        }
    }
    // Remember the current head position
    dma_rx_pos = current_pos;

    // Check if we've reached the end of the buffer, move the head to the start
    if( dma_rx_pos == HAL_UART_RX_DMA_BUFFER_SIZE )
    {
        dma_rx_pos = 0;
    }
#endif
}

/* ------------------------------------------------------------------*/

void UART5_IRQHandler( void )
{
#ifdef UART_IRQ
    // Check tx empty flag
    if(LL_USART_IsEnabledIT_TXE(UART5) && LL_USART_IsActiveFlag_TXE(UART5) )
    {
        LL_USART_ClearFlag_TC(UART5);
        LL_USART_DisableIT_TXE(UART5);

        // Check for more data to send
        hal_uart_start_tx();
    }

    if(LL_USART_IsEnabledIT_RXNE(UART5) && LL_USART_IsActiveFlag_RXNE(UART5) )
    {
        LL_USART_ClearFlag_RXNE(UART5);
        hal_usart_rx_handler();
    }
#endif

    // Idle line interrupt occurs when the UART RX line has been high for more than one frame
    if( LL_USART_IsEnabledIT_IDLE( UART5 ) && LL_USART_IsActiveFlag_IDLE( UART5 ) )
    {
        // Clear IDLE line flag
        LL_USART_ClearFlag_IDLE( UART5 );

        // Check for data to process
        hal_usart_rx_handler();
    }
}

#ifdef UART_DMA
// RX
void DMA1_Stream0_IRQHandler( void )
{
    // Half transfer complete
    if( LL_DMA_IsEnabledIT_HT( DMA1, LL_DMA_STREAM_0 ) && LL_DMA_IsActiveFlag_HT0( DMA1 ) )
    {
        LL_DMA_ClearFlag_HT0( DMA1 );
        hal_usart_irq_rx_handler();
    }

    // Full transfer complete
    if( LL_DMA_IsEnabledIT_TC( DMA1, LL_DMA_STREAM_0 ) && LL_DMA_IsActiveFlag_TC0( DMA1 ) )
    {
        LL_DMA_ClearFlag_TC0( DMA1 );
        hal_usart_irq_rx_handler();
    }
}

// TX
void DMA1_Stream7_IRQHandler( void )
{
    // Transfer complete
    if( LL_DMA_IsEnabledIT_TC( DMA1, LL_DMA_STREAM_7 ) && LL_DMA_IsActiveFlag_TC7( DMA1 ) )
    {
        LL_DMA_ClearFlag_TC7( DMA1 );    // Clear transfer complete flag

        // Flush the data that completed, and send more if needed
        fifo_skip( &tx_fifo, tx_sneak_bytes );
        hal_uart_start_tx();
    }
}
#endif

/* ------------------------------------------------------------------*/