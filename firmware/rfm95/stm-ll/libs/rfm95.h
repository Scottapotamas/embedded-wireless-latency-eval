#pragma once

#include <stdbool.h>

#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_spi.h"

#if defined (TESTING)
#include "testing/mock_hal.h"
#endif

#ifndef RFM95_SPI_TIMEOUT
#define RFM95_SPI_TIMEOUT 10
#endif

#ifndef RFM95_WAKEUP_TIMEOUT
#define RFM95_WAKEUP_TIMEOUT 10
#endif

#ifndef RFM95_SEND_TIMEOUT
#define RFM95_SEND_TIMEOUT 100
#endif

#ifndef RFM95_RECEIVE_TIMEOUT
#define RFM95_RECEIVE_TIMEOUT 1000
#endif

#define RFM95_EEPROM_CONFIG_MAGIC 0xab67

typedef struct {

	uint32_t frequency;

} rfm95_channel_config_t;

typedef struct {

	/**
	 * MAGIC
	 */
	uint16_t magic;

	/**
	 * The current RX frame counter value.
	 */
	uint16_t rx_frame_count;

	/**
	 * The current TX frame counter value.
	 */
	uint16_t tx_frame_count;

	/**
	 * The delay to the RX1 window.
	 */
	uint8_t rx1_delay;

	/**
	 * The configuration of channels;
	 */
	rfm95_channel_config_t channels[16];

	/**
	 * Mask defining which channels are configured.
	 */
	uint16_t channel_mask;

} rfm95_eeprom_config_t;

typedef void (*rfm95_on_after_interrupts_configured)();

typedef bool (*rfm95_load_eeprom_config)(rfm95_eeprom_config_t *config);
typedef void (*rfm95_save_eeprom_config)(const rfm95_eeprom_config_t *config);

typedef uint32_t (*rfm95_get_precision_tick)();
typedef void (*rfm95_precision_sleep_until)(uint32_t ticks_target);

typedef uint8_t (*rfm95_random_int)(uint8_t max);
typedef uint8_t (*rfm95_get_battery_level)();

typedef enum
{
	RFM95_INTERRUPT_DIO0,
	RFM95_INTERRUPT_DIO1,
	RFM95_INTERRUPT_DIO5
} rfm95_interrupt_t;

typedef enum
{
	RFM95_RECEIVE_MODE_NONE,
	RFM95_RECEIVE_MODE_RX1_ONLY,
	RFM95_RECEIVE_MODE_RX12,
} rfm95_receive_mode_t;

#define RFM95_INTERRUPT_COUNT 3

/**
 * Structure defining a handle describing an RFM95(W) transceiver.
 */
typedef struct {

	/**
	 * The handle to the SPI bus for the device.
	 */
    SPI_TypeDef *spi_handle;

	/**
	 * The port of the NSS pin.
	 */
    GPIO_TypeDef *nss_port;

	/**
	 * The NSS pin.
	 */
	uint16_t nss_pin;

	/**
	 * The port of the RST pin.
	 */
	GPIO_TypeDef *nrst_port;

	/**
	 * The RST pin.
	 */
	uint16_t nrst_pin;

	/**
	 * The device address for the LoraWAN
	 */
	uint8_t device_address[4];

	/**
	 * The network session key for ABP activation with the LoraWAN
	 */
	uint8_t network_session_key[16];

	/**
	 * The application session key for ABP activation with the LoraWAN
	 */
	uint8_t application_session_key[16];

	/**
	 * The frequency of the precision tick in Hz.
	 */
	uint32_t precision_tick_frequency;

	/**
	 * The +/- timing drift per second in nanoseconds.
	 */
	uint32_t precision_tick_drift_ns_per_s;

	/**
	 * The receive mode to operate at.
	 */
	rfm95_receive_mode_t receive_mode;

	/**
	 * Function provided that returns a precise tick for timing critical operations.
	 */
	rfm95_get_precision_tick get_precision_tick;

	/**
	 * Function that provides a precise sleep until a given tick count is reached.
	 */
	rfm95_precision_sleep_until precision_sleep_until;

	/**
	 * Function that provides a random integer.
	 */
	rfm95_random_int random_int;

	/**
	 * Function that returns the device's battery level.
	 */
	rfm95_get_battery_level get_battery_level;

	/**
	 * The load config function pointer can be used to load config values from non-volatile memory.
	 * Can be set to NULL to skip.
	 */
	rfm95_load_eeprom_config reload_config;

	/**
	 * The save config function pointer can be used to store config values in non-volatile memory.
	 * Can be set to NULL to skip.
	 */
	rfm95_save_eeprom_config save_config;

	/**
	 * Callback called after the interrupt functions have been properly configred;
	 */
	rfm95_on_after_interrupts_configured on_after_interrupts_configured;

	/**
	 * The config saved into the eeprom.
	 */
	rfm95_eeprom_config_t config;

	/**
	 * Tick values when each interrupt was called.
	 */
	volatile uint32_t interrupt_times[RFM95_INTERRUPT_COUNT];

} rfm95_handle_t;

bool rfm95_init(rfm95_handle_t *handle);

bool rfm95_set_power(rfm95_handle_t *handle, int8_t power);

bool rfm95_send_receive_cycle(rfm95_handle_t *handle, const uint8_t *send_data, uint32_t send_data_length);

void rfm95_on_interrupt(rfm95_handle_t *handle, rfm95_interrupt_t interrupt);
