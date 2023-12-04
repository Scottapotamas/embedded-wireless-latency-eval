#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"

#include "driver/gpio.h"

/* -------------------------------------------------------------------------- */

#define INITATOR 1
#define ACCEPTOR 2
#define SPP_DEVICE_MODE INITATOR
// #define SPP_DEVICE_MODE ACCEPTOR

/* -------------------------------------------------------------------------- */

// Packet size test length settings
// #define PAYLOAD_12B
// #define PAYLOAD_128B
#define PAYLOAD_1024B

/* -------------------------------------------------------------------------- */

// Test stimulus input pin
#define GPIO_INPUT_IO_0     19
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0))

// Status output pin
#define GPIO_OUTPUT_IO_0    18
#define GPIO_OUTPUT_IO_LED  2   // onboard LED signals if the board has added a peer

#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_LED))

#define ESP_INTR_FLAG_DEFAULT 0

/* -------------------------------------------------------------------------- */

#define SPP_SERVER_NAME "SPP_SERVER"
#define EXAMPLE_DEVICE_NAME "ESP_SPP_ACCEPTOR"

static const esp_spp_mode_t esp_spp_mode = ESP_SPP_MODE_CB;
static const bool esp_spp_enable_l2cap_ertm = true;

static const esp_spp_sec_t sec_mask = ESP_SPP_SEC_AUTHENTICATE;

/* -------------------------------------------------------------------------- */

// Specific to master
esp_bd_addr_t peer_bd_addr = {0};
static uint8_t peer_bdname_len;
static char peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
static const char remote_device_name[] = EXAMPLE_DEVICE_NAME;

#if SPP_DEVICE_MODE==INITATOR
static const esp_bt_inq_mode_t inq_mode = ESP_BT_INQ_MODE_GENERAL_INQUIRY;
static const uint8_t inq_len = 30;
static const uint8_t inq_num_rsps = 0;
#endif 

/* -------------------------------------------------------------------------- */

static const char *TAG = "espspp";

#define MAX_SPP_PAYLOAD_BYTES (64)
// #define MAX_SPP_PAYLOAD_BYTES (ESP_SPP_MAX_MTU)
#define SPP_QUEUE_SIZE (8)
static QueueHandle_t spp_evt_queue;

typedef enum {
    SPP_SEND_CB,
    SPP_RECV_CB,
} spp_event_id_t;

typedef struct {
    uint32_t bytes_sent;
    bool congested;
} spp_event_send_cb_t;

typedef struct {
    uint8_t *data;
    uint32_t data_len;
} spp_event_recv_cb_t;

typedef union {
    spp_event_send_cb_t send_cb;
    spp_event_recv_cb_t recv_cb;
} spp_event_data_t;

// Main task queue needs to support send and receive events
// The ID field helps distinguish between them
typedef struct {
    spp_event_id_t id;
    spp_event_data_t data;
} spp_event_t;

uint32_t spp_handle = 0;

/* -------------------------------------------------------------------------- */

volatile bool trigger_pending = false;

#define CRC_SEED (0xFFFFu)
uint16_t bytes_read = 0;
uint16_t bytes_sent = 0;

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

static void setup_gpio_output( void );
static void setup_gpio_input( void );
static void IRAM_ATTR gpio_isr_handler(void* arg);

static void crc16(uint8_t data, uint16_t *crc);

static void benchmark_task(void *pvParameter);

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

