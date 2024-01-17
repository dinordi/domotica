/*
 * Copyright (c) 2019 Tobias Svehagen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/printk.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/mesh/cfg_cli.h> //For adding subscribtions
#include <zephyr/bluetooth/mesh/cfg_srv.h> //For adding subscribtions

#include <zephyr/drivers/uart.h>

#include "/Users/dinordi/zephyrproject/zephyr/include/zephyr/bluetooth/mesh/cfg_cli.h"

#include "board.h"
// #include "onoff.h"

#define SW0_NODE	DT_ALIAS(sw0)
#define SW1_NODE	DT_ALIAS(sw1)

#define LED0_NODE DT_ALIAS(led0)
#define LED_GPIO_DEV_NAME DT_LABEL(DT_ALIAS(led_gpio))
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define OP_ONOFF_GET       BT_MESH_MODEL_OP_2(0x82, 0x01)
#define OP_ONOFF_SET       BT_MESH_MODEL_OP_2(0x82, 0x02)
#define OP_ONOFF_SET_UNACK BT_MESH_MODEL_OP_2(0x82, 0x03)
#define OP_ONOFF_STATUS    BT_MESH_MODEL_OP_2(0x82, 0x04)

static bool button_pressed_flag = false;
static const char *const onoff_str[] = { "off", "on" };
static struct {
	bool val;
	uint8_t tid;
	uint16_t src;
	uint32_t transition_time;
	struct k_work_delayable work;
} onoff;
static const uint16_t net_idx;
static const uint16_t app_idx;
static uint16_t self_addr = 1, node_addr;
static const uint8_t dev_uuid[16] = { 0xdd, 0xdd };
static uint8_t node_uuid[16];

K_SEM_DEFINE(sem_unprov_beacon, 0, 1);
K_SEM_DEFINE(sem_node_added, 0, 1);
#if DT_NODE_HAS_STATUS(SW0_NODE, okay)
K_SEM_DEFINE(sem_button_pressed, 0, 1);
#endif


extern const struct device *const uart_dev;

#define MSG_SIZE 32
int toggle = 0;
/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

/* receive buffer used in UART ISR callback */
static char rx_buf[MSG_SIZE];
static int rx_buf_pos;
char message_received[32];

void serial_cb(const struct device *dev, void *user_data)
{
    uint8_t c;
 
    if (!uart_irq_update(uart_dev)) {
        return;
    }
 
    if (!uart_irq_rx_ready(uart_dev)) {
        return;
    }
 
    /* read until FIFO empty */
    while (uart_fifo_read(uart_dev, &c, 1) == 1) {
        if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
            /* terminate string */
            rx_buf[rx_buf_pos] = '\0';
 
            /* if queue is full, message is silently dropped */
            k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);
 
            /* reset the buffer (it was copied to the msgq) */
            rx_buf_pos = 0;
        } else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
            rx_buf[rx_buf_pos++] = c;
        }
        /* else: characters beyond buffer size are dropped */
    }

	// char tx_buf[32];
	// 	while (k_msgq_get(&uart_msgq, &tx_buf, K_MSEC(100)) == 0) {
	
	// 		printk("buffer: %s", tx_buf);
	// 		if(strcmp(tx_buf, "BTN") == 0)
	// 		{
	// 			toggleLed();
	// 			toggleLocalLED(1);
	// 		}
	// 		// if(strcmp(tx_buf, "BTN") == 0)
	// 		// {
	// 		// 	toggleLed();
	// 		// }
	// 	}
}




static struct bt_mesh_cfg_cli cfg_cli = {
};

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

static void gen_onoff_set(struct bt_mesh_model *model,
        struct bt_mesh_msg_ctx *ctx,
        struct net_buf_simple *buf);

static int gen_onoff_get(struct bt_mesh_model *model,
			 struct bt_mesh_msg_ctx *ctx,
			 struct net_buf_simple *buf);
