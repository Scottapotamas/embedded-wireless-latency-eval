#include "string.h"

#include "server.h"
#include "ble_main_defs.h"

#include "esp_system.h"
#include "esp_log.h"

#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* -------------------------------------------------------------------------- */

static QueueHandle_t *user_evt_queue;

static const char *TAG = "NimBLE-S";

/* -------------------------------------------------------------------------- */

static void ble_spp_server_print_conn_desc(struct ble_gap_conn_desc *desc);
static void ble_spp_server_advertise(void);
static int ble_spp_server_gap_event(struct ble_gap_event *event, void *arg);
static void ble_spp_server_on_reset(int reason);
static void ble_spp_server_on_sync(void);
static void ble_spp_server_host_task(void *param);
static int  ble_svc_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
int gatt_svr_init(void);

int gatt_svr_register(void);
void ble_store_config_init(void);

/* -------------------------------------------------------------------------- */

#define LL_PACKET_TIME (2120)
#define LL_PACKET_LENGTH (200)
#define MTU_DESIRED (200)


/* 16 Bit SPP Service UUID */
#define BLE_SVC_SPP_UUID16                                  0xABF0

/* 16 Bit SPP Service Characteristic UUID */
#define BLE_SVC_SPP_CHR_UUID16                              0xABF1

struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

/* -------------------------------------------------------------------------- */

static uint8_t own_addr_type;
static bool conn_handle_subs[CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1];
static uint16_t ble_spp_svc_gatt_read_val_handle;
static uint16_t spp_mtu_size = 23;

// Custom service
static const struct ble_gatt_svc_def new_ble_svc_gatt_defs[] = {
    {
        /*** Service: SPP */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_UUID16),
        .characteristics = (struct ble_gatt_chr_def[])
        { {
                /* Support SPP service */
                .uuid = BLE_UUID16_DECLARE(BLE_SVC_SPP_CHR_UUID16),
                .access_cb = ble_svc_gatt_handler,
                .val_handle = &ble_spp_svc_gatt_read_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            }, {
                0, /* No more characteristics */
            }
        },
    },
    {
        0, /* No more services. */
    },
};

/* -------------------------------------------------------------------------- */

static char *bda2str(uint8_t * bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18)
    {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[5], p[4], p[3], p[2], p[1], p[0]);
    return str;
}

// Logs information about a connection.
static void ble_spp_server_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    /*
        The description structure is using MyNewt's struct ble_gap_conn_desc 
        We want to print the address info, which is this structure 

        typedef struct {
            uint8_t type;
            uint8_t val[6];
        } ble_addr_t;

        So I bring in bda2str() which helps format val[6] into a string
    */
    char bda_str[18] = {0};

    ESP_LOGI(TAG, "handle=%d our_ota_addr_type=%d our_ota_addr=%s",
                desc->conn_handle, 
                desc->our_ota_addr.type, 
                bda2str(desc->our_ota_addr.val, bda_str, sizeof(bda_str))
                );

    ESP_LOGI(TAG, " our_id_addr_type=%d our_id_addr=%s",
                desc->our_id_addr.type, 
                bda2str(desc->our_id_addr.val, bda_str, sizeof(bda_str))
                );

    ESP_LOGI(TAG, " peer_ota_addr_type=%d peer_ota_addr=%s",
                desc->peer_ota_addr.type, 
                bda2str(desc->peer_ota_addr.val, bda_str, sizeof(bda_str))
                );

    ESP_LOGI(TAG, " peer_id_addr_type=%d peer_id_addr=%s",
                desc->peer_id_addr.type, 
                bda2str(desc->peer_id_addr.val, bda_str, sizeof(bda_str))
                );

    ESP_LOGI(TAG, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void ble_spp_server_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[])
    {
        BLE_UUID16_INIT(BLE_SVC_SPP_UUID16)
    };

    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if( rc != 0)
    {
        ESP_LOGE( TAG, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_spp_server_gap_event, NULL);
    if( rc != 0)
    {
        ESP_LOGE( TAG, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * ble_spp_server uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  ble_spp_server.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int ble_spp_server_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            /* A new connection was established or a connection attempt failed. */
            ESP_LOGI(TAG, "Connection %s; status=%d ",
                        event->connect.status == 0 ? "established" : "failed",
                        event->connect.status);

            if( event->connect.status == 0 )
            {
                rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
                assert(rc == 0);
                ble_spp_server_print_conn_desc(&desc);

                ble_gap_set_data_len(event->connect.conn_handle, LL_PACKET_LENGTH, LL_PACKET_TIME);

                // ble_hs_hci_util_set_data_len( event->connect.conn_handle,
                //               LL_PACKET_LENGTH,
                //               LL_PACKET_TIME );

            }

            if( event->connect.status != 0 || CONFIG_BT_NIMBLE_MAX_CONNECTIONS > 1 )
            {
                /* Connection failed or if multiple connection allowed; resume advertising. */
                ble_spp_server_advertise();
            }

            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "disconnect; reason=%d ", event->disconnect.reason);
            ble_spp_server_print_conn_desc(&event->disconnect.conn);
            ESP_LOGI(TAG, "\n");

            conn_handle_subs[event->disconnect.conn.conn_handle] = false;

            /* Connection terminated; resume advertising. */
            ble_spp_server_advertise();
            return 0;

        case BLE_GAP_EVENT_CONN_UPDATE:
            /* The central has updated the connection parameters. */
            ESP_LOGI(TAG, "connection updated; status=%d ",
                        event->conn_update.status);
            rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
            assert(rc == 0);
            ble_spp_server_print_conn_desc(&desc);
            ESP_LOGI(TAG, "\n");
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "advertise complete; reason=%d",
                        event->adv_complete.reason);
            ble_spp_server_advertise();
            return 0;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                        event->mtu.conn_handle,
                        event->mtu.channel_id,
                        event->mtu.value);
            spp_mtu_size = event->mtu.value;
            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d "
                        "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                        event->subscribe.conn_handle,
                        event->subscribe.attr_handle,
                        event->subscribe.reason,
                        event->subscribe.prev_notify,
                        event->subscribe.cur_notify,
                        event->subscribe.prev_indicate,
                        event->subscribe.cur_indicate);
            conn_handle_subs[event->subscribe.conn_handle] = true;
            return 0;

        case BLE_GAP_EVENT_NOTIFY_TX:

            if( user_evt_queue )
            {
                spp_event_t evt;
                spp_event_send_cb_t *send_cb = &evt.data.send_cb;
                evt.id = SPP_SEND_CB;
                send_cb->bytes_sent = 0;

                // Put the event into the queue for processing
                if( xQueueSend(*user_evt_queue, &evt, 512) != pdTRUE )
                {
                    ESP_LOGW(TAG, "Send event failed to enqueue");
                }
            }
            return 0;

        default:
            return 0;
    }
}

