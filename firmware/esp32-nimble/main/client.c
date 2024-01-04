/* -------------------------------------------------------------------------- */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "client.h"
#include "ble_main_defs.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_central.h"

/* -------------------------------------------------------------------------- */

static QueueHandle_t *user_evt_queue;

static const char *TAG = "NimBLE-C";

/* -------------------------------------------------------------------------- */

// Client stuff

#define PEER_ADDR_VAL_SIZE      6

struct ble_hs_adv_fields;
struct ble_gap_conn_desc;
struct ble_hs_cfg;
union ble_store_value;
union ble_store_key;

/* 16 Bit SPP Service UUID */
#define GATT_SPP_SVC_UUID                                  0xABF0

/* 16 Bit SPP Service Characteristic UUID */
#define GATT_SPP_CHR_UUID                                  0xABF1


#define LL_PACKET_TIME (2120)
#define LL_PACKET_LENGTH (200)
#define MTU_DESIRED (200)

/* -------------------------------------------------------------------------- */

static void ble_spp_client_set_handle(const struct peer *peer);
static void ble_spp_client_subscribe_spp(const struct peer *peer);
static void ble_spp_client_on_disc_complete(const struct peer *peer, int status, void *arg);
static void ble_spp_client_scan(void);
static int ble_spp_client_should_connect(const struct ble_gap_disc_desc *disc);
static void ble_spp_client_connect_if_interesting(const struct ble_gap_disc_desc *disc);
static int ble_spp_client_gap_event(struct ble_gap_event *event, void *arg);
static void ble_spp_client_on_reset(int reason);
static void ble_spp_client_on_sync(void);
void ble_spp_client_host_task(void *param);

/* -------------------------------------------------------------------------- */

// Not sure where this is being called, might be a library _WEAK function?
void ble_store_config_init(void);

/* -------------------------------------------------------------------------- */

uint16_t attribute_handle[CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1] = { 0 };
static ble_addr_t connected_addr[CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1] = { 0 };

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

/* -------------------------------------------------------------------------- */

// Logs information about a connection.
static void ble_spp_client_print_conn_desc(struct ble_gap_conn_desc *desc)
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


static void ble_spp_client_set_handle(const struct peer *peer)
{
    const struct peer_chr *chr;
    chr = peer_chr_find_uuid(peer,
                             BLE_UUID16_DECLARE(GATT_SPP_SVC_UUID),
                             BLE_UUID16_DECLARE(GATT_SPP_CHR_UUID));
    attribute_handle[peer->conn_handle] = chr->chr.val_handle;

    ESP_LOGI(TAG, "Set handle; chr val_handle=%d", chr->chr.val_handle );

}

static void ble_spp_client_subscribe_spp(const struct peer *peer)
{
    uint8_t value[2] = { 0 };
    const struct peer_dsc *dsc;

    dsc = peer_dsc_find_uuid(peer,
                             BLE_UUID16_DECLARE(GATT_SPP_SVC_UUID),
                             BLE_UUID16_DECLARE(GATT_SPP_CHR_UUID),
                             BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));
    if( dsc == NULL )
    {
        ESP_LOGE( TAG, "Peer lacks a CCCD for the subscribable characterstic");
        return;
    }

    // Write the subscription code 0x00 and 0x01 to the CCCD
    value[0] = 1;
    value[1] = 0;
    ble_gattc_write_flat( peer->conn_handle, 
                          dsc->dsc.handle,
                          value, 
                          sizeof(value), 
                          NULL, 
                          NULL
                         );

    ESP_LOGI(TAG, "Subscribed to SPP?");
}