static int gen_onoff_status(struct bt_mesh_model *model,
			    struct bt_mesh_msg_ctx *ctx,
			    struct net_buf_simple *buf);
static int gen_onoff_set_unack(struct bt_mesh_model *model,
			       struct bt_mesh_msg_ctx *ctx,
			       struct net_buf_simple *buf);
static int gen_onoff_send(bool val);

static void health_current_status(struct bt_mesh_health_cli *cli, uint16_t addr,
				  uint8_t test_id, uint16_t cid, uint8_t *faults,
				  size_t fault_count)
{
	size_t i;

	printk("Health Current Status from 0x%04x\n", addr);

	if (!fault_count) {
		printk("Health Test ID 0x%02x Company ID 0x%04x: no faults\n",
		       test_id, cid);
		return;
	}

	printk("Health Test ID 0x%02x Company ID 0x%04x Fault Count %zu:\n",
	       test_id, cid, fault_count);

	for (i = 0; i < fault_count; i++) {
		printk("\t0x%02x\n", faults[i]);
	}
}

static struct bt_mesh_health_cli health_cli = {
	.current_status = health_current_status,
};
static const struct bt_mesh_model_op gen_onoff_srv_op[] = {
	{ OP_ONOFF_GET,       BT_MESH_LEN_EXACT(0), gen_onoff_get },
	{ OP_ONOFF_SET,       BT_MESH_LEN_MIN(2),   gen_onoff_set },
	{ OP_ONOFF_SET_UNACK, BT_MESH_LEN_MIN(2),   gen_onoff_set_unack },
	BT_MESH_MODEL_OP_END,
};
BT_MESH_MODEL_PUB_DEFINE(gen_onoff_pub_cli, NULL, 2 + 2);


static const struct bt_mesh_model_op gen_onoff_cli_op[] = {
	{OP_ONOFF_STATUS, BT_MESH_LEN_MIN(1), gen_onoff_status},
	BT_MESH_MODEL_OP_END,
};
static const struct bt_mesh_model root_models[] = {
	BT_MESH_MODEL_CFG_SRV,
	BT_MESH_MODEL_CFG_CLI(&cfg_cli),
	BT_MESH_MODEL_HEALTH_CLI(&health_cli),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_SRV, gen_onoff_srv_op, NULL,
		      NULL),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_CLI, gen_onoff_cli_op, &gen_onoff_pub_cli,
		      &onoff),
};

