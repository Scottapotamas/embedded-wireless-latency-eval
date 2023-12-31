#include "string.h"

#include "esp_system.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"

#include "server.h"
#include "ble_main_defs.h"

/* -------------------------------------------------------------------------- */

static QueueHandle_t *user_evt_queue;

/* -------------------------------------------------------------------------- */

#define GATTS_TAG  "GATTS_SPP_DEMO"

#define SPP_PROFILE_NUM             1
#define SPP_PROFILE_APP_IDX         0
#define ESP_SPP_APP_ID              0x56
#define SAMPLE_DEVICE_NAME          "ESP_SPP_SERVER"    //The Device Name Characteristics in GAP
#define SPP_SVC_INST_ID	            0

/// SPP Service
static const uint16_t spp_service_uuid = 0xABF0;
/// Characteristic UUID
#define ESP_GATT_UUID_SPP_DATA_RECEIVE      0xABF1
#define ESP_GATT_UUID_SPP_DATA_NOTIFY       0xABF2
#define ESP_GATT_UUID_SPP_COMMAND_RECEIVE   0xABF3
#define ESP_GATT_UUID_SPP_COMMAND_NOTIFY    0xABF4

static const uint8_t spp_adv_data[23] = {
    /* Flags */
    0x02,0x01,0x06,
    /* Complete List of 16-bit Service Class UUIDs */
    0x03,0x03,0xF0,0xAB,
    /* Complete Local Name in advertising */
    0x0F,0x09, 'E', 'S', 'P', '_', 'S', 'P', 'P', '_', 'S', 'E', 'R','V', 'E', 'R'
};

static uint16_t spp_mtu_size = 23;
static uint16_t spp_conn_id = 0xffff;
static esp_gatt_if_t spp_gatts_if = 0xff;

static bool enable_data_ntf = false;
static bool is_connected = false;
static esp_bd_addr_t spp_remote_bda = {0x0,};

static uint16_t spp_handle_table[SPP_IDX_NB];