// Called when service discovery of the specified peer has completed.
static void ble_spp_client_on_disc_complete(const struct peer *peer, int status, void *arg)
{
    if( status != 0)
    {
        // Service discovery failed.  Terminate the connection.
        ESP_LOGE(TAG, "Error: Service discovery failed; status=%d conn_handle=%d\n",
                status,
                peer->conn_handle
                );
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    /* Service discovery has completed successfully.  Now we have a complete
     * list of services, characteristics, and descriptors that the peer
     * supports.
     */
    ESP_LOGI(TAG, "Service discovery complete; status=%d conn_handle=%d\n", 
            status, 
            peer->conn_handle
            );

    ble_spp_client_set_handle(peer);
    ble_spp_client_subscribe_spp(peer);

    ble_hs_hci_util_set_data_len( peer->conn_handle,
                                  LL_PACKET_LENGTH,
                                  LL_PACKET_TIME );

    ble_gattc_exchange_mtu(peer->conn_handle, NULL, NULL);

    // Sets the client's BLE connection behaviours 
    // https://mynewt.apache.org/latest/network/ble_hs/ble_gap.html#c.ble_gap_update_params
    // ITVL uses 1.25 ms units
    // Timout is in 10ms units
    // CE LEN uses 0.625 ms units
    // BLE specifies minimum 7.5ms connection interval
    struct ble_gap_upd_params conn_parameters = { 0 };
    conn_parameters.itvl_min = 6;   // 7.5ms
    conn_parameters.itvl_max = 24;  // 30ms
    conn_parameters.latency = 0;
    conn_parameters.supervision_timeout = 20; 
    // https://github.com/apache/mynewt-nimble/issues/793#issuecomment-616022898
    conn_parameters.min_ce_len = 0x00;
    conn_parameters.max_ce_len = 0x00;

    ble_gap_update_params(peer->conn_handle, &conn_parameters);
    // ble_spp_client_scan();
}

/**
 * Initiates the GAP general discovery procedure.
 */
static void ble_spp_client_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params = { 0 };
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if( rc != 0)
    {
        ESP_LOGE(TAG, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      ble_spp_client_gap_event, NULL);
    if( rc != 0)
    {
        ESP_LOGE(TAG, "Error initiating GAP discovery procedure; rc=%d\n", rc);
    }
}

/**
 * Indicates whether we should try to connect to the sender of the specified
 * advertisement.  The function returns a positive result if the device
 * advertises connectability and support for the Alert Notification service.
 */
static int ble_spp_client_should_connect(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int rc;
    int i;

    /* Check if device is already connected or not */
    for( i = 0; i <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++ )
    {
        if( memcmp(&connected_addr[i].val, disc->addr.val, PEER_ADDR_VAL_SIZE) == 0)
        {
            ESP_LOGI(TAG, "Device already connected: ");
            return 0;
        }
    }

    /* The device has to be advertising connectability. */
    if( disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
            disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)
    {
        ESP_LOGI(TAG, "It isn't advertising connectability");
        return 0;
    }

    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if( rc != 0)
    {
        return 0;
    }

    /* The device has to advertise support for the SPP
     * service (0xABF0).
     */
    for( i = 0; i < fields.num_uuids16; i++ )
    {
        if( ble_uuid_u16(&fields.uuids16[i].u) == GATT_SPP_SVC_UUID)
        {
            ESP_LOGI(TAG, "Found SPP service!");
            return 1;
        }
    }
    return 0;
}

/**
 * Connects to the sender of the specified advertisement of it looks
 * interesting.  A device is "interesting" if it advertises connectability and
 * support for the Alert Notification service.
 */
static void ble_spp_client_connect_if_interesting(const struct ble_gap_disc_desc *disc)
{
    uint8_t own_addr_type;
    int rc;

    // Don't do anything if we don't care about this advertiser.
    if( !ble_spp_client_should_connect(disc) )
    {
        // ESP_LOGI(TAG, "Not interesting...\n");
        return;
    }

    // Scanning must be stopped before a connection can be initiated.
    rc = ble_gap_disc_cancel();
    if( rc != 0 )
    {
        ESP_LOGI(TAG, "Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    // Figure out address to use for connect (no privacy for now)
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if( rc != 0 )
    {
        ESP_LOGE(TAG, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout.
     */

    rc = ble_gap_connect(own_addr_type, &disc->addr, 30000, NULL,
                         ble_spp_client_gap_event, NULL);
    if( rc != 0)
    {
        char bda_str[18] = {0};
        ESP_LOGE(TAG, "Error: Failed to connect to device; addr_type=%d addr=%s; rc=%d\n",
                    disc->addr.type, 
                    bda2str(disc->addr.val, bda_str, sizeof(bda_str)),
                    rc
                );
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  ble_spp_client uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                                  ble_spp_client.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int ble_spp_client_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data);
        if( rc != 0)
        {
            return 0;
        }

        // An advertisment report was received during GAP discovery.
        print_adv_fields(&fields);

        // Try to connect to the advertiser if it looks interesting. 
        ble_spp_client_connect_if_interesting(&event->disc);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        // A new connection was established or a connection attempt failed.
        if( event->connect.status == 0)
        {
            // Connection successfully established.
            ESP_LOGI(TAG, "Connection established ");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            memcpy(&connected_addr[event->connect.conn_handle].val, desc.peer_id_addr.val,
                   PEER_ADDR_VAL_SIZE);
            // ble_spp_client_print_conn_desc(&desc);

            // Remember this peer.
            rc = peer_add(event->connect.conn_handle);
            if( rc != 0)
            {
                ESP_LOGE(TAG, "Failed to add peer; rc=%d\n", rc);
                return 0;
            }

            // Perform service discovery
            rc = peer_disc_all(event->connect.conn_handle,
                               ble_spp_client_on_disc_complete, NULL);
            if( rc != 0)
            {
                ESP_LOGE(TAG, "Failed to discover services; rc=%d\n", rc);
                return 0;
            }
        } 
        else 
        {
            /* Connection attempt failed; resume scanning. */
            ESP_LOGE(TAG, "Error: Connection failed; status=%d\n",
                        event->connect.status);
            ble_spp_client_scan();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated. */
        ESP_LOGI(TAG, "disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);

        /* Forget about peer. */
        memset(&connected_addr[event->disconnect.conn.conn_handle].val, 0, PEER_ADDR_VAL_SIZE);
        attribute_handle[event->disconnect.conn.conn_handle] = 0;
        peer_delete(event->disconnect.conn.conn_handle);

        /* Resume scanning. */
        ble_spp_client_scan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "discovery complete; reason=%d\n",
                    event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        // Peer sent us a notification or indication. 
        // ESP_LOGI(TAG, "received %s; conn_handle=%d attr_handle=%d "
        //             "attr_len=%d\n",
        //             event->notify_rx.indication ?
        //             "indication" :
        //             "notification",
        //             event->notify_rx.conn_handle,
        //             event->notify_rx.attr_handle,
        //             OS_MBUF_PKTLEN(event->notify_rx.om));

        // Post an event to the user-space event queue with the inbound data
        if( user_evt_queue )
        {
            spp_event_t evt;
            spp_event_recv_cb_t *recv_cb = &evt.data.recv_cb;
            evt.id = SPP_RECV_CB;

            // Allocate a sufficiently large chunk of memory and copy the payload into it
            // User task is responsible for freeing this memory
            recv_cb->data = malloc( event->notify_rx.om->om_len );
            if( recv_cb->data == NULL )
            {
                ESP_LOGE( TAG, "RX Malloc fail");
                return 0;
            }

            // Attribute data is contained in event->notify_rx.om.
            os_mbuf_copydata(event->notify_rx.om, 0, event->notify_rx.om->om_len, recv_cb->data);
            recv_cb->data_len = event->notify_rx.om->om_len;

            // Put the event into the queue for processing
            if( xQueueSend(*user_evt_queue, &evt, 512) != pdTRUE )
            {
                ESP_LOGW( TAG, "RX event failed to enqueue");
                free(recv_cb->data);
            }
        }

        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

/* -------------------------------------------------------------------------- */

static void ble_spp_client_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d\n", reason);
}

/* -------------------------------------------------------------------------- */

static void ble_spp_client_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Begin scanning for a peripheral to connect to. */
    ble_spp_client_scan();
}

/* -------------------------------------------------------------------------- */

void ble_spp_client_host_task(void *param)
{
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

/* -------------------------------------------------------------------------- */

void ble_client_setup( void )
{
    int rc;

    nimble_port_init();

    ble_hs_cfg.reset_cb = ble_spp_client_on_reset;
    ble_hs_cfg.sync_cb = ble_spp_client_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Initialize data structures to track connected peers.
    rc = peer_init( MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64 );
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("nimble-ble-spp-client");
    assert(rc == 0);

    // ble_store_config_init();

    // Set the preferred MTU size
    ble_att_set_preferred_mtu(200);

    nimble_port_freertos_init(ble_spp_client_host_task);
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
    // TODO: some kind of is_connected check first?
    for( int i = 0; i <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++ )
    {
        if( attribute_handle[i] != 0 )
        {
            if( ble_gattc_write_flat(i, attribute_handle[i], buffer, length, NULL, NULL) ) 
            {
                ESP_LOGI(TAG, "Error writing characteristic");
            }
        }
    }
}

/* -------------------------------------------------------------------------- */