static const struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, root_models, BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp mesh_comp = {
	.cid = BT_COMP_ID_LF,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static void setup_cdb(void)
{
	struct bt_mesh_cdb_app_key *key;
	uint8_t app_key[16];
	int err;

	key = bt_mesh_cdb_app_key_alloc(net_idx, app_idx);
	if (key == NULL) {
		printk("Failed to allocate app-key 0x%04x\n", app_idx);
		return;
	}

	bt_rand(app_key, 16);

	err = bt_mesh_cdb_app_key_import(key, 0, app_key);
	if (err) {
		printk("Failed to import appkey into cdb. Err:%d\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		bt_mesh_cdb_app_key_store(key);
	}
}

static void configure_self(struct bt_mesh_cdb_node *self)
{
	struct bt_mesh_cdb_app_key *key;
	uint8_t app_key[16];
	uint8_t status = 0;
	int err;

	printk("Configuring self...\n");

	key = bt_mesh_cdb_app_key_get(app_idx);
	if (key == NULL) {
		printk("No app-key 0x%04x\n", app_idx);
		return;
	}

	err = bt_mesh_cdb_app_key_export(key, 0, app_key);
	if (err) {
		printk("Failed to export appkey from cdb. Err:%d\n", err);
		return;
	}

	/* Add Application Key */
	err = bt_mesh_cfg_cli_app_key_add(self->net_idx, self->addr, self->net_idx, app_idx,
					  app_key, &status);
	if (err || status) {
		printk("Failed to add app-key (err %d, status %d)\n", err,
		       status);
		return;
	}

	err = bt_mesh_cfg_cli_mod_app_bind(self->net_idx, self->addr, self->addr, app_idx,
					   BT_MESH_MODEL_ID_HEALTH_CLI, &status);
	if (err || status) {
		printk("Failed to bind app-key (err %d, status %d)\n", err,
		       status);
		return;
	}
	/* Bind to Generic OnOff Server model */
	err = bt_mesh_cfg_cli_mod_app_bind(self->net_idx, self->addr, self->addr, app_idx,
									BT_MESH_MODEL_ID_GEN_ONOFF_SRV, &status);
	if (err || status) {
		printk("Failed to bind app-key to Generic OnOff Server model (err %d, status %d)\n", err, status);
		return;
	}
	/* Bind to Generic OnOff Server model */
	err = bt_mesh_cfg_cli_mod_app_bind(self->net_idx, self->addr, self->addr, app_idx,
									BT_MESH_MODEL_ID_GEN_ONOFF_CLI, &status);
	if (err || status) {
		printk("Failed to bind app-key to Generic OnOff Client model (err %d, status %d)\n", err, status);
		return;
	}

	atomic_set_bit(self->flags, BT_MESH_CDB_NODE_CONFIGURED);

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		bt_mesh_cdb_node_store(self);
	}
	uint16_t mod_id; /* Model identifier */
	/* Add a subscription to the group address for the Generic OnOff Server model */
	mod_id = BT_MESH_MODEL_ID_GEN_ONOFF_SRV;
	err = bt_mesh_cfg_cli_mod_sub_add(net_idx, self->addr, self->addr, 0xC001, mod_id, &status);
	if (err || status) {
		printk("Failed to add subscription for Server model (err %d, status %d)\n", err, status);
		return;
	}

	/* Add a subscription to the group address for the Generic OnOff Client model */
	mod_id = BT_MESH_MODEL_ID_GEN_ONOFF_CLI;
	err = bt_mesh_cfg_cli_mod_sub_add(net_idx, self->addr, self->addr, 0xC001, mod_id, &status);
	if (err || status) {
		printk("Failed to add subscription for Client model (err %d, status %d)\n", err, status);
		return;
	}

	printk("Configuration complete\n");
}

static void gen_onoff_set(struct bt_mesh_model *model,
        struct bt_mesh_msg_ctx *ctx,
        struct net_buf_simple *buf)
{
    // uint8_t onoff_state = net_buf_simple_pull_u8(buf);

	// /* Toggle the LED based on the onoff_state */
	// if (onoff_state) {
	// 	gpio_pin_set_dt(&led, 1);
	// 	printk("LED ON\n");
	// } else {
	// 	gpio_pin_set_dt(&led, 0);
	// 	printk("LED OFF\n");
	// }

	// /* Prepare a response */
	// NET_BUF_SIMPLE_DEFINE(msg, 2 + 1 + 4);
	// bt_mesh_model_msg_init(&msg, BT_MESH_MODEL_OP_2(0x82, 0x04));

	// /* Add the OnOff state to the message */
	// net_buf_simple_add_u8(&msg, onoff_state);

	// /* Send the response */
	// bt_mesh_model_send(model, ctx, &msg, NULL, NULL);
	uint8_t onoff_state = net_buf_simple_pull_u8(buf);

	printk("Ledje toggle\n");
	if(toggle == 0){
        print_uart("3");
        toggle = 1;
        k_cycle_get_32();
        return;
    }
    if(toggle == 1)
    {
        print_uart("4");
        toggle = 0;   
        k_cycle_get_32();
        return;
    }

	/* Prepare a response */
	NET_BUF_SIMPLE_DEFINE(msg, 2 + 1 + 4);
	bt_mesh_model_msg_init(&msg, BT_MESH_MODEL_OP_2(0x82, 0x04));

	/* Add the OnOff state to the message */
	net_buf_simple_add_u8(&msg, onoff_state);

	/* Send the response */
	bt_mesh_model_send(model, ctx, &msg, NULL, NULL);
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
		// toggleLocalLED(1);
		(void)gen_onoff_send(!onoff.val);
	}
	

	return 0;
}


