#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "client.h"
#include "ble_main_defs.h"

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"


/* -------------------------------------------------------------------------- */

static QueueHandle_t *user_evt_queue;

/* -------------------------------------------------------------------------- */

// Client stuff

#define GATTC_TAG                   "GATTC_SPP_DEMO"
#define PROFILE_NUM                 1
#define PROFILE_APP_ID              0
#define BT_BD_ADDR_STR              "%02x:%02x:%02x:%02x:%02x:%02x"
#define BT_BD_ADDR_HEX(addr)        addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]
#define ESP_GATT_SPP_SERVICE_UUID   0xABF0
#define SCAN_ALL_THE_TIME           0

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    esp_bd_addr_t remote_bda;
};

/* -------------------------------------------------------------------------- */

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

/* -------------------------------------------------------------------------- */

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

static const char device_name[] = "ESP_SPP_SERVER";
static bool is_connected = false;
static uint16_t spp_conn_id = 0;
static uint16_t spp_mtu_size = 23;
static uint16_t cmd = 0;
static uint16_t spp_srv_start_handle = 0;
static uint16_t spp_srv_end_handle = 0;
static uint16_t spp_gattc_if = 0xff;
static uint16_t count = SPP_IDX_NB;
static esp_gattc_db_elem_t *db = NULL;
static esp_ble_gap_cb_param_t scan_rst;

static esp_bt_uuid_t spp_service_uuid = {
    .len  = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_SPP_SERVICE_UUID,},
};

/* -------------------------------------------------------------------------- */

static void notify_event_handler(esp_ble_gattc_cb_param_t * p_data)
{
    uint8_t handle = 0;
/*
    if( p_data->notify.is_notify )
    {
        ESP_LOGI(GATTC_TAG,"+NOTIFY:handle = %d,length = %d ", p_data->notify.handle, p_data->notify.value_len);
    }
    else
    {
        ESP_LOGI(GATTC_TAG,"+INDICATE:handle = %d,length = %d ", p_data->notify.handle, p_data->notify.value_len);
    }
*/
    handle = p_data->notify.handle;
    if( db == NULL ) 
    {
        ESP_LOGE(GATTC_TAG, " %s db is NULL", __func__);
        return;
    }

    if( handle == db[SPP_IDX_SPP_DATA_NTY_VAL].attribute_handle )
    {
        // esp_log_buffer_char(GATTC_TAG, (char *)p_data->notify.value, p_data->notify.value_len);

        // Post an event to the user-space event queue with the inbound data
        if( user_evt_queue )
        {
            spp_event_t evt;
            spp_event_recv_cb_t *recv_cb = &evt.data.recv_cb;
            evt.id = SPP_RECV_CB;

            // Allocate a sufficiently large chunk of memory and copy the payload into it
            // User task is responsible for freeing this memory
            recv_cb->data = malloc( p_data->notify.value_len );
            if( recv_cb->data == NULL )
            {
                ESP_LOGE(GATTC_TAG, "RX Malloc fail");
                return;
            }

            memcpy(recv_cb->data, p_data->notify.value, p_data->notify.value_len);
            recv_cb->data_len = p_data->notify.value_len;

            // Put the event into the queue for processing
            if( xQueueSend(*user_evt_queue, &evt, 512) != pdTRUE )
            {
                ESP_LOGW(GATTC_TAG, "RX event failed to enqueue");
                free(recv_cb->data);
            }
        }
        
    }
    else if( handle == ((db+SPP_IDX_SPP_STATUS_VAL)->attribute_handle) )
    {
        // esp_log_buffer_char(GATTC_TAG, (char *)p_data->notify.value, p_data->notify.value_len);
        //TODO:server notify status characteristic
    }
    else
    {
        // esp_log_buffer_char(GATTC_TAG, (char *)p_data->notify.value, p_data->notify.value_len);
    }
}

