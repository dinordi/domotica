#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/sys/printk.h>

#include <zephyr/settings/settings.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/byteorder.h>

void board_output_number(bt_mesh_output_action_t action, uint32_t number);

void board_prov_complete(void);

int board_init(struct k_work *button_work);

void board_led_set(bool val);

void printbombo();