/** Send an OnOff Set message from the Generic OnOff Client to all nodes. */
static int gen_onoff_send(bool val)
{
	struct bt_mesh_msg_ctx ctx = {
		.app_idx = root_models[3].keys[0], /* Use the bound key */
		.addr = root_models[3].groups[0],
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

	printk("Sending OnOff Set: %s To address: %d\n", onoff_str[val], root_models[3].groups[0]);

	return bt_mesh_model_send(&root_models[3], &ctx, &buf, NULL, NULL);
}


static int gen_onoff_get(struct bt_mesh_model *model,
			 struct bt_mesh_msg_ctx *ctx,
			 struct net_buf_simple *buf)
{
    printk("Get received!\n");
	// onoff_status_send(model, ctx);
	return 0;
};
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

	// toggleLed();
	printk("Ledje toggle\n");
	if(toggle == 0){
        print_uart("3");
        toggle = 1;
        k_cycle_get_32();
        return;
    }
    if(toggle == 1)
    {
        print_uart("4");
		
        toggle = 0;   
        k_cycle_get_32();
        return;
    }

	onoff.tid = tid;
	onoff.src = ctx->addr;
	onoff.val = val;
	onoff.transition_time = trans;

	/* Schedule the next action to happen on the delay, and keep
	 * transition time stored, so it can be applied in the timeout.
	 */
	// k_work_reschedule(&onoff.work, K_MSEC(delay));

	return 0;
};





static void button_pressedLED(struct k_work *work)
{
	printk("BOMBOCLART\n");
	return;
	if (bt_mesh_is_provisioned()) {
		struct bt_mesh_msg_ctx ctx = {
			.app_idx = root_models[3].keys[0], /* Use the bound key */
			// .addr = models[3].pub->addr, //Use the publication address
			.addr = root_models[3].groups[0], //Use the subscription address
			.send_ttl = BT_MESH_TTL_DEFAULT,
		};
		// subscribe_to_group(0xC004, 0x0018);

		BT_MESH_MODEL_BUF_DEFINE(buf, OP_ONOFF_GET, 2);
		bt_mesh_model_msg_init(&buf, OP_ONOFF_GET);

		int err = bt_mesh_model_send(&root_models[3], &ctx, &buf, NULL, NULL);
		if (err) {
			printk("Unable to send OnOff Get message (err %d)\n", err);
		}
		printk("sent onoff get to address: %d\n", ctx.addr);
		button_pressed_flag = true;
		// light = !light;
		// gen_onoff_send(light);

		return;
	}
}

static uint16_t addresses[5];
static uint8_t print_node_addr(struct bt_mesh_cdb_node *node, void *data)
{
    printk("Node address: 0x%04x\n", node->addr);
	
    return BT_MESH_CDB_ITER_CONTINUE;
}

void print_all_node_addresses(void)
{
    bt_mesh_cdb_node_foreach(print_node_addr, NULL);
}

static uint16_t get_model_subscriptions(uint16_t addr, uint16_t elem_addr, uint16_t mod_id)
{
	uint16_t net_idx = 0; /* Network index */
	uint8_t status; /* Status of the operation */
	uint16_t subs[8]; /* Array to hold the subscription addresses */
	size_t subs_count; /* Number of subscription addresses */
	int err;

	/* Get the subscription list for the model */
	err = bt_mesh_cfg_cli_mod_sub_get(net_idx, addr, elem_addr, BT_MESH_MODEL_ID_GEN_ONOFF_SRV, &status, subs, &subs_count);
	if (err || status) {
		printk("Failed to get subscription list for model (err %d, status %d)\n", err, status);
		return NULL;
	}
	if (subs_count == 0) {
		printk("No subscriptions for model\n");
		return NULL;
	}
	printk("Model subscribed to addresses:\n");
	for (size_t i = 0; i < subs_count; i++) {
		printk("0x%04x\n", subs[i]);
	}
	return subs[0];
}