static void free_gattc_srv_db(void)
{
    is_connected = false;
    spp_gattc_if = 0xff;
    spp_conn_id = 0;
    spp_mtu_size = 23;
    cmd = 0;
    spp_srv_start_handle = 0;
    spp_srv_end_handle = 0;
    if(db)
    {
        free(db);
        db = NULL;
    }
}

static void esp_gap_cb( esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param )
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    esp_err_t err;

    switch(event)
    {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: 
        {
            if((err = param->scan_param_cmpl.status) != ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGE(GATTC_TAG, "Scan param set failed: %s", esp_err_to_name(err));
                break;
            }
            //the unit of the duration is second
            uint32_t duration = 0xFFFF;
            ESP_LOGI(GATTC_TAG, "Enable Ble Scan:during time %04" PRIx32 " minutes.",duration);
            esp_ble_gap_start_scanning(duration);
            break;
        }

        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            //scan start complete event to indicate scan start successfully or failed
            if ((err = param->scan_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) 
            {
                ESP_LOGE(GATTC_TAG, "Scan start failed: %s", esp_err_to_name(err));
                break;
            }
            ESP_LOGI(GATTC_TAG, "Scan start successed");
            break;

        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            if ((err = param->scan_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS) 
            {
                ESP_LOGE(GATTC_TAG, "Scan stop failed: %s", esp_err_to_name(err));
                break;
            }
            ESP_LOGI(GATTC_TAG, "Scan stop successed");
            if (is_connected == false) 
            {
                ESP_LOGI(GATTC_TAG, "Connect to the remote device.");
                esp_ble_gattc_open(gl_profile_tab[PROFILE_APP_ID].gattc_if, scan_rst.scan_rst.bda, scan_rst.scan_rst.ble_addr_type, true);
            }
            break;

        case ESP_GAP_BLE_SCAN_RESULT_EVT: 
        {
            esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
            switch (scan_result->scan_rst.search_evt)
            {
                case ESP_GAP_SEARCH_INQ_RES_EVT:
                    esp_log_buffer_hex(GATTC_TAG, scan_result->scan_rst.bda, 6);
                    ESP_LOGI(GATTC_TAG, "Searched Adv Data Len %d, Scan Response Len %d", scan_result->scan_rst.adv_data_len, scan_result->scan_rst.scan_rsp_len);
                    adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
                    ESP_LOGI(GATTC_TAG, "Searched Device Name Len %d", adv_name_len);
                    esp_log_buffer_char(GATTC_TAG, adv_name, adv_name_len);
                    
                    if (adv_name != NULL) 
                    {
                        if ( strncmp((char *)adv_name, device_name, adv_name_len) == 0) 
                        {
                            memcpy(&(scan_rst), scan_result, sizeof(esp_ble_gap_cb_param_t));
                            esp_ble_gap_stop_scanning();
                        }
                    }
                    break;
                case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                    break;
                default:
                    break;
            }
            break;
        }

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if ((err = param->adv_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS){
                ESP_LOGE(GATTC_TAG, "Adv stop failed: %s", esp_err_to_name(err));
            }else {
                ESP_LOGI(GATTC_TAG, "Stop adv successfully");
            }
            break;
        default:
            break;
    }
}

static void esp_gattc_cb( esp_gattc_cb_event_t event,
                          esp_gatt_if_t gattc_if,
                          esp_ble_gattc_cb_param_t *param
                        )
{
    // ESP_LOGI(GATTC_TAG, "EVT %d, gattc if %d", event, gattc_if);

    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) 
    {
        if (param->reg.status == ESP_GATT_OK) 
        {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } 
        else 
        {
            ESP_LOGI(GATTC_TAG, "Reg app failed, app_id %04x, status %d", param->reg.app_id, param->reg.status);
            return;
        }
    }
    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    int idx;
    for (idx = 0; idx < PROFILE_NUM; idx++) 
    {
        if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                gattc_if == gl_profile_tab[idx].gattc_if) 
        {
            if (gl_profile_tab[idx].gattc_cb) 
            {
                gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
            }
        }
    }
}

