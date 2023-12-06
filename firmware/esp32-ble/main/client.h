/* -------------------------------------------------------------------------- */

#include "spp_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* -------------------------------------------------------------------------- */

enum{
    SPP_IDX_SVC,

    SPP_IDX_SPP_DATA_RECV_VAL,

    SPP_IDX_SPP_DATA_NTY_VAL,
    SPP_IDX_SPP_DATA_NTF_CFG,

    SPP_IDX_SPP_COMMAND_VAL,

    SPP_IDX_SPP_STATUS_VAL,
    SPP_IDX_SPP_STATUS_CFG,
    
    SPP_IDX_NB,
};

/* -------------------------------------------------------------------------- */

void ble_client_register_callbacks( void );

/* -------------------------------------------------------------------------- */

void ble_client_register_user_evt_queue( QueueHandle_t *queue );

/* -------------------------------------------------------------------------- */

void ble_client_send_payload( uint8_t* buffer, uint16_t length );

/* -------------------------------------------------------------------------- */