static void change_model_subscription(uint16_t addr, uint16_t elem_addr, uint16_t mod_id, uint16_t group_addr, bool add)
{
	uint16_t net_idx = 0; /* Network index */
	uint8_t status; /* Status of the operation */
	int err;

	if(add)
	{
		/* Add a subscription to the group address for the Generic OnOff Server model */
		err = bt_mesh_cfg_cli_mod_sub_add(net_idx, addr, elem_addr, group_addr, mod_id, &status);
		if (err || status) {
			printk("Failed to add subscription for Server model (err %d, status %d)\n", err, status);
			return;
		}
	}
	else
	{
		/* Remove a subscription to the group address for the Generic OnOff Server model */
		err = bt_mesh_cfg_cli_mod_sub_del(net_idx, addr, elem_addr, group_addr, mod_id, &status);
		if (err || status) {
			printk("Failed to remove subscription for Server model (err %d, status %d)\n", err, status);
			return;
		}
	}
}


static uint16_t print_model_subscriptions(uint16_t addr, uint16_t elem_addr, uint16_t mod_id, const char *model_name)
{
    uint16_t net_idx = 0; /* Network index */
    uint8_t status; /* Status of the operation */
    uint16_t subs[8]; /* Array to hold the subscription addresses */
    size_t subs_count; /* Number of subscription addresses */
    int err;

    /* Get the subscription list for the model */
    err = bt_mesh_cfg_cli_mod_sub_get(net_idx, addr, elem_addr, mod_id, &status, subs, &subs_count);
    if (err || status) {
        printk("Failed to get subscription list for %s model (err %d, status %d)\n", model_name, err, status);
        return;
    }
	if (subs_count == 0) {
		printk("No subscriptions for %s model\n", model_name);
		return;
	}
    /* Print the subscription addresses */
    for (size_t i = 0; i < subs_count; i++) {
        printk("%s model subscribed to address: 0x%04x\n", model_name, subs[i]);
    }
	return subs[0];
}

