#ifndef UDP_SERVER_H
#define UDP_SERVER_H


#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */

#include "udp_main_defs.h"

/* -------------------------------------------------------------------------- */

void udp_server_task(void *pvParameters);

/* -------------------------------------------------------------------------- */

void udp_server_send_payload( uint8_t *data, uint32_t length );

/* -------------------------------------------------------------------------- */

void udp_server_register_user_evt_queue( QueueHandle_t *queue );

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif	// end UDP_SERVER_H