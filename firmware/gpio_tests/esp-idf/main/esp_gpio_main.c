#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "esp_gpio_test";

// Test stimulus input pin
#define GPIO_INPUT_IO_0     19
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0))

// Status output pin
#define GPIO_OUTPUT_IO_0    18
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0))

#define ESP_INTR_FLAG_DEFAULT 0

volatile bool trigger_pending = false;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    trigger_pending = true;
}

void app_main(void)
{
    gpio_config_t io_conf = {};

    // Output pin
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

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

    while(1)
    {
        if( trigger_pending )
        {
            gpio_set_level(GPIO_OUTPUT_IO_0, 1);
            trigger_pending = false;
        }
        else
        {
            gpio_set_level(GPIO_OUTPUT_IO_0, 0);
        }
    }
}