void print_all_subscriptions(uint16_t address)
{
	uint16_t primary_addr = bt_mesh_primary_addr();
	uint16_t elem_addr;
	uint16_t elem_index = 0; /* Index of the element */

	printk("self_addr: 0x%04x\n", self_addr);
	// struct bt_mesh_elem *elem = bt_mesh_elem_find(address);
	// if (!elem) {
	// 	printk("Element not found\n");
	// 	return;
	// }
	elem_addr = primary_addr + elem_index;
	uint16_t sub;
	sub = print_model_subscriptions(address, address, BT_MESH_MODEL_ID_GEN_ONOFF_SRV, "Generic OnOff Server");
	printk("sub: 0x%04x\n", sub);
	sub = print_model_subscriptions(address, address, BT_MESH_MODEL_ID_GEN_ONOFF_CLI, "Generic OnOff Client");
	printk("sub: 0x%04x\n", sub);
}
static void configure_node(struct bt_mesh_cdb_node *node)
{
	NET_BUF_SIMPLE_DEFINE(buf, BT_MESH_RX_SDU_MAX);
	struct bt_mesh_comp_p0_elem elem;
	struct bt_mesh_cdb_app_key *key;
	uint8_t app_key[16];
	struct bt_mesh_comp_p0 comp;
	uint8_t status;
	int err, elem_addr;

	printk("Configuring node 0x%04x...\n", node->addr);

	key = bt_mesh_cdb_app_key_get(app_idx);
	if (key == NULL) {
		printk("No app-key 0x%04x\n", app_idx);
		return;
	}

	err = bt_mesh_cdb_app_key_export(key, 0, app_key);
	if (err) {
		printk("Failed to export appkey from cdb. Err:%d\n", err);
		return;
	}

	/* Add Application Key */
	err = bt_mesh_cfg_cli_app_key_add(net_idx, node->addr, net_idx, app_idx, app_key, &status);
	if (err || status) {
		printk("Failed to add app-key (err %d status %d)\n", err, status);
		return;
	}

	/* Get the node's composition data and bind all models to the appkey */
	err = bt_mesh_cfg_cli_comp_data_get(net_idx, node->addr, 0, &status, &buf);
	if (err || status) {
		printk("Failed to get Composition data (err %d, status: %d)\n",
		       err, status);
		return;
	}

	err = bt_mesh_comp_p0_get(&comp, &buf);
	if (err) {
		printk("Unable to parse composition data (err: %d)\n", err);
		return;
	}

	elem_addr = node->addr;
	while (bt_mesh_comp_p0_elem_pull(&comp, &elem)) {
		printk("Element @ 0x%04x: %u + %u models\n", elem_addr,
		       elem.nsig, elem.nvnd);
		for (int i = 0; i < elem.nsig; i++) {
			uint16_t id = bt_mesh_comp_p0_elem_mod(&elem, i);

			if (id == BT_MESH_MODEL_ID_CFG_CLI ||
			    id == BT_MESH_MODEL_ID_CFG_SRV) {
				continue;
			}
			printk("Binding AppKey to model 0x%03x:%04x\n",
			       elem_addr, id);

			err = bt_mesh_cfg_cli_mod_app_bind(net_idx, node->addr, elem_addr, app_idx,
							   id, &status);
			if (err || status) {
				printk("Failed (err: %d, status: %d)\n", err,
				       status);
			}
		}

		for (int i = 0; i < elem.nvnd; i++) {
			struct bt_mesh_mod_id_vnd id =
				bt_mesh_comp_p0_elem_mod_vnd(&elem, i);

			printk("Binding AppKey to model 0x%03x:%04x:%04x\n",
			       elem_addr, id.company, id.id);

			err = bt_mesh_cfg_cli_mod_app_bind_vnd(net_idx, node->addr, elem_addr,
							       app_idx, id.id, id.company, &status);
			if (err || status) {
				printk("Failed (err: %d, status: %d)\n", err,
				       status);
			}
		}

		uint16_t mod_id; /* Model identifier */
		/* Add a subscription to the group address for the Generic OnOff Server model */
		mod_id = BT_MESH_MODEL_ID_GEN_ONOFF_SRV;
		err = bt_mesh_cfg_cli_mod_sub_add(net_idx, node->addr, elem_addr, 0xC001, mod_id, &status);
		if (err || status) {
			printk("Failed to add subscription for Server model (err %d, status %d)\n", err, status);
			return;
		}

		/* Add a subscription to the group address for the Generic OnOff Client model */
		mod_id = BT_MESH_MODEL_ID_GEN_ONOFF_CLI;
		err = bt_mesh_cfg_cli_mod_sub_add(net_idx, node->addr, elem_addr, 0xC001, mod_id, &status);
		if (err || status) {
			printk("Failed to add subscription for Client model (err %d, status %d)\n", err, status);
			return;
		}


		elem_addr++;
	}

	atomic_set_bit(node->flags, BT_MESH_CDB_NODE_CONFIGURED);

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		bt_mesh_cdb_node_store(node);
	}

	printk("Configuration complete\n");
}

static void unprovisioned_beacon(uint8_t uuid[16],
				 bt_mesh_prov_oob_info_t oob_info,
				 uint32_t *uri_hash)
{
	memcpy(node_uuid, uuid, 16);
	k_sem_give(&sem_unprov_beacon);
}

