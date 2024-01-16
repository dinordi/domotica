/* main.c - Application main entry point */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/printk.h>

#include <zephyr/settings/settings.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>

#include <zephyr/bluetooth/mesh/cfg_cli.h> //For adding subscribtions
#include <zephyr/bluetooth/mesh/cfg_srv.h> //For adding subscribtions

#include "/Users/dinordi/zephyrproject/zephyr/include/zephyr/bluetooth/mesh/cfg_cli.h"

#include "board.h"

#define OP_ONOFF_GET       BT_MESH_MODEL_OP_2(0x82, 0x01)
#define OP_ONOFF_SET       BT_MESH_MODEL_OP_2(0x82, 0x02)
#define OP_ONOFF_SET_UNACK BT_MESH_MODEL_OP_2(0x82, 0x03)
#define OP_ONOFF_STATUS    BT_MESH_MODEL_OP_2(0x82, 0x04)

#define LED0_NODE DT_ALIAS(led0)
#define LED_GPIO_DEV_NAME DT_LABEL(DT_ALIAS(led_gpio))
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

BT_MESH_MODEL_PUB_DEFINE(gen_onoff_pub_cli, NULL, 2 + 2);

static bool button_pressed_flag = false;
static int gen_onoff_send(bool val);


// #define LED_PORT DT_GPIO_LABEL(DT_ALIAS(led0), gpios)
// #define LED_PIN DT_GPIO_PIN(DT_ALIAS(led0), gpios)

static void attention_on(struct bt_mesh_model *mod)
{
	board_led_set(true);
}

static void attention_off(struct bt_mesh_model *mod)
{
	board_led_set(false);
}

static const struct bt_mesh_health_srv_cb health_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

static const char *const onoff_str[] = { "off", "on" };

static struct {
	bool val;
	uint8_t tid;
	uint16_t src;
	uint32_t transition_time;
	struct k_work_delayable work;
} onoff;

/* OnOff messages' transition time and remaining time fields are encoded as an
 * 8 bit value with a 6 bit step field and a 2 bit resolution field.
 * The resolution field maps to:
 * 0: 100 ms
 * 1: 1 s
 * 2: 10 s
 * 3: 20 min
 */
static const uint32_t time_res[] = {
	100,
	MSEC_PER_SEC,
	10 * MSEC_PER_SEC,
	10 * 60 * MSEC_PER_SEC,
};

static inline int32_t model_time_decode(uint8_t val)
{
	uint8_t resolution = (val >> 6) & BIT_MASK(2);
	uint8_t steps = val & BIT_MASK(6);

	if (steps == 0x3f) {
		return SYS_FOREVER_MS;
	}

	return steps * time_res[resolution];
}

static inline uint8_t model_time_encode(int32_t ms)
{
	if (ms == SYS_FOREVER_MS) {
		return 0x3f;
	}

	for (int i = 0; i < ARRAY_SIZE(time_res); i++) {
		if (ms >= BIT_MASK(6) * time_res[i]) {
			continue;
		}

		uint8_t steps = DIV_ROUND_UP(ms, time_res[i]);

		return steps | (i << 6);
	}

	return 0x3f;
}

static int onoff_status_send(struct bt_mesh_model *model,
			     struct bt_mesh_msg_ctx *ctx)
{
	uint32_t remaining;

	BT_MESH_MODEL_BUF_DEFINE(buf, OP_ONOFF_STATUS, 3);
	bt_mesh_model_msg_init(&buf, OP_ONOFF_STATUS);

	remaining = k_ticks_to_ms_floor32(
			    k_work_delayable_remaining_get(&onoff.work)) +
		    onoff.transition_time;

	/* Check using remaining time instead of "work pending" to make the
	 * onoff status send the right value on instant transitions. As the
	 * work item is executed in a lower priority than the mesh message
	 * handler, the work will be pending even on instant transitions.
	 */
	if (remaining) {
		net_buf_simple_add_u8(&buf, !onoff.val);
		net_buf_simple_add_u8(&buf, onoff.val);
		net_buf_simple_add_u8(&buf, model_time_encode(remaining));
	} else {
		net_buf_simple_add_u8(&buf, onoff.val);
	}

