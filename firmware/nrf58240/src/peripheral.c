/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Modified from the Nordic UART Service sample
 */

#include <stdlib.h>
#include <stdio.h>

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <soc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

#include <bluetooth/services/nus.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>

#include "peripheral.h"

/* -------------------------------------------------------------------------- */

struct k_msgq *user_periph_evt_queue;

/* -------------------------------------------------------------------------- */

#define LOG_MODULE_NAME peripheral_uart
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN	(sizeof(DEVICE_NAME) - 1)

#define MIN_CONN_INTERVAL   (6)		// (N * 1.25 ms)
#define MAX_CONN_INTERVAL   (24)	// (N * 1.25 ms)
#define CONN_LATENCY (0)
#define SUPERVISION_TIMEOUT (42)	// (N * 10 ms)

static struct bt_conn *current_conn;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

/* -------------------------------------------------------------------------- */

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Connected %s\n", addr);

	current_conn = bt_conn_ref(conn);

	struct bt_le_conn_param *conn_param = BT_LE_CONN_PARAM(	MIN_CONN_INTERVAL, 
															MAX_CONN_INTERVAL, 
															CONN_LATENCY,
															SUPERVISION_TIMEOUT );

	err = bt_conn_le_param_update(current_conn, conn_param);
	if (err) {
		LOG_WRN("ITVL exchange failed (err %d)", err);
	}

}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason %u)\n", addr, reason);

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected,
	.disconnected = disconnected,
};

static void ble_send_cb( struct bt_conn *conn )
{
	// printk("TxDone?\n");

	if( user_periph_evt_queue )
	{
		bench_event_t evt;
		bench_event_send_cb_t *send_cb = &evt.data.send_cb;
		evt.id = BENCH_SEND_CB;
		send_cb->bytes_sent = 0;
		k_msgq_put(user_periph_evt_queue, &evt, K_MSEC(10) );
	}
}

static void ble_receive_cb(struct bt_conn *conn, const uint8_t *const data,
			  uint16_t len)
{
	char addr[BT_ADDR_LE_STR_LEN] = {0};
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));
	// printk("Received %i data from: %s\n", len, addr);

    if( user_periph_evt_queue && len )
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

		// Copy the event into the queue for processing
    	k_msgq_put(user_periph_evt_queue, &evt, K_MSEC(10));
	}
}

static void ble_notify_enabled(enum bt_nus_send_status status )
{
	if(status == BT_NUS_SEND_STATUS_ENABLED)
	{
		printk("TX Notify Enabled\n");
	}
	else if(status == BT_NUS_SEND_STATUS_DISABLED)
	{
		printk("TX Notify Disabled\n");
	}
}

static struct bt_nus_cb nus_cb = {
	.received = ble_receive_cb,
	.sent = ble_send_cb,
	.send_enabled = ble_notify_enabled,
};

/* -------------------------------------------------------------------------- */

void peripheral_init(void)
{
    int err;

	err = bt_enable(NULL);

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}
	
	err = bt_nus_init(&nus_cb);
	if (err) {
		LOG_ERR("Failed to initialize UART service (err: %d)", err);
		return;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd,
			      ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	printk("Bluetooth initialized\n");

}

/* -------------------------------------------------------------------------- */

void peripheral_send_payload( uint8_t *data, uint32_t length )
{
    if( current_conn )
    {
        uint8_t *buffer = malloc(length * sizeof(uint8_t));
        memcpy(buffer, data, length);
		// printk("Sending %iB\n", length);

        if( bt_nus_send(NULL, buffer, length) ) {
            LOG_WRN("Failed to send data over BLE connection");
        }

        free(buffer);
    }
}

/* -------------------------------------------------------------------------- */

void peripheral_register_user_evt_queue( struct k_msgq *queue )
{
    if( queue )
    {
        user_periph_evt_queue = queue;
    }
}

/* -------------------------------------------------------------------------- */