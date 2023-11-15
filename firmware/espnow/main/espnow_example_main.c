#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"

#include <stdio.h>
#include <inttypes.h>
#include "freertos/task.h"
#include "driver/gpio.h"

/* -------------------------------------------------------------------------- */


#define PAYLOAD_12B
// #define PAYLOAD_128B
// #define PAYLOAD_1024B


#define ESPNOW_QUEUE_SIZE           6

#define IS_BROADCAST_ADDR(addr) (memcmp(addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN) == 0)

typedef enum {
    EXAMPLE_ESPNOW_SEND_CB,
    EXAMPLE_ESPNOW_RECV_CB,
} example_espnow_event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} example_espnow_event_send_cb_t;

typedef struct {
    uint8_t src_addr[ESP_NOW_ETH_ALEN];
    uint8_t dst_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} example_espnow_event_recv_cb_t;

typedef union {
    example_espnow_event_send_cb_t send_cb;
    example_espnow_event_recv_cb_t recv_cb;
} example_espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct {
    example_espnow_event_id_t id;
    example_espnow_event_info_t info;
} example_espnow_event_t;

enum {
    EXAMPLE_ESPNOW_DATA_BROADCAST,
    EXAMPLE_ESPNOW_DATA_UNICAST,
    EXAMPLE_ESPNOW_DATA_MAX,
};

/* User defined field of ESPNOW data in this example. */
typedef struct {
    uint8_t payload[0];                   // Real payload of ESPNOW data.
} __attribute__((packed)) example_espnow_data_t;

/* Parameters of sending ESPNOW data. */
typedef struct {
    int len;                              // Length of ESPNOW data to be sent, unit: byte.
    uint8_t *buffer;                      // Buffer pointing to ESPNOW data.
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];   // MAC address of destination device.
} example_espnow_send_param_t;

// TODO: cleanup
/* -------------------------------------------------------------------------- */



// Test stimulus input pin
#define GPIO_INPUT_IO_0     19
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0))

// Status output pin
#define GPIO_OUTPUT_IO_0    18
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0))

#define ESP_INTR_FLAG_DEFAULT 0

/* -------------------------------------------------------------------------- */

static const char *TAG = "espnow_example";

static QueueHandle_t s_example_espnow_queue;

static uint8_t s_example_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* -------------------------------------------------------------------------- */

static void setup_gpio_output( void );
static void setup_gpio_input( void );
static void IRAM_ATTR gpio_isr_handler(void* arg);

static void crc16(uint8_t data, uint16_t *crc);

static void wifi_init(void);
static esp_err_t espnow_init(void);
static void example_espnow_deinit(example_espnow_send_param_t *send_param);

static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);

static void broadcast_for_peers( example_espnow_send_param_t *send_param );

static void example_espnow_task(void *pvParameter);

/* -------------------------------------------------------------------------- */

volatile bool trigger_pending = false;

#define CRC_SEED (0xFFFFu)
uint16_t bytes_read = 0;
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

void setup_gpio_output( void )
{
    gpio_config_t io_conf = {};

    // Output pin
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
}

/* -------------------------------------------------------------------------- */