static esp_ble_adv_params_t spp_adv_params = 
{
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst 
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

typedef struct spp_receive_data_node
{
    int32_t len;
    uint8_t * node_buff;
    struct spp_receive_data_node * next_node;
} spp_receive_data_node_t;

static spp_receive_data_node_t * temp_spp_recv_data_node_p1 = NULL;
static spp_receive_data_node_t * temp_spp_recv_data_node_p2 = NULL;

typedef struct spp_receive_data_buff
{
    int32_t node_num;
    int32_t buff_size;
    spp_receive_data_node_t * first_node;
} spp_receive_data_buff_t;

static spp_receive_data_buff_t SppRecvDataBuff = 
{
    .node_num   = 0,
    .buff_size  = 0,
    .first_node = NULL
};

/* -------------------------------------------------------------------------- */

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst spp_profile_tab[SPP_PROFILE_NUM] = 
{
    [SPP_PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

/*
 *  SPP PROFILE ATTRIBUTES
 ****************************************************************************************
 */

#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_WRITE_NR|ESP_GATT_CHAR_PROP_BIT_READ;

///SPP Service - data receive characteristic, read&write without response
static const uint16_t spp_data_receive_uuid = ESP_GATT_UUID_SPP_DATA_RECEIVE;
static const uint8_t  spp_data_receive_val[20] = {0x00};

///SPP Service - data notify characteristic, notify&read
static const uint16_t spp_data_notify_uuid = ESP_GATT_UUID_SPP_DATA_NOTIFY;
static const uint8_t  spp_data_notify_val[20] = {0x00};
static const uint8_t  spp_data_notify_ccc[2] = {0x00, 0x00};

///SPP Service - command characteristic, read&write without response
static const uint16_t spp_command_uuid = ESP_GATT_UUID_SPP_COMMAND_RECEIVE;
static const uint8_t  spp_command_val[10] = {0x00};

///SPP Service - status characteristic, notify&read
static const uint16_t spp_status_uuid = ESP_GATT_UUID_SPP_COMMAND_NOTIFY;
static const uint8_t  spp_status_val[10] = {0x00};
static const uint8_t  spp_status_ccc[2] = {0x00, 0x00};

///Full HRS Database Description - Used to add attributes into the database
static const esp_gatts_attr_db_t spp_gatt_db[SPP_IDX_NB] =
{
    //SPP -  Service Declaration
    [SPP_IDX_SVC]                      	=
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
    sizeof(spp_service_uuid), sizeof(spp_service_uuid), (uint8_t *)&spp_service_uuid}},

    //SPP -  data receive characteristic Declaration
    [SPP_IDX_SPP_DATA_RECV_CHAR]            =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
    CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},

    //SPP -  data receive characteristic Value
    [SPP_IDX_SPP_DATA_RECV_VAL]             	=
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_data_receive_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
    SPP_DATA_MAX_LEN,sizeof(spp_data_receive_val), (uint8_t *)spp_data_receive_val}},

    //SPP -  data notify characteristic Declaration
    [SPP_IDX_SPP_DATA_NOTIFY_CHAR]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
    CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

    //SPP -  data notify characteristic Value
    [SPP_IDX_SPP_DATA_NTY_VAL]   =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_data_notify_uuid, ESP_GATT_PERM_READ,
    SPP_DATA_MAX_LEN, sizeof(spp_data_notify_val), (uint8_t *)spp_data_notify_val}},

    //SPP -  data notify characteristic - Client Characteristic Configuration Descriptor
    [SPP_IDX_SPP_DATA_NTF_CFG]         =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
    sizeof(uint16_t),sizeof(spp_data_notify_ccc), (uint8_t *)spp_data_notify_ccc}},

    //SPP -  command characteristic Declaration
    [SPP_IDX_SPP_COMMAND_CHAR]            =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
    CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},

    //SPP -  command characteristic Value
    [SPP_IDX_SPP_COMMAND_VAL]                 =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_command_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
    SPP_CMD_MAX_LEN,sizeof(spp_command_val), (uint8_t *)spp_command_val}},

    //SPP -  status characteristic Declaration
    [SPP_IDX_SPP_STATUS_CHAR]            =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
    CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

    //SPP -  status characteristic Value
    [SPP_IDX_SPP_STATUS_VAL]                 =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_status_uuid, ESP_GATT_PERM_READ,
    SPP_STATUS_MAX_LEN,sizeof(spp_status_val), (uint8_t *)spp_status_val}},

    //SPP -  status characteristic - Client Characteristic Configuration Descriptor
    [SPP_IDX_SPP_STATUS_CFG]         =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
    sizeof(uint16_t),sizeof(spp_status_ccc), (uint8_t *)spp_status_ccc}},

};

/* -------------------------------------------------------------------------- */

static uint8_t find_char_and_desr_index(uint16_t handle)
{
    uint8_t error = 0xff;

    for( int i = 0; i < SPP_IDX_NB ; i++ )
    {
        if( handle == spp_handle_table[i] )
        {
            return i;
        }
    }

    return error;
}

/* -------------------------------------------------------------------------- */

static bool store_wr_buffer(esp_ble_gatts_cb_param_t *p_data)
{
    temp_spp_recv_data_node_p1 = (spp_receive_data_node_t *)malloc(sizeof(spp_receive_data_node_t));

    if(temp_spp_recv_data_node_p1 == NULL)
    {
        ESP_LOGI(GATTS_TAG, "malloc error %s %d", __func__, __LINE__);
        return false;
    }
    
    if(temp_spp_recv_data_node_p2 != NULL)
    {
        temp_spp_recv_data_node_p2->next_node = temp_spp_recv_data_node_p1;
    }
    
    temp_spp_recv_data_node_p1->len = p_data->write.len;
    SppRecvDataBuff.buff_size += p_data->write.len;
    temp_spp_recv_data_node_p1->next_node = NULL;
    temp_spp_recv_data_node_p1->node_buff = (uint8_t *)malloc(p_data->write.len);
    temp_spp_recv_data_node_p2 = temp_spp_recv_data_node_p1;
    
    if (temp_spp_recv_data_node_p1->node_buff == NULL) 
    {
        ESP_LOGI(GATTS_TAG, "malloc error %s %d\n", __func__, __LINE__);
        temp_spp_recv_data_node_p1->len = 0;
    }
    else
    {
        memcpy(temp_spp_recv_data_node_p1->node_buff,p_data->write.value,p_data->write.len);
    }

    if(SppRecvDataBuff.node_num == 0)
    {
        SppRecvDataBuff.first_node = temp_spp_recv_data_node_p1;
        SppRecvDataBuff.node_num++;
    }
    else
    {
        SppRecvDataBuff.node_num++;
    }

    return true;
}

