#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H


#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */

#include "websockets_main_defs.h"

/* -------------------------------------------------------------------------- */

void websocket_client_task(void *pvParameters);

/* -------------------------------------------------------------------------- */

void websocket_client_send_payload( uint8_t *data, uint32_t length );

/* -------------------------------------------------------------------------- */

void websocket_client_register_user_evt_queue( QueueHandle_t *queue );

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif	// end WEBSOCKET_CLIENT_H