	return bt_mesh_model_send(model, ctx, &buf, NULL, NULL);
}

static void onoff_timeout(struct k_work *work)
{
	if (onoff.transition_time) {
		/* Start transition.
		 *
		 * The LED should be on as long as the transition is in
		 * progress, regardless of the target value, according to the
		 * Bluetooth Mesh Model specification, section 3.1.1.
		 */
		board_led_set(true);

		k_work_reschedule(&onoff.work, K_MSEC(onoff.transition_time));
		onoff.transition_time = 0;
		return;
	}

	board_led_set(onoff.val);
}

/* Generic OnOff Server message handlers */

static int gen_onoff_get(struct bt_mesh_model *model,
			 struct bt_mesh_msg_ctx *ctx,
			 struct net_buf_simple *buf)
{
	printk("Get received!\n");
	onoff_status_send(model, ctx);
	return 0;
}

static int gen_onoff_set_unack(struct bt_mesh_model *model,
			       struct bt_mesh_msg_ctx *ctx,
			       struct net_buf_simple *buf)
{
	uint8_t val = net_buf_simple_pull_u8(buf);
	uint8_t tid = net_buf_simple_pull_u8(buf);
	int32_t trans = 0;
	int32_t delay = 0;

	if (buf->len) {
		trans = model_time_decode(net_buf_simple_pull_u8(buf));
		delay = net_buf_simple_pull_u8(buf) * 5;
	}

	/* Only perform change if the message wasn't a duplicate and the
	 * value is different.
	 */
	if (tid == onoff.tid && ctx->addr == onoff.src) {
		/* Duplicate */
		return 0;
	}

	if (val == onoff.val) {
		/* No change */
		return 0;
	}

	printk("set: %s delay: %d ms time: %d ms\n", onoff_str[val], delay,
	       trans);

	onoff.tid = tid;
	onoff.src = ctx->addr;
	onoff.val = val;
	onoff.transition_time = trans;

	/* Schedule the next action to happen on the delay, and keep
	 * transition time stored, so it can be applied in the timeout.
	 */
	k_work_reschedule(&onoff.work, K_MSEC(delay));

	return 0;
}

static void gen_onoff_set(struct bt_mesh_model *model,
						  struct bt_mesh_msg_ctx *ctx,
						  struct net_buf_simple *buf)
{
	uint8_t onoff_state = net_buf_simple_pull_u8(buf);

	/* Toggle the LED based on the onoff_state */
	if (onoff_state) {
		gpio_pin_set_dt(&led, 1);
		printk("LED ON\n");
	} else {
		gpio_pin_set_dt(&led, 0);
		printk("LED OFF\n");
	}

	/* Prepare a response */
	NET_BUF_SIMPLE_DEFINE(msg, 2 + 1 + 4);
	bt_mesh_model_msg_init(&msg, BT_MESH_MODEL_OP_2(0x82, 0x04));

	/* Add the OnOff state to the message */
	net_buf_simple_add_u8(&msg, onoff_state);

	/* Send the response */
	bt_mesh_model_send(model, ctx, &msg, NULL, NULL);
}

static const struct bt_mesh_model_op gen_onoff_srv_op[] = {
	{ OP_ONOFF_GET,       BT_MESH_LEN_EXACT(0), gen_onoff_get },
	{ OP_ONOFF_SET,       BT_MESH_LEN_MIN(2),   gen_onoff_set },
	{ OP_ONOFF_SET_UNACK, BT_MESH_LEN_MIN(2),   gen_onoff_set_unack },
	BT_MESH_MODEL_OP_END,
};

/* Generic OnOff Client */
static int gen_onoff_status(struct bt_mesh_model *model,
			    struct bt_mesh_msg_ctx *ctx,
			    struct net_buf_simple *buf)
{
	uint8_t present = net_buf_simple_pull_u8(buf);