/* -------------------------------------------------------------------------- */

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    esp_err_t err;
    ESP_LOGE(GATTS_TAG, "GAP_EVT, event %d", event);

    switch (event) 
    {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&spp_adv_params);
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            //advertising start complete event to indicate advertising start successfully or failed
            if((err = param->adv_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) 
            {
                ESP_LOGE(GATTS_TAG, "Advertising start failed: %s", esp_err_to_name(err));
            }
            break;
        default:
            break;
    }
}

/* -------------------------------------------------------------------------- */

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    esp_ble_gatts_cb_param_t *p_data = (esp_ble_gatts_cb_param_t *) param;
    uint8_t res = 0xff;

    // ESP_LOGI(GATTS_TAG, "event = %x",event);
    switch (event) 
    {
    	case ESP_GATTS_REG_EVT:
    	    ESP_LOGI(GATTS_TAG, "%s %d", __func__, __LINE__);
        	esp_ble_gap_set_device_name(SAMPLE_DEVICE_NAME);

        	ESP_LOGI(GATTS_TAG, "%s %d", __func__, __LINE__);
        	esp_ble_gap_config_adv_data_raw((uint8_t *)spp_adv_data, sizeof(spp_adv_data));

        	ESP_LOGI(GATTS_TAG, "%s %d", __func__, __LINE__);
        	esp_ble_gatts_create_attr_tab(spp_gatt_db, gatts_if, SPP_IDX_NB, SPP_SVC_INST_ID);
       	break;

    	case ESP_GATTS_READ_EVT:
            res = find_char_and_desr_index(p_data->read.handle);
            if(res == SPP_IDX_SPP_STATUS_VAL)
            {
                //TODO:client read the status characteristic
            }
       	 break;

    	case ESP_GATTS_WRITE_EVT: 
        {
    	    res = find_char_and_desr_index(p_data->write.handle);
            
            if( p_data->write.is_prep == false )
            {
                // ESP_LOGI(GATTS_TAG, "ESP_GATTS_WRITE_EVT : handle = %d", res);

                switch( res )
                {
                    case SPP_IDX_SPP_COMMAND_VAL:
                        uint8_t * spp_cmd_buff = NULL;
                        spp_cmd_buff = (uint8_t *)malloc((spp_mtu_size - 3) * sizeof(uint8_t));
                        if(spp_cmd_buff == NULL)
                        {
                            ESP_LOGE(GATTS_TAG, "%s malloc failed", __func__);
                            break;
                        }
                        memset(spp_cmd_buff, 0x00, (spp_mtu_size - 3));
                        memcpy(spp_cmd_buff, p_data->write.value, p_data->write.len);

                        esp_log_buffer_char( GATTS_TAG, (char *)(spp_cmd_buff), strlen((char *)spp_cmd_buff) );
                        free( spp_cmd_buff );
                    break;

                    case SPP_IDX_SPP_DATA_NTF_CFG:
                        if(    ( p_data->write.len == 2 )
                            && ( p_data->write.value[0] == 0x01 )
                            && ( p_data->write.value[1] == 0x00 ) 
                          )
                        {
                            enable_data_ntf = true;
                        }
                        else if( (p_data->write.len == 2)&&(p_data->write.value[0] == 0x00)&&(p_data->write.value[1] == 0x00))
                        {
                            enable_data_ntf = false;
                        }
                    break;

                    case SPP_IDX_SPP_DATA_RECV_VAL:
                        // esp_log_buffer_char(GATTS_TAG,(char *)(p_data->write.value),p_data->write.len);

                        // Post an event to the user-space event queue with the inbound data
                        if( user_evt_queue )
                        {
                            spp_event_t evt;
                            spp_event_recv_cb_t *recv_cb = &evt.data.recv_cb;
                            evt.id = SPP_RECV_CB;

                            // Allocate a sufficiently large chunk of memory and copy the payload into it
                            // User task is responsible for freeing this memory
                            recv_cb->data = malloc( p_data->write.len );
                            if( recv_cb->data == NULL )
                            {
                                ESP_LOGE(GATTS_TAG, "RX Malloc fail");
                                return;
                            }

                            memcpy(recv_cb->data, p_data->write.value, p_data->write.len);
                            recv_cb->data_len = p_data->write.len;

                            // Put the event into the queue for processing
                            if( xQueueSend(*user_evt_queue, &evt, 512) != pdTRUE )
                            {
                                ESP_LOGW(GATTS_TAG, "RX event failed to enqueue");
                                free(recv_cb->data);
                            }
                        }
                    break;
                }
             
            }
            else if(   (p_data->write.is_prep == true)
                    && (res == SPP_IDX_SPP_DATA_RECV_VAL) )
            {
                ESP_LOGI(GATTS_TAG, "ESP_GATTS_PREP_WRITE_EVT : handle = %d", res);
                store_wr_buffer(p_data);
            }
      	 	break;
    	}

    	case ESP_GATTS_EXEC_WRITE_EVT:{
    	    // ESP_LOGI(GATTS_TAG, "ESP_GATTS_EXEC_WRITE_EVT");

    	    if( p_data->exec_write.exec_write_flag )
            {
                temp_spp_recv_data_node_p1 = SppRecvDataBuff.first_node;

                // TODO: rework this
                while( temp_spp_recv_data_node_p1 != NULL )
                {
                    // Post an event to the user-space event queue with the inbound data
                    if( user_evt_queue )
                    {
                        spp_event_t evt;
                        spp_event_recv_cb_t *recv_cb = &evt.data.recv_cb;
                        evt.id = SPP_RECV_CB;

                        // Allocate a sufficiently large chunk of memory and copy the payload into it
                        // User task is responsible for freeing this memory
                        recv_cb->data = malloc( temp_spp_recv_data_node_p1->len );
                        if( recv_cb->data == NULL )
                        {
                            ESP_LOGE(GATTS_TAG, "RX Malloc fail");
                            return;
                        }

                        memcpy(recv_cb->data, temp_spp_recv_data_node_p1->node_buff, temp_spp_recv_data_node_p1->len);
                        recv_cb->data_len = temp_spp_recv_data_node_p1->len;

                        // Put the event into the queue for processing
                        if( xQueueSend(*user_evt_queue, &evt, 512) != pdTRUE )
                        {
                            ESP_LOGW(GATTS_TAG, "RX event failed to enqueue");
                            free(recv_cb->data);
                        }
                    }                    

                    temp_spp_recv_data_node_p1 = temp_spp_recv_data_node_p1->next_node;
                }

	            temp_spp_recv_data_node_p1 = SppRecvDataBuff.first_node;

                while( temp_spp_recv_data_node_p1 != NULL )
                {
                    temp_spp_recv_data_node_p2 = temp_spp_recv_data_node_p1->next_node;

                    if (temp_spp_recv_data_node_p1->node_buff) 
                    {
                        free(temp_spp_recv_data_node_p1->node_buff);
                    }

                    free(temp_spp_recv_data_node_p1);
                    temp_spp_recv_data_node_p1 = temp_spp_recv_data_node_p2;
                }

                SppRecvDataBuff.node_num = 0;
                SppRecvDataBuff.buff_size = 0;
                SppRecvDataBuff.first_node = NULL;
    	    }
    	    break;
    	}

    	case ESP_GATTS_MTU_EVT:
    	    spp_mtu_size = p_data->mtu.mtu;
    	    break;

    	case ESP_GATTS_CONF_EVT:
            esp_gatt_status_t status = param->conf.status;
            uint16_t conn_id = param->conf.conn_id;
            uint16_t attr_handle = param->conf.handle;

            if (status == ESP_GATT_OK)
            {
                // ESP_LOGI(GATTS_TAG, "Confirmation for conn_id: %d, attr_handle: %d", conn_id, attr_handle);
                if( user_evt_queue )
                {
                    spp_event_t evt;
                    spp_event_send_cb_t *send_cb = &evt.data.send_cb;
                    evt.id = SPP_SEND_CB;
                    send_cb->bytes_sent = 0;

                    // Put the event into the queue for processing
                    if( xQueueSend(*user_evt_queue, &evt, 512) != pdTRUE )
                    {
                        ESP_LOGW(GATTS_TAG, "Send event failed to enqueue");
                    }
                }
            }
            else
            {
                ESP_LOGE(GATTS_TAG, "Conf failed for conn_id: %d, attr_handle: %d, status: %d", conn_id, attr_handle, status);
            }

    	    break;

    	case ESP_GATTS_UNREG_EVT:
        	break;
    	case ESP_GATTS_DELETE_EVT:
        	break;
    	case ESP_GATTS_START_EVT:
        	break;
    	case ESP_GATTS_STOP_EVT:
        	break;

    	case ESP_GATTS_CONNECT_EVT:
    	    spp_conn_id = p_data->connect.conn_id;
    	    spp_gatts_if = gatts_if;
    	    is_connected = true;
    	    memcpy(&spp_remote_bda,&p_data->connect.remote_bda,sizeof(esp_bd_addr_t));

        	break;

    	case ESP_GATTS_DISCONNECT_EVT:
    	    is_connected = false;
    	    enable_data_ntf = false;
    	    esp_ble_gap_start_advertising(&spp_adv_params);
    	    break;

    	case ESP_GATTS_OPEN_EVT:
    	    break;
    	case ESP_GATTS_CANCEL_OPEN_EVT:
    	    break;
    	case ESP_GATTS_CLOSE_EVT:
    	    break;
    	case ESP_GATTS_LISTEN_EVT:
    	    break;
    	case ESP_GATTS_CONGEST_EVT:
    	    break;

    	case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        {
    	    ESP_LOGI(GATTS_TAG, "The number handle =%x",param->add_attr_tab.num_handle);

    	    if (param->add_attr_tab.status != ESP_GATT_OK)
            {
    	        ESP_LOGE(GATTS_TAG, "Create attribute table failed, error code=0x%x", param->add_attr_tab.status);
    	    }
    	    else if (param->add_attr_tab.num_handle != SPP_IDX_NB)
            {
    	        ESP_LOGE(GATTS_TAG, "Create attribute table abnormally, num_handle (%d) doesn't equal to HRS_IDX_NB(%d)", param->add_attr_tab.num_handle, SPP_IDX_NB);
    	    }
    	    else
            {
    	        memcpy(spp_handle_table, param->add_attr_tab.handles, sizeof(spp_handle_table));
    	        esp_ble_gatts_start_service(spp_handle_table[SPP_IDX_SVC]);
    	    }
    	    break;
    	}

    	default:
    	    break;
    }
}

