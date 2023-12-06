/* -------------------------------------------------------------------------- */

#include "spp_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* -------------------------------------------------------------------------- */

enum{
    SPP_IDX_SVC,

    SPP_IDX_SPP_DATA_RECV_CHAR,
    SPP_IDX_SPP_DATA_RECV_VAL,

    SPP_IDX_SPP_DATA_NOTIFY_CHAR,
    SPP_IDX_SPP_DATA_NTY_VAL,
    SPP_IDX_SPP_DATA_NTF_CFG,

    SPP_IDX_SPP_COMMAND_CHAR,
    SPP_IDX_SPP_COMMAND_VAL,

    SPP_IDX_SPP_STATUS_CHAR,
    SPP_IDX_SPP_STATUS_VAL,
    SPP_IDX_SPP_STATUS_CFG,

    SPP_IDX_NB,
};

/* -------------------------------------------------------------------------- */

void ble_server_register_callbacks( void );

/* -------------------------------------------------------------------------- */


void ble_server_register_user_evt_queue( QueueHandle_t *queue );

/* -------------------------------------------------------------------------- */

void ble_server_send_payload( uint8_t* buffer, uint16_t length );

/* -------------------------------------------------------------------------- */
