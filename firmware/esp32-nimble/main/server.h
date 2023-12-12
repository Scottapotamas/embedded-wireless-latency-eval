#ifndef H_BLESPPSERVER_
#define H_BLESPPSERVER_

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* -------------------------------------------------------------------------- */

void ble_server_setup( void );

/* -------------------------------------------------------------------------- */

void ble_server_register_user_evt_queue( QueueHandle_t *queue );

/* -------------------------------------------------------------------------- */

void ble_server_send_payload( uint8_t* buffer, uint16_t length );

/* -------------------------------------------------------------------------- */


#ifdef __cplusplus
}
#endif

#endif