static void node_added(uint16_t idx, uint8_t uuid[16], uint16_t addr, uint8_t num_elem)
{
	node_addr = addr;
	k_sem_give(&sem_node_added);
}

static const struct bt_mesh_prov prov = {
	.uuid = dev_uuid,
	.unprovisioned_beacon = unprovisioned_beacon,
	.node_added = node_added,
};

static int bt_ready(void)
{
	uint8_t net_key[16], dev_key[16];
	int err;

	err = bt_mesh_init(&prov, &mesh_comp);
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return err;
	}

	printk("Mesh initialized\n");

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		printk("Loading stored settings\n");
		settings_load();
	}

	bt_rand(net_key, 16);

	err = bt_mesh_cdb_create(net_key);
	if (err == -EALREADY) {
		printk("Using stored CDB\n");
	} else if (err) {
		printk("Failed to create CDB (err %d)\n", err);
		return err;
	} else {
		printk("Created CDB\n");
		setup_cdb();
	}

	bt_rand(dev_key, 16);

	err = bt_mesh_provision(net_key, BT_MESH_NET_PRIMARY, 0, 0, self_addr,
				dev_key);
	if (err == -EALREADY) {
		printk("Using stored settings\n");
	} else if (err) {
		printk("Provisioning failed (err %d)\n", err);
		return err;
	} else {
		printk("Provisioning completed\n");
	}

	return 0;
}

static uint8_t check_unconfigured(struct bt_mesh_cdb_node *node, void *data)
{
	if (!atomic_test_bit(node->flags, BT_MESH_CDB_NODE_CONFIGURED)) {
		if (node->addr == self_addr) {
			configure_self(node);
		} else {
			configure_node(node);
		}
	}

	return BT_MESH_CDB_ITER_CONTINUE;
}

#if DT_NODE_HAS_STATUS(SW0_NODE, okay)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
static struct gpio_callback button_cb_data;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_sem_give(&sem_button_pressed);
}

static void button_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n", button.port->name);
		return;
	}
	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n", ret, button.port->name,
		       button.pin);
		return;
	}
	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n", ret,
		       button.port->name, button.pin);
		return;
	}
	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
}
#endif
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

		// k_work_reschedule(&onoff.work, K_MSEC(onoff.transition_time));
		onoff.transition_time = 0;
		return;
	}

	board_led_set(onoff.val);
}

void toggleLocalLED(int val)
{
	if(val == 0){
		gpio_pin_set_dt(&led, 1);
		return;
	}
	if(val == 1)
	{
		gpio_pin_set_dt(&led, 0);
		return;
	}
}
void toggleLed()
{
	struct bt_mesh_msg_ctx ctx = {
		.app_idx = root_models[3].keys[0], /* Use the bound key */
		// .addr = root_models[3].pub->addr, //Use the publication address
		.addr = root_models[3].groups[0], //Use the subscription address
		.send_ttl = BT_MESH_TTL_DEFAULT,
	};

	BT_MESH_MODEL_BUF_DEFINE(buf, OP_ONOFF_GET, 2);
	bt_mesh_model_msg_init(&buf, OP_ONOFF_GET);

	int err = bt_mesh_model_send(&root_models[3], &ctx, &buf, NULL, NULL);
	if (err) {
		printk("Unable to send OnOff Get message (err %d)\n", err);
	}
	// printk("sent onoff get to address: %d\n", ctx.addr);
	button_pressed_flag = true;
	return;	
}