static void ble_spp_server_on_reset(int reason)
{
    ESP_LOGE( TAG, "Resetting state; reason=%d\n", reason);
}

static void ble_spp_server_on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    // Figure out address to use while advertising (no privacy) 
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if( rc != 0)
    {
        ESP_LOGE( TAG, "error determining address type; rc=%d\n", rc);
        return;
    }

    // Printing this device's bluetooth address
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    char bda_str[18] = {0};
    ESP_LOGI(TAG, "Device Address: %s\n", bda2str(addr_val, bda_str, sizeof(bda_str)));

    ble_spp_server_advertise();
}

static void ble_spp_server_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");

    nimble_port_run();

    // Cleanup
    nimble_port_freertos_deinit();
}

/* Callback function for custom service */
static int  ble_svc_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI( TAG, "Callback for read");
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        // Client wrote against the server
        // ESP_LOGI( TAG, "Data received in write event,conn_handle = %x,attr_handle = %x, length = %d", conn_handle, attr_handle, ctxt->om->om_len);
        
        // Post an event to the user-space event queue with the inbound data
        if( user_evt_queue )
        {
            spp_event_t evt;
            spp_event_recv_cb_t *recv_cb = &evt.data.recv_cb;
            evt.id = SPP_RECV_CB;

            // Allocate a sufficiently large chunk of memory and copy the payload into it
            // User task is responsible for freeing this memory
            recv_cb->data = malloc( ctxt->om->om_len );
            if( recv_cb->data == NULL )
            {
                ESP_LOGE( TAG, "RX Malloc fail");
                return 0;
            }

            // Data is behind the ble_gatt_access_ctxt pointer
            os_mbuf_copydata(ctxt->om, 0, ctxt->om->om_len, recv_cb->data);
            recv_cb->data_len = ctxt->om->om_len;

            // Put the event into the queue for processing
            if( xQueueSend(*user_evt_queue, &evt, 512) != pdTRUE )
            {
                ESP_LOGW( TAG, "RX event failed to enqueue");
                free(recv_cb->data);
            }
        }
        break;

    default:
        ESP_LOGI( TAG, "\nDefault Callback");
        break;
    }
    return 0;
}

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI( TAG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI( TAG, "registering characteristic %s with "
                    "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGI( TAG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

int gatt_svr_init(void)
{
    int rc = 0;
    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(new_ble_svc_gatt_defs);

    if( rc != 0)
    {
        return rc;
    }

    rc = ble_gatts_add_svcs(new_ble_svc_gatt_defs);
    if( rc != 0)
    {
        return rc;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */

void ble_server_setup( void )
{
    int rc = 0;

    esp_err_t ret = nimble_port_init();
    if( ret != ESP_OK)
    {
        ESP_LOGE( TAG, "Failed to init nimble %d \n", ret);
        return;
    }

    /* Initialize connection_handle array */
    for( int i = 0; i <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
    {
        conn_handle_subs[i] = false;
    }

    // Initialize the NimBLE
    ble_hs_cfg.reset_cb = ble_spp_server_on_reset;
    ble_hs_cfg.sync_cb = ble_spp_server_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // TODO: Work out what this value means?
    ble_hs_cfg.sm_io_cap = 3;
#ifdef CONFIG_EXAMPLE_BONDING
    ble_hs_cfg.sm_bonding = 1;
#endif
#ifdef CONFIG_EXAMPLE_MITM
    ble_hs_cfg.sm_mitm = 1;
#endif
#ifdef CONFIG_EXAMPLE_USE_SC
    ble_hs_cfg.sm_sc = 1;
#else
    ble_hs_cfg.sm_sc = 0;
#endif
#ifdef CONFIG_EXAMPLE_BONDING
    ble_hs_cfg.sm_our_key_dist = 1;
    ble_hs_cfg.sm_their_key_dist = 1;
#endif

    /* Register custom service */
    rc = gatt_svr_init();
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("nimble-ble-spp-svr");
    assert(rc == 0);

    ble_att_set_preferred_mtu(MTU_DESIRED);

    // ble_store_config_init();

    nimble_port_freertos_init(ble_spp_server_host_task);
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
   for( int i = 0; i <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++ )
   {
        // Check if client has subscribed to notifications
        if( conn_handle_subs[i] && length <= (spp_mtu_size - 3) )
        {
            struct os_mbuf *txom;
            txom = ble_hs_mbuf_from_flat(buffer, length);

            if( ble_gatts_notify_custom(i, ble_spp_svc_gatt_read_val_handle, txom) )
            {
                ESP_LOGI(TAG, "Error in sending notification");
            }
        }
    }

}

/* -------------------------------------------------------------------------- */