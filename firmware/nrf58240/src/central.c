/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Modified from the Nordic UART Service Client sample
 */

#include <stdlib.h>

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/nus.h>
#include <bluetooth/services/nus_client.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include "central.h"

/* -------------------------------------------------------------------------- */

struct k_msgq *user_evt_queue;

bool ready_for_use = false;

/* -------------------------------------------------------------------------- */

#define MIN_CONN_INTERVAL   (6)		// (N * 1.25 ms)
#define MAX_CONN_INTERVAL   (24)	// (N * 1.25 ms)
#define CONN_LATENCY (0)
#define SUPERVISION_TIMEOUT (42)	// (N * 10 ms)

#define LOG_MODULE_NAME central_uart
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

K_SEM_DEFINE(nus_write_sem, 0, 1);

static struct bt_conn *default_conn;
static struct bt_nus_client nus_client;

/* -------------------------------------------------------------------------- */

static void ble_data_sent(struct bt_nus_client *nus, uint8_t err,
					const uint8_t *const data, uint16_t len)
{
	ARG_UNUSED(nus);

	// When sending we allocated a buffer.
	// Free it here. This definitely could be cleaner.
	free(data);       
	// printk("Sent %iB\n", len);

	if( user_evt_queue )
	{
		bench_event_t evt;
		bench_event_send_cb_t *send_cb = &evt.data.send_cb;
		evt.id = BENCH_SEND_CB;
		send_cb->bytes_sent = len;
		k_msgq_put(user_evt_queue, &evt, K_MSEC(10) );
	}

	k_sem_give(&nus_write_sem);

	if (err) {
		LOG_WRN("ATT error code: 0x%02X", err);
	}
}

static uint8_t ble_data_received(struct bt_nus_client *nus,
						const uint8_t *data, uint16_t len)
{
	ARG_UNUSED(nus);

	// int err;
	// printk("Rx %iB\n", len);

	// Post an event to the user-space event queue with the inbound data
	if( user_evt_queue && len )
	{
		bench_event_t evt;
		evt.id = BENCH_RECV_CB;
		bench_event_recv_cb_t *recv_cb = &evt.data.recv_cb;

		// Allocate a sufficiently large chunk of memory and copy the payload into it
		// User task is responsible for freeing this memory
		recv_cb->data = malloc( len );
		if( recv_cb->data == NULL )
		{
			LOG_WRN("Not able to allocate recv data buffer");
			return;
		}

		// Copy inbound data in
		memcpy(recv_cb->data, data, len);
		recv_cb->data_len = len;
		// printk("Enqueuing %iB\n", len);

		// Copy the event into the queue for processing
		k_msgq_put(user_evt_queue, &evt, K_MSEC(10) );

	}

	return BT_GATT_ITER_CONTINUE;
}

static void ble_unsubscribed(struct bt_nus_client *nus )
{
	printk("TX Notify Disabled\n");	
}

static void discovery_complete(struct bt_gatt_dm *dm,
			       void *context)
{
	struct bt_nus_client *nus = context;
	printk("Service discovery completed\n");

	bt_gatt_dm_data_print(dm);
	bt_nus_handles_assign(dm, nus);

	if (bt_nus_subscribe_receive(nus)) {
		printk("Subscribe failed\n");
	} else {
		printk("Subscribed\n");
	}

	bt_gatt_dm_data_release(dm);
	ready_for_use = true;
}

static void discovery_service_not_found(struct bt_conn *conn,
					void *context)
{
	printk("Service not found\n");
}

static void discovery_error(struct bt_conn *conn,
			    int err,
			    void *context)
{
	LOG_WRN("Error while discovering GATT database: (%d)", err);
}

struct bt_gatt_dm_cb discovery_cb = {
	.completed         = discovery_complete,
	.service_not_found = discovery_service_not_found,
	.error_found       = discovery_error,
};

static void gatt_discover(struct bt_conn *conn)
{
	int err;

	if (conn != default_conn) {
		return;
	}

	err = bt_gatt_dm_start(conn,
			       BT_UUID_NUS_SERVICE,
			       &discovery_cb,
			       &nus_client);
	if (err) {
		LOG_ERR("could not start the discovery procedure, error "
			"code: %d", err);
	}
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	printk("Connection parameters updated.\n"
	       " interval: %d, latency: %d, timeout: %d\n",
	       interval, latency, timeout);
}

static void le_phy_updated(struct bt_conn *conn,
			   struct bt_conn_le_phy_info *param)
{
	printk("LE PHY updated: TX PHY %s, RX PHY %s\n",
	       phy2str(param->tx_phy), phy2str(param->rx_phy));

}

