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
#include "espnow_example.h"

#include <stdio.h>
#include <inttypes.h>
#include "freertos/task.h"
#include "driver/gpio.h"

/* -------------------------------------------------------------------------- */

// Test stimulus input pin
#define GPIO_INPUT_IO_0     19
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0))

// Status output pin
#define GPIO_OUTPUT_IO_0    18
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0))

#define ESP_INTR_FLAG_DEFAULT 0

/* -------------------------------------------------------------------------- */

#define ESPNOW_MAXDELAY 512

static const char *TAG = "espnow_example";

static QueueHandle_t s_example_espnow_queue;

static uint8_t s_example_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint16_t s_example_espnow_seq[EXAMPLE_ESPNOW_DATA_MAX] = { 0, 0 };


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

static void example_espnow_task(void *pvParameter);
void example_espnow_data_prepare(example_espnow_send_param_t *send_param);

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
    example_espnow_send_param_t *send_param;

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

    /* Add broadcast peer information to peer list. */
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

    /* Initialize sending parameters. */
    send_param = malloc(sizeof(example_espnow_send_param_t));
    if (send_param == NULL) {
        ESP_LOGE(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(send_param, 0, sizeof(example_espnow_send_param_t));
    send_param->unicast = false;
    send_param->broadcast = true;
    send_param->state = 0;
    send_param->magic = esp_random();
    send_param->count = CONFIG_ESPNOW_SEND_COUNT;
    send_param->delay = CONFIG_ESPNOW_SEND_DELAY;
    send_param->len = CONFIG_ESPNOW_SEND_LEN;
    send_param->buffer = malloc(CONFIG_ESPNOW_SEND_LEN);
    if (send_param->buffer == NULL) {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        free(send_param);
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memcpy(send_param->dest_mac, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
    example_espnow_data_prepare(send_param);

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

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    
    if(xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE)
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
    uint8_t * mac_addr = recv_info->src_addr;
    uint8_t * des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    if (IS_BROADCAST_ADDR(des_addr)) {
        /* If added a peer with encryption before, the receive packets may be
         * encrypted as peer-to-peer message or unencrypted over the broadcast channel.
         * Users can check the destination address to distinguish it.
         */
        ESP_LOGD(TAG, "Receive broadcast ESPNOW data");
    } else {
        ESP_LOGD(TAG, "Receive unicast ESPNOW data");
    }

    evt.id = EXAMPLE_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);

    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;

    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* -------------------------------------------------------------------------- */


/* Parse received ESPNOW data. */
int example_espnow_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state, uint16_t *seq, int *magic)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(example_espnow_data_t))
    {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return buf->type;
    }

    return -1;
}

/* Prepare ESPNOW data to be sent. */
void example_espnow_data_prepare(example_espnow_send_param_t *send_param)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)send_param->buffer;

    assert(send_param->len >= sizeof(example_espnow_data_t));

    buf->type = IS_BROADCAST_ADDR(send_param->dest_mac) ? EXAMPLE_ESPNOW_DATA_BROADCAST : EXAMPLE_ESPNOW_DATA_UNICAST;
    buf->state = send_param->state;
    buf->seq_num = s_example_espnow_seq[buf->type]++;
    buf->crc = 0;
    buf->magic = send_param->magic;
    /* Fill all remaining bytes after the data with random values */
    esp_fill_random(buf->payload, send_param->len - sizeof(example_espnow_data_t));
    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

