/* -------------------------------------------------------------------------- */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/gpio.h>

#include "benchmark_defs.h"

#if BLE_MODE == SERVER
    #include "central.h"
#else
    #include "peripheral.h"
#endif

/* -------------------------------------------------------------------------- */

// Stimulus IO
#define SW3_NODE	DT_ALIAS(sw3) 
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW3_NODE, gpios);

#define LED2_NODE	DT_ALIAS(led2)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED2_NODE, gpios);

/* -------------------------------------------------------------------------- */

#define CRC_SEED (0xFFFFu)
uint16_t bytes_read = 0;
uint16_t bytes_sent = 0;
uint16_t bytes_pending = 0;

uint16_t working_crc = CRC_SEED;
uint16_t payload_crc = 0x00;

/* -------------------------------------------------------------------------- */

// todo remove and use bench_event_t instead
typedef struct
{
	int id;
	char message[32];
} custom_message_t;


uint8_t bench_evt_buffer[BENCHMARK_QUEUE_SIZE * sizeof(bench_event_t)];
struct k_msgq bench_evt_queue;

/* -------------------------------------------------------------------------- */

#define LOG_MODULE_NAME nus_test
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

/* -------------------------------------------------------------------------- */

static struct gpio_callback gpio_cb;

volatile bool trigger_pending = false;

// This is the callback function that will be called when an IRQ is detected 
// for GPIO_PIN 25:
static void gpio_callback(const struct device *dev, struct gpio_callback *cb,
                          uint32_t pins) {
	trigger_pending = true;

}

static void configure_gpio(void)
{
	int ret;

	if (!device_is_ready(led.port)) {
		LOG_INF("Output IO not ready");
	}

	if (!device_is_ready(button.port)) {
		LOG_INF("Input IO not ready");
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_INF("Failed to configure Output IO");
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0)
	{
		LOG_INF("Failed to configure input IO");
	}
	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE );

    gpio_init_callback(&gpio_cb, gpio_callback, BIT(button.pin)); 	
	gpio_add_callback(button.port, &gpio_cb);
}

static void crc16(uint8_t data, uint16_t *crc)
{
    *crc  = (uint8_t)(*crc >> 8) | (*crc << 8);
    *crc ^= data;
    *crc ^= (uint8_t)(*crc & 0xff) >> 4;
    *crc ^= (*crc << 8) << 4;
    *crc ^= ((*crc & 0xff) << 4) << 1;
}

int main(void)
{
    k_msgq_init(&bench_evt_queue, bench_evt_buffer, sizeof(bench_event_t), BENCHMARK_QUEUE_SIZE);

	configure_gpio();

#if BLE_MODE == SERVER
	central_init();
    central_register_user_evt_queue(&bench_evt_queue);
#else
	peripheral_init();
    peripheral_register_user_evt_queue(&bench_evt_queue);
#endif

    // Work out the correct CRC for the active payload
    working_crc = CRC_SEED;
    for( uint16_t i = 0; i < sizeof(test_payload); i++ )
    {
        crc16( test_payload[i], &working_crc );
    }

    payload_crc = working_crc;
    working_crc = CRC_SEED;


    bench_event_t evt;
    uint16_t bytes_sent = 0;
    int msgq_res = 0;

	for (;;) {

 		if( trigger_pending )
        {
            // Chunk large payloads into smaller packets
            bytes_sent = 0;

            uint16_t bytes_to_send = sizeof(test_payload) - bytes_sent;
            if( bytes_to_send > BENCH_DATA_MAX_LEN )
            {
                bytes_to_send = BENCH_DATA_MAX_LEN;
            }
            
            LOG_INF( "Trig. Sending %iB", bytes_to_send );

#if BLE_MODE == SERVER
            central_send_payload( &test_payload[bytes_sent], bytes_to_send );
#else
            peripheral_send_payload( &test_payload[bytes_sent], bytes_to_send );
#endif
			bytes_pending = bytes_to_send;
            trigger_pending = false;
        }

        msgq_res = k_msgq_get( &bench_evt_queue, &evt, K_NO_WAIT );

		if( msgq_res == 0 )
		{
            // LOG_INF( "EVT[%i]", evt.id);

			switch( evt.id )
            {
                // Previously sent a packet
                case BENCH_SEND_CB:
                {
                    // bench_event_send_cb_t *send_cb = &evt.data.send_cb;

                    // LOG_INF( "At %i, Wrote %iB", bytes_sent, send_cb->bytes_sent);

                    // Update index of sent data (for multi-packet transfers)
                    bytes_sent += bytes_pending;
                    bytes_pending = 0;

                    // Send the next chunk if needed
                    if( bytes_sent < sizeof(test_payload) )
                    {
                        uint16_t bytes_to_send = sizeof(test_payload) - bytes_sent;
                        if( bytes_to_send > BENCH_DATA_MAX_LEN )
                        {
                            bytes_to_send = BENCH_DATA_MAX_LEN;
                        }

                        LOG_INF( "Cont. Sending %iB", bytes_to_send );

#if BLE_MODE == SERVER
                        central_send_payload( &test_payload[bytes_sent], bytes_to_send );
#else
                        peripheral_send_payload( &test_payload[bytes_sent], bytes_to_send );
#endif
                        bytes_pending = bytes_to_send;

                    }
                    else
                    {
                        bytes_sent = 0;
                        bytes_pending = 0;
                        // LOG_INF( "FIN \n");
                    }
  
                    break;
                } // end tx callback handling

                case BENCH_RECV_CB:
                {
                    // Destructure the callback into something more ergonomic
                    bench_event_recv_cb_t *recv_cb = &evt.data.recv_cb;

                    LOG_INF( "Got %iB", recv_cb->data_len);

                    for( uint16_t i = 0; i < recv_cb->data_len; i++ )
                    {
                        // Reset the "parser" if the start of a new test structure is seen
                        if(recv_cb->data[i] == 0x00 )
                        {
                            bytes_read = 0;
                            working_crc = CRC_SEED;
                            LOG_INF( "RESET\n");
                        }

                        // Running crc and byte count
                        crc16( recv_cb->data[i], &working_crc );
                        bytes_read++;

                        // Identify the end of the packet via expected length and correct CRC
                        if( bytes_read == sizeof(test_payload) && working_crc == payload_crc )
                        {
                            // Valid test structure
                            gpio_pin_set_dt(&led, true);
                        }
                    }

                    gpio_pin_set_dt(&led, false);
                    
                    // The rx callback uses malloc to store the inbound data
                    // so clean up after we're done handling that data
					free(recv_cb->data);

                    break;
                }   // end rx callback handling

                default:
                    LOG_INF( "Invalid callback type: %d", evt.id);
                    break;
            }
            
		}
	}
}