static void le_data_length_updated(struct bt_conn *conn,
				   struct bt_conn_le_data_len_info *info)
{
	printk("LE data len updated: TX (len: %d time: %d)"
	       " RX (len: %d time: %d)\n", info->tx_max_len,
	       info->tx_max_time, info->rx_max_len, info->rx_max_time);
}


static void mtu_exchange_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
	if (!err) {
		printk("MTU exchange done\n");
	} else {
		LOG_WRN("MTU exchange failed (err %" PRIu8 ")", err);
	}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Failed to connect to %s (%d)\n", addr, conn_err);

		if (default_conn == conn) {
			bt_conn_unref(default_conn);
			default_conn = NULL;

			err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
			if (err) {
				LOG_ERR("Scanning failed to start (err %d)",
					err);
			}
		}

		return;
	}

	printk("Connected: %s\n", addr);

	static struct bt_gatt_exchange_params exchange_params;
	exchange_params.func = mtu_exchange_func;

	err = bt_gatt_exchange_mtu(conn, &exchange_params);
	if (err) {
		LOG_WRN("MTU exchange failed (err %d)", err);
	}

	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		LOG_WRN("Failed to set security: %d", err);

		gatt_discover(conn);
	}

	struct bt_le_conn_param *conn_param = BT_LE_CONN_PARAM(	MIN_CONN_INTERVAL, 
															MAX_CONN_INTERVAL, 
															CONN_LATENCY,
															SUPERVISION_TIMEOUT );

	err = bt_conn_le_param_update(default_conn, conn_param);
	if (err) {
		LOG_WRN("ITVL exchange failed (err %d)", err);
	}

	err = bt_scan_stop();
	if ((!err) && (err != -EALREADY)) {
		LOG_ERR("Stop LE scan failed (err %d)", err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	ready_for_use = false;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason %u)\n", addr, reason);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;
	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)",
			err);
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		LOG_WRN("Security failed: %s level %u err %d", addr,
			level, err);
	}

	gatt_discover(conn);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
	.le_param_updated = le_param_updated,
};

static void scan_filter_match(struct bt_scan_device_info *device_info,
			      struct bt_scan_filter_match *filter_match,
			      bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	printk("Filters matched. Address: %s connectable: %d\n",
		addr, connectable);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	LOG_WRN("Connecting failed");
}

static void scan_connecting(struct bt_scan_device_info *device_info,
			    struct bt_conn *conn)
{
	default_conn = bt_conn_ref(conn);
}

static int nus_client_init(void)
{
	int err;
	struct bt_nus_client_init_param init = {
		.cb = {
			.received = ble_data_received,
			.sent = ble_data_sent,
			.unsubscribed = ble_unsubscribed,
		}
	};

	err = bt_nus_client_init(&nus_client, &init);
	if (err) {
		LOG_ERR("NUS Client initialization failed (err %d)", err);
		return err;
	}

	printk("NUS Client module initialized\n");
	return err;
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL,
		scan_connecting_error, scan_connecting);

static int scan_init(void)
{
	int err;
	struct bt_scan_init_param scan_init = {
		.connect_if_match = 1,
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_NUS_SERVICE);
	if (err) {
		LOG_ERR("Scanning filters cannot be set (err %d)", err);
		return err;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		LOG_ERR("Filters cannot be turned on (err %d)", err);
		return err;
	}

	printk("Scan module initialized\n");
	return err;
}


static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}


static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}


static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_WRN("Pairing failed conn: %s, reason %d\n", addr, reason);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};

void central_init(void)
{
	int err;

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		LOG_ERR("Failed to register authorization callbacks.");
		return;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		printk("Failed to register authorization info callbacks.\n");
		return;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}
	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = scan_init();
	if (err != 0) {
		LOG_ERR("scan_init failed (err %d)", err);
		return;
	}

	err = nus_client_init();
	if (err != 0) {
		LOG_ERR("nus_client_init failed (err %d)", err);
		return;
	}

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	printk("Scanning successfully started\n");
}

/* -------------------------------------------------------------------------- */

#define NUS_WRITE_TIMEOUT K_MSEC(200)

void central_send_payload( uint8_t *data, uint32_t length )
{
	if( !ready_for_use )
	{ 
		return;
	}

	int err;
    uint8_t *buffer = malloc(length * sizeof(uint8_t));
    memcpy(buffer, data, length);

	err = bt_nus_client_send(&nus_client, buffer, length);
	if (err) {
		LOG_WRN("Failed to send data over BLE connection"
			"(err %d)", err);
		free(buffer);       
	}
	// printk("Sent?\n");

	err = k_sem_take(&nus_write_sem, NUS_WRITE_TIMEOUT);
	if (err) {
		LOG_WRN("NUS send timeout");
	}
}

/* -------------------------------------------------------------------------- */

void central_register_user_evt_queue( struct k_msgq *queue )
{
    if( queue )
    {
        user_evt_queue = queue;
    }
}


	

