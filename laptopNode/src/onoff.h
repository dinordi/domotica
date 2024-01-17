#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/sys/printk.h>

#include <zephyr/settings/settings.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/byteorder.h>
// #include "board.h"




/* Generic OnOff Client */
static int gen_onoff_status(struct bt_mesh_model *model,
                             struct bt_mesh_msg_ctx *ctx, 
                             struct net_buf_simple *buf)
                             ;

static void button_pressedLED(struct k_work *work)
;
/** Send an OnOff Set message from the Generic OnOff Client to all nodes. */
static int gen_onoff_send(bool val);
/* Generic OnOff Server message handlers */

static int gen_onoff_get(struct bt_mesh_model *model,
			 struct bt_mesh_msg_ctx *ctx,
			 struct net_buf_simple *buf)
            ;
static int gen_onoff_set_unack(struct bt_mesh_model *model,
			       struct bt_mesh_msg_ctx *ctx,
			       struct net_buf_simple *buf)
                    ;
// static void gen_onoff_set(struct bt_mesh_model *model,
//         struct bt_mesh_msg_ctx *ctx,
//         struct net_buf_simple *buf)
//         {};