static char *bda2str(uint8_t * bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18)
    {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

static bool get_name_from_eir(uint8_t *eir, char *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if( !eir)
    {
        return false;
    }

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    
    if( !rmt_bdname)
    {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if( rmt_bdname)
    {
        if( rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if( bdname)
        {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }

        if( bdname_len)
        {
            *bdname_len = rmt_bdname_len;
        }

        return true;
    }

    return false;
}

/* -------------------------------------------------------------------------- */

static void spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    uint8_t i = 0;
    char bda_str[18] = {0};

    switch (event) 
    {
        case ESP_SPP_INIT_EVT:
            if (param->init.status == ESP_SPP_SUCCESS) 
            {
                ESP_LOGI(TAG, "ESP_SPP_INIT_EVT");

#if SPP_DEVICE_MODE==INITATOR
                esp_bt_dev_set_device_name(EXAMPLE_DEVICE_NAME);
                esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
                esp_bt_gap_start_discovery(inq_mode, inq_len, inq_num_rsps);
#else
                esp_spp_start_srv(sec_mask, ESP_SPP_ROLE_SLAVE, 0, SPP_SERVER_NAME);
#endif
            }
            else 
            {
                ESP_LOGE(TAG, "ESP_SPP_INIT_EVT status:%d", param->init.status);
            }
            break;

        case ESP_SPP_DISCOVERY_COMP_EVT:
            if( param->disc_comp.status == ESP_SPP_SUCCESS )
            {
                ESP_LOGI(TAG, "ESP_SPP_DISCOVERY_COMP_EVT scn_num:%d", param->disc_comp.scn_num);
                
                for( i = 0; i < param->disc_comp.scn_num; i++ )
                {
                    ESP_LOGI(   TAG,
                                "-- [%d] scn:%d service_name:%s", 
                                i, 
                                param->disc_comp.scn[i],
                                param->disc_comp.service_name[i]
                            );
                }

                /* We only connect to the first found server on the remote SPP acceptor here */
                esp_spp_connect(sec_mask, ESP_SPP_ROLE_MASTER, param->disc_comp.scn[0], peer_bd_addr);
            }
            else
            {
                ESP_LOGE(TAG, "ESP_SPP_DISCOVERY_COMP_EVT status=%d", param->disc_comp.status);
            }
            break;

        case ESP_SPP_OPEN_EVT:
            if( param->open.status == ESP_SPP_SUCCESS )
            {
                ESP_LOGI(TAG, "ESP_SPP_OPEN_EVT handle:%"PRIu32" rem_bda:[%s]", param->open.handle,
                         bda2str(param->open.rem_bda, bda_str, sizeof(bda_str)));

                // Cache the handle (a bit dirty, sorry), allowing user-space send calls to be made
                spp_handle = param->open.handle;
            }
            else
            {
                ESP_LOGE(TAG, "ESP_SPP_OPEN_EVT status:%d", param->open.status);
            }
            break;

        case ESP_SPP_CLOSE_EVT:
            spp_handle = 0;
            ESP_LOGI( TAG, 
                      "ESP_SPP_CLOSE_EVT status:%d handle:%"PRIu32" close_by_remote:%d", 
                      param->close.status,
                      param->close.handle, 
                      param->close.async 
                    );
            break;

        case ESP_SPP_START_EVT:
            if( param->start.status == ESP_SPP_SUCCESS )
            {
                ESP_LOGI( TAG, 
                          "ESP_SPP_START_EVT handle:%"PRIu32" sec_id:%d scn:%d", 
                          param->start.handle, 
                          param->start.sec_id,
                          param->start.scn
                        );
                esp_bt_dev_set_device_name(EXAMPLE_DEVICE_NAME);
                esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            }
            else
            {
                ESP_LOGE(TAG, "ESP_SPP_START_EVT status:%d", param->start.status);
            }
            break;

        case ESP_SPP_CL_INIT_EVT:
            if( param->cl_init.status == ESP_SPP_SUCCESS )
            {
                ESP_LOGI( TAG, 
                          "ESP_SPP_CL_INIT_EVT handle:%"PRIu32" sec_id:%d", 
                          param->cl_init.handle, 
                          param->cl_init.sec_id
                        );
            } 
            else 
            {
                ESP_LOGE(TAG, "ESP_SPP_CL_INIT_EVT status:%d", param->cl_init.status);
            }
            break;

        case ESP_SPP_WRITE_EVT:
        {
            spp_handle = param->write.handle;

            // Post an event to the user-space event queue describing the 'write completed' data
            spp_event_t evt;
            spp_event_send_cb_t *send_cb = &evt.data.send_cb;
            evt.id = SPP_SEND_CB;

            if( param->write.status == ESP_SPP_SUCCESS )
            {
                send_cb->bytes_sent = param->write.len;
            }
            else
            {
                ESP_LOGI(TAG, "TX  Fail, sent  %i", param->write.len);

                // Previous packet failed to send
                send_cb->bytes_sent = 0;
            }

            // Provide congestion status to the task
            send_cb->congested = param->write.cong;

            // Put the event into the queue for processing
            if( xQueueSend(spp_evt_queue, &evt, 512) != pdTRUE )
            {
                ESP_LOGW(TAG, "TX Write event failed to enqueue");
            }

            break;
        }

        case ESP_SPP_CONG_EVT:
            // Congestion resolution means the user-space task can continue sending if needed
            if( param->cong.cong == 0 )
            {
                // Publish an event to the user-space queue with updated status
                spp_event_t evt;
                spp_event_send_cb_t *send_cb = &evt.data.send_cb;
                evt.id = SPP_SEND_CB;

                send_cb->congested = param->cong.cong;
                send_cb->bytes_sent = 0;

                // Put the event into the queue for processing
                if( xQueueSend(spp_evt_queue, &evt, 512) != pdTRUE )
                {
                    ESP_LOGW(TAG, "Congestion update event failed to enqueue");
                }
            }
            break;

        case ESP_SPP_DATA_IND_EVT:
        {
            // Post an event to the user-space event queue with the inbound data
            spp_event_t evt;
            spp_event_recv_cb_t *recv_cb = &evt.data.recv_cb;
            evt.id = SPP_RECV_CB;

            // Allocate a sufficiently large chunk of memory and copy the payload into it
            recv_cb->data = malloc( param->data_ind.len );
            if( recv_cb->data == NULL )
            {
                ESP_LOGE(TAG, "RX Malloc fail");
                return;
            }
            memcpy(recv_cb->data, param->data_ind.data, param->data_ind.len);
            recv_cb->data_len = param->data_ind.len;

            // Put the event into the queue for processing
            if( xQueueSend(spp_evt_queue, &evt, 512) != pdTRUE )
            {
                ESP_LOGW(TAG, "RX event failed to enqueue");
                free(recv_cb->data);
            }
            break;
        }

        case ESP_SPP_SRV_OPEN_EVT:
            ESP_LOGI( TAG, 
                      "ESP_SPP_SRV_OPEN_EVT status:%d handle:%"PRIu32", rem_bda:[%s]", 
                      param->srv_open.status,
                      param->srv_open.handle,
                      bda2str(param->srv_open.rem_bda, 
                      bda_str, 
                      sizeof(bda_str))
                    );
            break;

        case ESP_SPP_SRV_STOP_EVT:
            ESP_LOGI(TAG, "ESP_SPP_SRV_STOP_EVT");
            break;

        case ESP_SPP_UNINIT_EVT:
            ESP_LOGI(TAG, "ESP_SPP_UNINIT_EVT");
            break;

        default:
            break;
    }
}

/* -------------------------------------------------------------------------- */

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch(event)
    {
        case ESP_BT_GAP_DISC_RES_EVT:
            ESP_LOGI(TAG, "ESP_BT_GAP_DISC_RES_EVT:");
            esp_log_buffer_hex(TAG, param->disc_res.bda, ESP_BD_ADDR_LEN);

            // Search the EIR data for the target peer device name
            for( int i = 0; i < param->disc_res.num_prop; i++ )
            {
                if( param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR
                    && get_name_from_eir(param->disc_res.prop[i].val, peer_bdname, &peer_bdname_len)
                  )
                {
                    esp_log_buffer_char(TAG, peer_bdname, peer_bdname_len);

                    // Have we found the target peer device?
                    if(    strlen(remote_device_name) == peer_bdname_len
                        && strncmp(peer_bdname, remote_device_name, peer_bdname_len) == 0 ) 
                    {
                        memcpy(peer_bd_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);

                        esp_bt_gap_cancel_discovery();
                        esp_spp_start_discovery(peer_bd_addr);
                    }
                }
            }
            break;

        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            ESP_LOGI(TAG, "ESP_BT_GAP_DISC_STATE_CHANGED_EVT");
            break;

        case ESP_BT_GAP_RMT_SRVCS_EVT:
            ESP_LOGI(TAG, "ESP_BT_GAP_RMT_SRVCS_EVT");
            break;

        case ESP_BT_GAP_RMT_SRVC_REC_EVT:
            ESP_LOGI(TAG, "ESP_BT_GAP_RMT_SRVC_REC_EVT");
            break;

        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if( param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS )
            {
                ESP_LOGI(TAG, "authentication success: %s", param->auth_cmpl.device_name);
                esp_log_buffer_hex(TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
            }
            else
            {
                ESP_LOGE(TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
            }
            break;
        
        case ESP_BT_GAP_PIN_REQ_EVT:
            if( param->pin_req.min_16_digit )
            {
                ESP_LOGI(TAG, "Input pin code: 0000 0000 0000 0000");
                esp_bt_pin_code_t pin_code = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
            }
            else
            {
                ESP_LOGI(TAG, "Input pin code: 1234");
                esp_bt_pin_code_t pin_code;
                pin_code[0] = '1';
                pin_code[1] = '2';
                pin_code[2] = '3';
                pin_code[3] = '4';
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;
        
            case ESP_BT_GAP_MODE_CHG_EVT:
            ESP_LOGI(TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d", param->mode_chg.mode);
            break;

        default:
            break;
    }
}

/* -------------------------------------------------------------------------- */

static void setup_bt( void )
{
    esp_err_t ret = 0;

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if( (ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK)
    {
        ESP_LOGE(TAG, "%s init bt failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if( (ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK)
    {
        ESP_LOGE(TAG, "%s enable bt failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if( (ret = esp_bluedroid_init()) != ESP_OK)
    {
        ESP_LOGE(TAG, "%s init bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if( (ret = esp_bluedroid_enable()) != ESP_OK)
    {
        ESP_LOGE(TAG, "%s enable bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if( (ret = esp_bt_gap_register_callback(bt_gap_cb)) != ESP_OK)
    {
        ESP_LOGE(TAG, "%s gap register failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if( (ret = esp_spp_register_callback(spp_cb)) != ESP_OK)
    {
        ESP_LOGE(TAG, "%s spp register failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    esp_spp_cfg_t bt_spp_cfg = {
        .mode = esp_spp_mode,
        .enable_l2cap_ertm = esp_spp_enable_l2cap_ertm,
        .tx_buffer_size = 0, // Only used for ESP_SPP_MODE_VFS mode
    };

    if( (ret = esp_spp_enhanced_init(&bt_spp_cfg)) != ESP_OK)
    {
        ESP_LOGE(TAG, "%s spp init failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);
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

    // Useful callback events need a queue for user-space handling 
    spp_evt_queue = xQueueCreate(SPP_QUEUE_SIZE, sizeof(spp_event_t));
    if (spp_evt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create user-space evt queue");
        return;
    }

    // Bluetooth Setup
    setup_bt();

    char bda_str[18] = {0};
    ESP_LOGI(   TAG, 
                "This board is: %s", 
                bda2str((uint8_t *)esp_bt_dev_get_address(), 
                bda_str, 
                sizeof(bda_str))
            );

    // Start benchmark logic task
    xTaskCreate(benchmark_task, "benchmark_task", 2048, NULL, 4, NULL);
}

static void benchmark_task(void *pvParameter)
{
    spp_event_t evt;
    uint16_t bytes_sent = 0;

    while(1)
    {
        if( trigger_pending )
        {
            if( spp_handle )
            {
                // Chunk large payloads into 250 byte packets
                bytes_sent = 0;

                uint16_t bytes_to_send = sizeof(test_payload) - bytes_sent;
                if( bytes_to_send > MAX_SPP_PAYLOAD_BYTES )
                {
                    bytes_to_send = MAX_SPP_PAYLOAD_BYTES;
                }
                
                esp_spp_write(spp_handle, bytes_to_send, &test_payload[bytes_sent] );

                // Once the packet is sent, ESP_SPP_WRITE_EVT fires with how many bytes were sent.
                // Subsequent chunks are sent from the event queue logic below 
            }

            trigger_pending = false;
        }

        // Handle inbound data from callbacks
        if( xQueueReceive(spp_evt_queue, &evt, 1) )
        {
            switch( evt.id )
            {
                // Previously sent a packet
                case SPP_SEND_CB:
                {
                    spp_event_send_cb_t *send_cb = &evt.data.send_cb;

                    // ESP_LOGI(TAG, "At %i, Wrote %"PRIu32"B, CON: %i", bytes_sent, send_cb->bytes_sent, send_cb->congested);

                    // Update index of sent data (for multi-packet transfers)
                    bytes_sent += send_cb->bytes_sent;

                    // If the link isn't congested, send more data as needed
                    if( !send_cb->congested )
                    {
                        // Send the next chunk if needed
                        if( bytes_sent < sizeof(test_payload) )
                        {
                            uint16_t bytes_to_send = sizeof(test_payload) - bytes_sent;
                            if( bytes_to_send > MAX_SPP_PAYLOAD_BYTES )
                            {
                                bytes_to_send = MAX_SPP_PAYLOAD_BYTES;
                            }

                            esp_spp_write(spp_handle, bytes_to_send, &test_payload[bytes_sent]);
                        }
                        else
                        {
                            bytes_sent = 0;
                            // ESP_LOGI(TAG, "FIN \n");
                        }
                    }
                    else
                    {
                        // ESP_LOGI(TAG, "...");
                    }
  
                    break;
                } // end tx callback handling

                case SPP_RECV_CB:
                {
                    // Destructure the callback into something more ergonomic
                    spp_event_recv_cb_t *recv_cb = &evt.data.recv_cb;

                    // ESP_LOGI(TAG, "Got %"PRIu32"B", recv_cb->data_len);

                    for( uint16_t i = 0; i < recv_cb->data_len; i++ )
                    {
                        // Reset the "parser" if the start of a new test structure is seen
                        if(recv_cb->data[i] == 0x00 )
                        {
                            bytes_read = 0;
                            working_crc = CRC_SEED;
                            // ESP_LOGI(TAG, "RESET\n");

                        }

                        // Running crc and byte count
                        crc16( recv_cb->data[i], &working_crc );
                        bytes_read++;

                        // Identify the end of the packet via expected length and correct CRC
                        if( bytes_read == sizeof(test_payload) && working_crc == payload_crc )
                        {
                            // Valid test structure
                            gpio_set_level( GPIO_OUTPUT_IO_0, 1 );
                            // ESP_LOGI(TAG, "GOOD \n");
                        }
                    }

                    gpio_set_level( GPIO_OUTPUT_IO_0, 0 );
                    
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