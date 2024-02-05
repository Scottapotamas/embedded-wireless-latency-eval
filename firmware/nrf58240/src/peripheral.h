#ifndef PERIPHERAL_H
#define PERIPHERAL_H

/* -------------------------------------------------------------------------- */

#include "benchmark_defs.h"
#include <zephyr/kernel.h>

/* -------------------------------------------------------------------------- */

void peripheral_init(void);

/* -------------------------------------------------------------------------- */

void peripheral_send_payload( uint8_t *data, uint32_t length );

/* -------------------------------------------------------------------------- */

void peripheral_register_user_evt_queue( struct k_msgq *queue );

/* -------------------------------------------------------------------------- */

#endif	// end PERIPHERAL_H