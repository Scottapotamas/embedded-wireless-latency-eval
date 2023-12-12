#ifndef H_BLESPPCLIENT_
#define H_BLESPPCLIENT_


#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* -------------------------------------------------------------------------- */

void ble_client_setup( void );

/* -------------------------------------------------------------------------- */

void ble_client_register_user_evt_queue( QueueHandle_t *queue );

/* -------------------------------------------------------------------------- */

void ble_client_send_payload( uint8_t* buffer, uint16_t length );

/* -------------------------------------------------------------------------- */


#ifdef __cplusplus
}
#endif

#endif