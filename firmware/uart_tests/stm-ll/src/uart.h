#ifndef UART_H
#define UART_H

void uart_init( void );

/* Non-blocking send for a number of characters to the UART tx FIFO queue.
 * Returns true when successful. false when queue was full.
 */
uint32_t hal_uart_write( const uint8_t *data, uint32_t length );

/* -------------------------------------------------------------------------- */

/* Returns number of available characters in the RX FIFO queue. */
uint32_t hal_uart_rx_data_available( void );

/* -------------------------------------------------------------------------- */

/* Retrieve a single byte from the rx FIFO queue.
 * Returns 0 when no data is available or when rx callback is in use.
 */
uint8_t hal_uart_rx_get( void );

/* -------------------------------------------------------------------------- */

/* Retrieve a number of bytes from the rx FIFO queue up to
 * buffer length. Returns the number of bytes actually read.
 */
uint32_t hal_uart_read( uint8_t *buffer, uint32_t bufferlen );

/* -------------------------------------------------------------------------- */

#endif //UART_H