static void example_espnow_task(void *pvParameter)
{
    example_espnow_event_t evt;
    uint8_t recv_state = 0;
    uint16_t recv_seq = 0;
    int recv_magic = 0;
    bool is_broadcast = false;
    int ret;

    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Start sending broadcast data");

    /* Start sending broadcast ESPNOW data. */
    example_espnow_send_param_t *send_param = (example_espnow_send_param_t *)pvParameter;
    if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) 
    {
        ESP_LOGE(TAG, "Send error");
        example_espnow_deinit(send_param);
        vTaskDelete(NULL);
    }

    // Handle events from the espnow callbacks
    while (xQueueReceive(s_example_espnow_queue, &evt, portMAX_DELAY) == pdTRUE)
    {
        switch (evt.id)
        {
            // Sent a packet
            case EXAMPLE_ESPNOW_SEND_CB:
            {
                example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
                is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

                ESP_LOGD(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);

                if (is_broadcast && (send_param->broadcast == false)) {
                    break;
                }

                if( !is_broadcast )
                {
                    send_param->count--;
                    if (send_param->count == 0)
                    {
                        ESP_LOGI(TAG, "Send done");
                        example_espnow_deinit(send_param);
                        vTaskDelete(NULL);
                    }
                }

                /* Delay a while before sending the next data. */
                if(send_param->delay > 0) 
                {
                    vTaskDelay(send_param->delay/portTICK_PERIOD_MS);
                }

                ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(send_cb->mac_addr));

                memcpy(send_param->dest_mac, send_cb->mac_addr, ESP_NOW_ETH_ALEN);
                example_espnow_data_prepare(send_param);

                /* Send the next data after the previous data is sent. */
                if(esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK)
                {
                    ESP_LOGE(TAG, "Send error");
                    example_espnow_deinit(send_param);
                    vTaskDelete(NULL);
                }
                break;
            }
            case EXAMPLE_ESPNOW_RECV_CB:
            {
                example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

                ret = example_espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state, &recv_seq, &recv_magic);
                free(recv_cb->data);

                if (ret == EXAMPLE_ESPNOW_DATA_BROADCAST)
                {
                    ESP_LOGI(TAG, "Receive %dth broadcast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                    /* If MAC address does not exist in peer list, add it to peer list. */
                    if (esp_now_is_peer_exist(recv_cb->mac_addr) == false)
                    {
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
                        memcpy(peer->peer_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                        ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                        free(peer);
                    }

                    /* Indicates that the device has received broadcast ESPNOW data. */
                    if (send_param->state == 0)
                    {
                        send_param->state = 1;
                    }

                    /* If receive broadcast ESPNOW data which indicates that the other device has received
                     * broadcast ESPNOW data and the local magic number is bigger than that in the received
                     * broadcast ESPNOW data, stop sending broadcast ESPNOW data and start sending unicast
                     * ESPNOW data.
                     */
                    if (recv_state == 1) {
                        /* The device which has the bigger magic number sends ESPNOW data, the other one
                         * receives ESPNOW data.
                         */
                        if (send_param->unicast == false && send_param->magic >= recv_magic)
                        {
                            ESP_LOGI(TAG, "Start sending unicast data");
                            ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(recv_cb->mac_addr));

                            /* Start sending unicast ESPNOW data. */
                            memcpy(send_param->dest_mac, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                            example_espnow_data_prepare(send_param);
                            if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK)
                            {
                                ESP_LOGE(TAG, "Send error");
                                example_espnow_deinit(send_param);
                                vTaskDelete(NULL);
                            }
                            else
                            {
                                send_param->broadcast = false;
                                send_param->unicast = true;
                            }
                        }
                    }
                }
                else if (ret == EXAMPLE_ESPNOW_DATA_UNICAST)
                {
                    ESP_LOGI(TAG, "Receive %dth unicast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                    /* If receive unicast ESPNOW data, also stop sending broadcast ESPNOW data. */
                    send_param->broadcast = false;
                }
                else
                {
                    ESP_LOGI(TAG, "Receive error data from: "MACSTR"", MAC2STR(recv_cb->mac_addr));
                }
                break;
            }
            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }
}






/* -------------------------------------------------------------------------- */

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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




/*




    // TODO: cleanup/remove if we're using xQueueRx style stuff
    volatile uint8_t rx_tmp[32] = {0};
    uint8_t bytes_held = 0;

    while(1)
    {
        if( 1 == 0 )
        {
            // Get inbound bytes

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
                    gpio_set_level(GPIO_OUTPUT_IO_0, 1);
                }

            }
            gpio_set_level(GPIO_OUTPUT_IO_0, 0);
        }

        if(trigger_pending)
        {
            // TODO: send_payload();

            trigger_pending = false;
        }

    }



*/