int main(void)
{
	static struct k_work button_work;

	char uuid_hex_str[32 + 1];
	int err;
	
	printk("Initializing...\n");
	if (!device_is_ready(uart_dev)) {
        printk("UART device not found!");
        return 0;
    }
	/* configure interrupt and callback to receive data */
    int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
 
    if (ret < 0) {
        if (ret == -ENOTSUP) {
            printk("Interrupt-driven UART API support not enabled\n");
        } else if (ret == -ENOSYS) {
            printk("UART device does not support interrupt-driven API\n");
        } else {
            printk("Error setting UART callback: %d\n", ret);
        }
        return 0;
    }
    uart_irq_rx_enable(uart_dev);
	err = board_init(&button_work);
	if (err) {
		return err;
	}

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	// int toggles = 100;
	// for(int i = 0; i < toggles; i++)
	// {
	// 	if(i%2 == 0)
	// 	{
	// 		board_led_set(true);	
	// 	}
	// 	else
	// 	{
	// 		board_led_set(false);
	// 	}
	// 	k_msleep(100);
	// }
	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");
	bt_ready();

#if DT_NODE_HAS_STATUS(SW0_NODE, okay)
	button_init();
#endif

	while (1) {
		k_sem_reset(&sem_unprov_beacon);
		k_sem_reset(&sem_node_added);
		bt_mesh_cdb_node_foreach(check_unconfigured, NULL);

		// printk("Waiting for unprovisioned beacon...\n");
		// print_all_node_addresses();
		// print_all_subscriptions();

		// toggleLed();
		char tx_buf[32];
		while (k_msgq_get(&uart_msgq, &tx_buf, K_MSEC(100)) == 0) {
	
			printk("buffer: %s", tx_buf);
			if(strcmp(tx_buf, "BTN") == 0)
			{
				toggleLed();
				toggleLocalLED(1);
			}
			uint16_t address;
			uint16_t subIN;
			if (sscanf(tx_buf, "BTN0x%xSUB0x%x", &address, &subIN) == 2) {
			printk("Address: 0x%04x\n", address);
			uint16_t sub = print_model_subscriptions(address, address, BT_MESH_MODEL_ID_GEN_ONOFF_SRV, "Generic OnOff Server");
			printk("Sub: %04x\n", sub);
			change_model_subscription(address, address, BT_MESH_MODEL_ID_GEN_ONOFF_SRV, sub, false);//Remove subscription
			change_model_subscription(address, address, BT_MESH_MODEL_ID_GEN_ONOFF_CLI, sub, false);

			change_model_subscription(address, address, BT_MESH_MODEL_ID_GEN_ONOFF_SRV, subIN, true);//Add subscription
			change_model_subscription(address, address, BT_MESH_MODEL_ID_GEN_ONOFF_CLI, subIN, true);

			// Use the address...
			}
			if (sscanf(tx_buf, "RARA0x%04x", &address) == 1) {
			printk("Address: 0x%04x\n", address);
			print_all_subscriptions(address);
			// Use the address...
			}



			// if(strcmp(tx_buf, "BTN") == 0)
			// {
			// 	toggleLed();
			// }
		}
		err = k_sem_take(&sem_unprov_beacon, K_MSEC(100));
		if (err == -EAGAIN) {
			continue;
		}

		bin2hex(node_uuid, 16, uuid_hex_str, sizeof(uuid_hex_str));

#if DT_NODE_HAS_STATUS(SW0_NODE, okay)
		k_sem_reset(&sem_button_pressed);
		printk("Device %s detected, press button 1 to provision.\n", uuid_hex_str);
		err = k_sem_take(&sem_button_pressed, K_SECONDS(30));
		if (err == -EAGAIN) {
			printk("Timed out, button 1 wasn't pressed in time.\n");
			continue;
		}
#endif

		printk("Provisioning %s\n", uuid_hex_str);
		err = bt_mesh_provision_adv(node_uuid, net_idx, 0, 0);
		if (err < 0) {
			printk("Provisioning failed (err %d)\n", err);
			continue;
		}

		printk("Waiting for node to be added...\n");
		err = k_sem_take(&sem_node_added, K_SECONDS(10));
		if (err == -EAGAIN) {
			printk("Timeout waiting for node to be added\n");
			continue;
		}

		printk("Added node 0x%04x\n", node_addr);
	}
	return 0;
}