	if (buf->len) {
		uint8_t target = net_buf_simple_pull_u8(buf);
		int32_t remaining_time =
			model_time_decode(net_buf_simple_pull_u8(buf));

		printk("OnOff status: %s -> %s: (%d ms)\n", onoff_str[present],
		       onoff_str[target], remaining_time);
		return 0;
	}

	printk("gen_onoff_status: %s\n", onoff_str[present]);
	
	if(button_pressed_flag)
	{
		onoff.val = present;

		button_pressed_flag = false;
		printk("Button pressed\n");
		(void)gen_onoff_send(!onoff.val);
	}
	

	return 0;
}

static const struct bt_mesh_model_op gen_onoff_cli_op[] = {
	{OP_ONOFF_STATUS, BT_MESH_LEN_MIN(1), gen_onoff_status},
	BT_MESH_MODEL_OP_END,
};

static struct bt_mesh_cfg_cli cfg_cli;

/* This application only needs one element to contain its models */
static struct bt_mesh_model models[] = {
	BT_MESH_MODEL_CFG_SRV,
	BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_SRV, gen_onoff_srv_op, NULL,
		      NULL),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_CLI, gen_onoff_cli_op, &gen_onoff_pub_cli,
		      &onoff),
};

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, models, BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp comp = {
	.cid = BT_COMP_ID_LF,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

/* Provisioning */

static int output_number(bt_mesh_output_action_t action, uint32_t number)
{
	printk("OOB Number: %u\n", number);

	board_output_number(action, number);

	return 0;
}

static void prov_complete(uint16_t net_idx, uint16_t addr)
{
	board_prov_complete();
}

static void prov_reset(void)
{
	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
}

static uint8_t dev_uuid[16];

static const struct bt_mesh_prov prov = {
	.uuid = dev_uuid,
	.output_size = 4,
	.output_actions = BT_MESH_DISPLAY_NUMBER,
	.output_number = output_number,
	.complete = prov_complete,
	.reset = prov_reset,
};

/** Send an OnOff Set message from the Generic OnOff Client to all nodes. */
static int gen_onoff_send(bool val)
{
	struct bt_mesh_msg_ctx ctx = {
		.app_idx = models[3].keys[0], /* Use the bound key */
		.addr = models[3].groups[0],
		.send_ttl = BT_MESH_TTL_DEFAULT,
	};
	static uint8_t tid;

	if (ctx.app_idx == BT_MESH_KEY_UNUSED) {
		printk("The Generic OnOff Client must be bound to a key before "
		       "sending.\n");
		return -ENOENT;
	}

	BT_MESH_MODEL_BUF_DEFINE(buf, OP_ONOFF_SET_UNACK, 2);
	bt_mesh_model_msg_init(&buf, OP_ONOFF_SET_UNACK);
	net_buf_simple_add_u8(&buf, val);
	net_buf_simple_add_u8(&buf, tid++);

	printk("Sending OnOff Set: %s To address: %d\n", onoff_str[val], models[3].groups[0]);

	return bt_mesh_model_send(&models[3], &ctx, &buf, NULL, NULL);
}

void (*status_cb)(uint8_t status, uint16_t net_idx, uint16_t addr, uint16_t elem_addr, uint16_t sub_addr, uint16_t model_id);

void sub_add_status_cb(uint8_t status, uint16_t net_idx, uint16_t addr, uint16_t elem_addr, uint16_t sub_addr, uint16_t model_id)
{
	if (status == 0) {
		printk("Successfully added subscription\n");
	} else {
		printk("Failed to add subscription (status %d)\n", status);
	}
}


void subscribe_to_group(uint16_t group_addr, uint16_t target_node) {

	uint16_t unicast_addr = bt_mesh_primary_addr();
    uint16_t model_id = BT_MESH_MODEL_ID_GEN_ONOFF_CLI;
    
									//Net_index, target_node, element_addr, group_addr, model_id, status_cb
    int err = bt_mesh_cfg_cli_mod_sub_add(0, 0x001D, 0x001D, 0xC001, 0x1001, NULL);
    if (err) {
        printk("Error adding subscription (err %d)\n", err);
    }

}

void unsubscribe_from_group(uint16_t group_addr, uint16_t target_node) {
	uint16_t element_addr = bt_mesh_primary_addr();
    uint16_t app_idx = models[3].keys[0];
    uint16_t model_id = BT_MESH_MODEL_ID_GEN_ONOFF_CLI;
    uint16_t net_idx = bt_mesh_primary_addr();

    int err = bt_mesh_cfg_cli_mod_sub_add(net_idx, target_node, element_addr, group_addr, model_id, NULL);
    if (err) {
        printk("Error removing subscription (err %d)\n", err);
    }
}

static bool light = false;
static void button_pressed(struct k_work *work)
{
	if (bt_mesh_is_provisioned()) {
		struct bt_mesh_msg_ctx ctx = {
			.app_idx = models[3].keys[0], /* Use the bound key */
			// .addr = models[3].pub->addr, //Use the publication address
			.addr = models[3].groups[0], //Use the subscription address
			.send_ttl = BT_MESH_TTL_DEFAULT,
		};
		// subscribe_to_group(0xC004, 0x0018);

		BT_MESH_MODEL_BUF_DEFINE(buf, OP_ONOFF_GET, 2);
		bt_mesh_model_msg_init(&buf, OP_ONOFF_GET);

		int err = bt_mesh_model_send(&models[3], &ctx, &buf, NULL, NULL);
		if (err) {
			printk("Unable to send OnOff Get message (err %d)\n", err);
		}
		printk("sent onoff get to address: %d\n", ctx.addr);
		button_pressed_flag = true;
		// light = !light;
		// gen_onoff_send(light);

		return;
	}

	/* Self-provision with an arbitrary address.
	 *
	 * NOTE: This should never be done in a production environment.
	 *       Addresses should be assigned by a provisioner, and keys should
	 *       be generated from true random numbers. It is done in this
	 *       sample to allow testing without a provisioner.
	 */
	static uint8_t net_key[16];
	static uint8_t dev_key[16];
	static uint8_t app_key[16];
	uint16_t addr;
	int err;

	if (IS_ENABLED(CONFIG_HWINFO)) {
		addr = sys_get_le16(&dev_uuid[0]) & BIT_MASK(15);
	} else {
		addr = k_uptime_get_32() & BIT_MASK(15);
	}

	printk("Self-provisioning with address 0x%04x\n", addr);
	err = bt_mesh_provision(net_key, 0, 0, 0, addr, dev_key);
	if (err) {
		printk("Provisioning failed (err: %d)\n", err);
		return;
	}

	/* Add an application key to both Generic OnOff models: */
	err = bt_mesh_app_key_add(0, 0, app_key);
	if (err) {
		printk("App key add failed (err: %d)\n", err);
		return;
	}

	/* Models must be bound to an app key to send and receive messages with
	 * it:
	 */
	models[2].keys[0] = 0;
	models[3].keys[0] = 0;

	printk("Provisioned and configured!\n");
}

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	err = bt_mesh_init(&prov, &comp);
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/* This will be a no-op if settings_load() loaded provisioning info */
	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

	printk("Mesh initialized\n");
}

int main(void)
{
	static struct k_work button_work;
	int err = -1;

	printk("Initializing...\n");

	if (IS_ENABLED(CONFIG_HWINFO)) {
		err = hwinfo_get_device_id(dev_uuid, sizeof(dev_uuid));
	}

	if (err < 0) {
		dev_uuid[0] = 0xdd;
		dev_uuid[1] = 0xdd;
	}

	k_work_init(&button_work, button_pressed);

	err = board_init(&button_work);
	if (err) {
		printk("Board init failed (err: %d)\n", err);
		return 0;
	}

	k_work_init_delayable(&onoff.work, onoff_timeout);

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	int ret;
	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	// ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	// if (ret < 0) {
	// 	return 0;
	// }

	// while (1) {
	// 	ret = gpio_pin_toggle_dt(&led);
	// 	if (ret < 0) {
	// 		return 0;
	// 	}
	// 	k_msleep(1000);
	// }
	// return 0;


	return 0;
}
