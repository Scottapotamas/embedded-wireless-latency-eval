#ifndef CENTRAL_H
#define CENTRAL_H

/* -------------------------------------------------------------------------- */

#include "benchmark_defs.h"
#include <zephyr/kernel.h>

/* -------------------------------------------------------------------------- */

void central_init(void);

/* -------------------------------------------------------------------------- */

void central_send_payload( uint8_t *data, uint32_t length );

/* -------------------------------------------------------------------------- */

void central_register_user_evt_queue( struct k_msgq *queue );

/* -------------------------------------------------------------------------- */

#endif	// end CENTRAL_H