/* -------------------------------------------------------------------------- */

static void gatts_event_handler(esp_gatts_cb_event_t event, 
                                esp_gatt_if_t gatts_if, 
                                esp_ble_gatts_cb_param_t *param
                               )
{
    // ESP_LOGI(GATTS_TAG, "EVT %d, gatts if %d", event, gatts_if);

    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            spp_profile_tab[SPP_PROFILE_APP_IDX].gatts_if = gatts_if;
        }
        else
        {
            ESP_LOGI(GATTS_TAG, "Reg app failed, app_id %04x, status %d",param->reg.app_id, param->reg.status);
            return;
        }
    }

    for( int idx = 0; idx < SPP_PROFILE_NUM; idx++ )
    {
        if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                gatts_if == spp_profile_tab[idx].gatts_if)
        {
            if (spp_profile_tab[idx].gatts_cb) {
                spp_profile_tab[idx].gatts_cb(event, gatts_if, param);
            }
        }
    }
}

/* -------------------------------------------------------------------------- */

void ble_server_register_callbacks( void )
{
    esp_ble_gap_register_callback( gap_event_handler );
    esp_ble_gatts_register_callback( gatts_event_handler );
    esp_ble_gatts_app_register( ESP_SPP_APP_ID );
}

/* -------------------------------------------------------------------------- */

void ble_server_register_user_evt_queue( QueueHandle_t *queue )
{
    if( queue )
    {
        user_evt_queue = queue;
    }
}

/* -------------------------------------------------------------------------- */

void ble_server_send_payload( uint8_t* buffer, uint16_t length )
{
    if(enable_data_ntf && is_connected)
    {
        if(length <= (spp_mtu_size - 3))
        {
            esp_ble_gatts_send_indicate(    spp_gatts_if, 
                                            spp_conn_id, 
                                            spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL], 
                                            length, 
                                            buffer, 
                                            false
                                        );
        }
        else
        {
            ESP_LOGI(GATTS_TAG, "Rejected %dB send, mtu is %d", length, spp_mtu_size);
        }
    }
}

/* -------------------------------------------------------------------------- */