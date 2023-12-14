#ifndef TCP_SERVER_H
#define TCP_SERVER_H


#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */

#include "tcp_main_defs.h"

/* -------------------------------------------------------------------------- */

void tcp_server_task(void *pvParameters);

/* -------------------------------------------------------------------------- */

void tcp_server_send_payload( uint8_t *data, uint32_t length );

/* -------------------------------------------------------------------------- */

void tcp_server_register_user_evt_queue( QueueHandle_t *queue );

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif	// end TCP_SERVER_H