void setup_gpio_input( void )
{
    gpio_config_t io_conf = {};

    // Input pin with rising edge interrupt
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // ISR setup
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    trigger_pending = true;
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

static void wifi_init(void)
{
    ESP_ERROR_CHECK( esp_netif_init() );
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    ESP_ERROR_CHECK( esp_wifi_start());
    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK( esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
#endif
}

/* -------------------------------------------------------------------------- */

static esp_err_t espnow_init(void)
{
    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(example_espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(example_espnow_recv_cb) );

#if CONFIG_ESPNOW_ENABLE_POWER_SAVE
    ESP_ERROR_CHECK( esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW) );
    ESP_ERROR_CHECK( esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL) );
#endif

    /* Set primary master key. */
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    // Add broadcast peer information to peer list.
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESP_IF_WIFI_AP;
    peer->encrypt = false;
    memcpy(peer->peer_addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    // Initialize sending parameters
    example_espnow_send_param_t *send_param = malloc(sizeof(example_espnow_send_param_t));
    if(send_param == NULL)
    {
        ESP_LOGE(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }

    memset(send_param, 0, sizeof(example_espnow_send_param_t));
    send_param->len = CONFIG_ESPNOW_SEND_LEN;
    send_param->buffer = malloc(CONFIG_ESPNOW_SEND_LEN);
    if (send_param->buffer == NULL) {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        free(send_param);
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }

    xTaskCreate(example_espnow_task, "example_espnow_task", 2048, send_param, 4, NULL);

    return ESP_OK;
}

static void example_espnow_deinit(example_espnow_send_param_t *send_param)
{
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(s_example_espnow_queue);
    esp_now_deinit();
}

/* -------------------------------------------------------------------------- */

// Tx callback
static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    example_espnow_event_t evt;
    example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL)
    {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    
    if(xQueueSend(s_example_espnow_queue, &evt, 128) != pdTRUE)
    {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

/* -------------------------------------------------------------------------- */

// Rx callback
static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    example_espnow_event_t evt;
    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t *mac_addr = recv_info->src_addr;
    uint8_t *des_addr = recv_info->des_addr;

    if( mac_addr == NULL || data == NULL || len <= 0 )
    {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    // Format the event object
    evt.id = EXAMPLE_ESPNOW_RECV_CB;

    // Keep track of the mac addresses in the event
    memcpy( recv_cb->src_addr, mac_addr, ESP_NOW_ETH_ALEN );
    memcpy( recv_cb->dst_addr, des_addr, ESP_NOW_ETH_ALEN );

    // Allocate a sufficiently large chunk of memory and copy the payload into it
    recv_cb->data = malloc(len);
    if( recv_cb->data == NULL )
    {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;

    // Put the event into the queue for processing
    if( xQueueSend(s_example_espnow_queue, &evt, 512) != pdTRUE )
    {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* -------------------------------------------------------------------------- */

// Sends some broadcast packets with a small delay
static void broadcast_for_peers( example_espnow_send_param_t *send_param )
{
    for( uint8_t i = 0; i < 4; i++ )
    {
        vTaskDelay(250 / portTICK_PERIOD_MS);

        // Send a packet using the broadcast address and dummy payload
        memcpy(send_param->dest_mac, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
        memset(send_param->buffer, 0xAA, 10);
        send_param->len = 10;
        if( esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK )
        {
            ESP_LOGE(TAG, "Send error");
            example_espnow_deinit(send_param);
            vTaskDelete(NULL);
        }
    }
}

static void example_espnow_task(void *pvParameter)
{
    example_espnow_send_param_t *send_param = (example_espnow_send_param_t *)pvParameter;
    example_espnow_event_t evt;
    bool is_broadcast = false;
    int ret;

    // Spend some time broadcasting so the other ESP32 can add us to it's peer list
    broadcast_for_peers( send_param );

    // Main event loop
    while(1)
    {
        if(trigger_pending)
        {
            // Get the candidate device we want to send to
            esp_now_peer_info_t *peer = malloc( sizeof(esp_now_peer_info_t) );
            if (peer == NULL)
            {
                ESP_LOGE(TAG, "Malloc peer information fail");
                example_espnow_deinit(send_param);
                vTaskDelete(NULL);
            }

            memset(peer, 0, sizeof(esp_now_peer_info_t));
            if( esp_now_fetch_peer( true, peer) == ESP_OK )
            {
                // ESP_LOGI(TAG, "Start sending unicast data to "MACSTR"", MAC2STR(peer->peer_addr));

                // Send an unicast ESPNOW packet to the peer we met during startup
                memcpy(send_param->dest_mac, peer->peer_addr, ESP_NOW_ETH_ALEN);

                // Copy our test data into the packet
                memcpy( send_param->buffer, test_payload, sizeof(test_payload) );
                send_param->len = sizeof(test_payload);

                if( esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK )
                {
                    ESP_LOGE(TAG, "Send error");
                    example_espnow_deinit(send_param);
                    vTaskDelete(NULL);
                }
            }

            // Cleanup
            free(peer);
            trigger_pending = false;
        }

        // Handle events from the espnow callbacks
        if( xQueueReceive(s_example_espnow_queue, &evt, 1) )
        {
            switch (evt.id)
            {
                // Sent a packet
                case EXAMPLE_ESPNOW_SEND_CB:
                {
                    example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

                    if( IS_BROADCAST_ADDR(send_cb->mac_addr) )
                    {
                        //ESP_LOGD(TAG, "Sent broadcast on "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);
                        break;
                    }

                    // TODO: send the rest of the chunks if dealing with large packets?
                    ESP_LOGD(TAG, "Sent to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);


                    break;
                } // end tx callback handling

                case EXAMPLE_ESPNOW_RECV_CB:
                {
                    // Destructure the callback into something more ergonomic
                    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

                    if( IS_BROADCAST_ADDR(recv_cb->dst_addr) )
                    {
                        ESP_LOGI(TAG, "Receive broadcast data from: "MACSTR", len: %d", MAC2STR(recv_cb->src_addr), recv_cb->data_len);

                        // Add new MAC addresses to the peer list
                        if( !esp_now_is_peer_exist(recv_cb->src_addr) )
                        {
                            ESP_LOGI(TAG, "  Adding it as a peer");

                            esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                            if (peer == NULL)
                            {
                                ESP_LOGE(TAG, "Malloc peer information fail");
                                example_espnow_deinit(send_param);
                                vTaskDelete(NULL);
                            }
                            memset(peer, 0, sizeof(esp_now_peer_info_t));
                            peer->channel = CONFIG_ESPNOW_CHANNEL;
                            peer->ifidx = ESP_IF_WIFI_AP;
                            peer->encrypt = true;
                            memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                            memcpy(peer->peer_addr, recv_cb->src_addr, ESP_NOW_ETH_ALEN);

                            ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                            free(peer);
                        }
                    }
                    else    // unicast packet
                    {
                        // ESP_LOGI(TAG, "Receive unicast data from: "MACSTR", len: %d", MAC2STR(recv_cb->src_addr), recv_cb->data_len);

                        for( uint8_t i = 0; i < recv_cb->data_len; i++ )
                        {
                            // ESP_LOGI(TAG, "%d: %d", i, recv_cb->data[i]);

                            // Reset the "parser" if the start of a new test structure is seen
                            if(recv_cb->data[i] == 0x00 )
                            {
                                bytes_read = 0;
                                working_crc = CRC_SEED;
                            }

                            // Running crc and byte count
                            crc16( recv_cb->data[i], &working_crc );
                            bytes_read++;

                            // Identify the end of the packet via expected length and correct CRC
                            if( bytes_read == sizeof(test_payload) && working_crc == payload_crc )
                            {
                                // Valid test structure
                                gpio_set_level( GPIO_OUTPUT_IO_0, 1 );
                                ESP_LOGI(TAG, "--- PACKET VALID ---");

                            }
                        }

                        gpio_set_level( GPIO_OUTPUT_IO_0, 0 );
                    }

                    // The rx callback uses malloc to store the inbound data
                    // so clean up after we're done handling that data
                    free( recv_cb->data );

                    break;
                }   // end rx callback handling

                default:
                    ESP_LOGE(TAG, "Invalid callback type: %d", evt.id);
                    break;
            }
        }   // end evtxQueueReceive

    }   // end event loop
}

/* -------------------------------------------------------------------------- */

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // General setup
    setup_gpio_output();
    setup_gpio_input();

    // Work out the correct CRC for the active payload
    working_crc = CRC_SEED;
    for( uint16_t i = 0; i < sizeof(test_payload); i++ )
    {
        crc16( test_payload[i], &working_crc );
    }

    payload_crc = working_crc;
    working_crc = CRC_SEED;

    // Start wifi/espnow tasks
    wifi_init();
    espnow_init();
}

/* -------------------------------------------------------------------------- */