static void gattc_profile_event_handler(    esp_gattc_cb_event_t event, 
                                            esp_gatt_if_t gattc_if, 
                                            esp_ble_gattc_cb_param_t *param
                                        )
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) 
    {
        case ESP_GATTC_REG_EVT:
            ESP_LOGI(GATTC_TAG, "REG EVT, set scan params");
            esp_ble_gap_set_scan_params(&ble_scan_params);
            break;

        case ESP_GATTC_CONNECT_EVT:
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_CONNECT_EVT: conn_id=%d, gatt_if = %d", spp_conn_id, gattc_if);
            ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
            esp_log_buffer_hex(GATTC_TAG, gl_profile_tab[PROFILE_APP_ID].remote_bda, sizeof(esp_bd_addr_t));
            spp_gattc_if = gattc_if;
            is_connected = true;
            spp_conn_id = p_data->connect.conn_id;
            memcpy(gl_profile_tab[PROFILE_APP_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
            esp_ble_gattc_search_service(spp_gattc_if, spp_conn_id, &spp_service_uuid);
            break;

        case ESP_GATTC_DISCONNECT_EVT:
            ESP_LOGI(GATTC_TAG, "disconnect");
            free_gattc_srv_db();
            esp_ble_gap_start_scanning(SCAN_ALL_THE_TIME);
            break;

        case ESP_GATTC_SEARCH_RES_EVT:
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_SEARCH_RES_EVT: start_handle = %d, end_handle = %d, UUID:0x%04x",p_data->search_res.start_handle,p_data->search_res.end_handle,p_data->search_res.srvc_id.uuid.uuid.uuid16);
            spp_srv_start_handle = p_data->search_res.start_handle;
            spp_srv_end_handle = p_data->search_res.end_handle;
            break;

        case ESP_GATTC_SEARCH_CMPL_EVT:
            ESP_LOGI(GATTC_TAG, "SEARCH_CMPL: conn_id = %x, status %d", spp_conn_id, p_data->search_cmpl.status);
            esp_ble_gattc_send_mtu_req(gattc_if, spp_conn_id);
            break;

        case ESP_GATTC_REG_FOR_NOTIFY_EVT: 
        {
            ESP_LOGI(GATTC_TAG,"Index = %d,status = %d,handle = %d",cmd, p_data->reg_for_notify.status, p_data->reg_for_notify.handle);
            if(p_data->reg_for_notify.status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT, status = %d", p_data->reg_for_notify.status);
                break;
            }
            uint16_t notify_en = 1;
            esp_ble_gattc_write_char_descr(
                    spp_gattc_if,
                    spp_conn_id,
                    (db+cmd+1)->attribute_handle,
                    sizeof(notify_en),
                    (uint8_t *)&notify_en,
                    ESP_GATT_WRITE_TYPE_RSP,
                    ESP_GATT_AUTH_REQ_NONE);

            break;
        }

        case ESP_GATTC_NOTIFY_EVT:
            // ESP_LOGI(GATTC_TAG,"ESP_GATTC_NOTIFY_EVT");
            notify_event_handler(p_data);
            break;

        case ESP_GATTC_READ_CHAR_EVT:
            ESP_LOGI(GATTC_TAG,"ESP_GATTC_READ_CHAR_EVT");
            break;

        case ESP_GATTC_WRITE_CHAR_EVT:
            // TODO: Add write callback here?
            ESP_LOGI(GATTC_TAG,"ESP_GATTC_WRITE_CHAR_EVT:status = %d,handle = %d", param->write.status, param->write.handle);
            if(param->write.status != ESP_GATT_OK)
            {
                ESP_LOGE(GATTC_TAG, "ESP_GATTC_WRITE_CHAR_EVT, error status = %d", p_data->write.status);
                break;
            }
            break;

        case ESP_GATTC_PREP_WRITE_EVT:
            break;

        case ESP_GATTC_EXEC_EVT:
            break;

        case ESP_GATTC_WRITE_DESCR_EVT:
            ESP_LOGI(GATTC_TAG,"ESP_GATTC_WRITE_DESCR_EVT: status =%d,handle = %d", p_data->write.status, p_data->write.handle);

            if(p_data->write.status != ESP_GATT_OK)
            {
                ESP_LOGE(GATTC_TAG, "ESP_GATTC_WRITE_DESCR_EVT, error status = %d", p_data->write.status);
                break;
            }

            switch(cmd)
            {
                case SPP_IDX_SPP_DATA_NTY_VAL:
                    cmd = SPP_IDX_SPP_STATUS_VAL;
                    ESP_LOGI(GATTC_TAG,"Index = %d,UUID = 0x%04x, handle = %d", cmd, (db+SPP_IDX_SPP_DATA_NTY_VAL)->uuid.uuid.uuid16, (db+SPP_IDX_SPP_DATA_NTY_VAL)->attribute_handle);
                    esp_ble_gattc_register_for_notify(spp_gattc_if, gl_profile_tab[PROFILE_APP_ID].remote_bda, (db+SPP_IDX_SPP_DATA_NTY_VAL)->attribute_handle);
                    break;
                case SPP_IDX_SPP_STATUS_VAL:
                    break;
                default:
                    break;
            };
        break;

        case ESP_GATTC_CFG_MTU_EVT:
            if(p_data->cfg_mtu.status != ESP_OK)
            {
                break;
            }
            ESP_LOGI(GATTC_TAG,"+MTU:%d", p_data->cfg_mtu.mtu);
            spp_mtu_size = p_data->cfg_mtu.mtu;

            db = (esp_gattc_db_elem_t *)malloc(count*sizeof(esp_gattc_db_elem_t));
            if(db == NULL)
            {
                ESP_LOGE(GATTC_TAG,"%s:malloc db falied",__func__);
                break;
            }

            if(esp_ble_gattc_get_db(spp_gattc_if, spp_conn_id, spp_srv_start_handle, spp_srv_end_handle, db, &count) != ESP_GATT_OK)
            {
                ESP_LOGE(GATTC_TAG,"%s:get db falied",__func__);
                break;
            }

            if(count != SPP_IDX_NB)
            {
                ESP_LOGE(GATTC_TAG,"%s:get db count != SPP_IDX_NB, count = %d, SPP_IDX_NB = %d",__func__,count,SPP_IDX_NB);
                break;
            }

            for( int i = 0; i < SPP_IDX_NB; i++ )
            {
                switch( (db+i)->type )
                {
                    case ESP_GATT_DB_PRIMARY_SERVICE:
                        ESP_LOGI(GATTC_TAG,"attr_type = PRIMARY_SERVICE,attribute_handle=%d,start_handle=%d,end_handle=%d,properties=0x%x,uuid=0x%04x",\
                                (db+i)->attribute_handle, (db+i)->start_handle, (db+i)->end_handle, (db+i)->properties, (db+i)->uuid.uuid.uuid16);
                        break;
                    case ESP_GATT_DB_SECONDARY_SERVICE:
                        ESP_LOGI(GATTC_TAG,"attr_type = SECONDARY_SERVICE,attribute_handle=%d,start_handle=%d,end_handle=%d,properties=0x%x,uuid=0x%04x",\
                                (db+i)->attribute_handle, (db+i)->start_handle, (db+i)->end_handle, (db+i)->properties, (db+i)->uuid.uuid.uuid16);
                        break;
                    case ESP_GATT_DB_CHARACTERISTIC:
                        ESP_LOGI(GATTC_TAG,"attr_type = CHARACTERISTIC,attribute_handle=%d,start_handle=%d,end_handle=%d,properties=0x%x,uuid=0x%04x",\
                                (db+i)->attribute_handle, (db+i)->start_handle, (db+i)->end_handle, (db+i)->properties, (db+i)->uuid.uuid.uuid16);
                        break;
                    case ESP_GATT_DB_DESCRIPTOR:
                        ESP_LOGI(GATTC_TAG,"attr_type = DESCRIPTOR,attribute_handle=%d,start_handle=%d,end_handle=%d,properties=0x%x,uuid=0x%04x",\
                                (db+i)->attribute_handle, (db+i)->start_handle, (db+i)->end_handle, (db+i)->properties, (db+i)->uuid.uuid.uuid16);
                        break;
                    case ESP_GATT_DB_INCLUDED_SERVICE:
                        ESP_LOGI(GATTC_TAG,"attr_type = INCLUDED_SERVICE,attribute_handle=%d,start_handle=%d,end_handle=%d,properties=0x%x,uuid=0x%04x",\
                                (db+i)->attribute_handle, (db+i)->start_handle, (db+i)->end_handle, (db+i)->properties, (db+i)->uuid.uuid.uuid16);
                        break;
                    case ESP_GATT_DB_ALL:
                        ESP_LOGI(GATTC_TAG,"attr_type = ESP_GATT_DB_ALL,attribute_handle=%d,start_handle=%d,end_handle=%d,properties=0x%x,uuid=0x%04x",\
                                (db+i)->attribute_handle, (db+i)->start_handle, (db+i)->end_handle, (db+i)->properties, (db+i)->uuid.uuid.uuid16);
                        break;
                    default:
                        break;
                }
            }

            cmd = SPP_IDX_SPP_DATA_NTY_VAL;
            ESP_LOGI(GATTC_TAG,"Index = %d,UUID = 0x%04x, handle = %d", cmd, (db+SPP_IDX_SPP_DATA_NTY_VAL)->uuid.uuid.uuid16, (db+SPP_IDX_SPP_DATA_NTY_VAL)->attribute_handle);
            esp_ble_gattc_register_for_notify(spp_gattc_if, gl_profile_tab[PROFILE_APP_ID].remote_bda, (db+SPP_IDX_SPP_DATA_NTY_VAL)->attribute_handle);

            break;

        case ESP_GATTC_SRVC_CHG_EVT:
            break;

        default:
            break;
    }
}










/* -------------------------------------------------------------------------- */

void ble_client_register_callbacks( void )
{
    esp_ble_gap_register_callback(esp_gap_cb);
    esp_ble_gattc_register_callback(esp_gattc_cb);
    esp_ble_gattc_app_register(PROFILE_APP_ID);

    esp_ble_gatt_set_local_mtu(200);
}

/* -------------------------------------------------------------------------- */

void ble_client_register_user_evt_queue( QueueHandle_t *queue )
{
    if( queue )
    {
        user_evt_queue = queue;
    }
}

/* -------------------------------------------------------------------------- */

void ble_client_send_payload( uint8_t* buffer, uint16_t length )
{
    if(    is_connected 
        && ( db != NULL ) 
        && ( (db+SPP_IDX_SPP_DATA_RECV_VAL)->properties & (ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_WRITE)) )
    {
        if( length <= (spp_mtu_size - 3) )
        {
            esp_ble_gattc_write_char( spp_gattc_if,
                          spp_conn_id,
                          (db+SPP_IDX_SPP_DATA_RECV_VAL)->attribute_handle,
                          length,
                          buffer,
                          ESP_GATT_WRITE_TYPE_RSP,
                          ESP_GATT_AUTH_REQ_NONE);
        }
        else
        {
            ESP_LOGI(GATTC_TAG, "Rejected %dB send, mtu is %d", length, spp_mtu_size);
        }
    }
}

/* -------------------------------------------------------------------------- */