

#include <zephyr/bluetooth/mesh.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include "board.h"

/* Locate led0 as alias or label by that name */
#if DT_NODE_EXISTS(DT_ALIAS(led0))
#define LED0 DT_ALIAS(led0)
#elif DT_NODE_EXISTS(DT_NODELABEL(led0))
#define LED0 DT_NODELABEL(led0)
#else
#define LED0 DT_INVALID_NODE
#endif

/* Locate button0 as alias or label by sw1 or button0 */
#if DT_NODE_EXISTS(DT_ALIAS(sw1))
#define BUTTON0 DT_ALIAS(sw1)
#elif DT_NODE_EXISTS(DT_ALIAS(button0))
#define BUTTON0 DT_ALIAS(button0)
#elif DT_NODE_EXISTS(DT_NODELABEL(sw1))
#define BUTTON0 DT_NODELABEL(sw1)
#elif DT_NODE_EXISTS(DT_NODELABEL(button0))
#define BUTTON0 DT_NODELABEL(button0)
#else
#define BUTTON0 DT_INVALID_NODE
#endif

#if DT_NODE_EXISTS(LED0)
#define LED0_DEV DT_PHANDLE(LED0, gpios)
#define LED0_PIN DT_PHA(LED0, gpios, pin)
#define LED0_FLAGS DT_PHA(LED0, gpios, flags)

static const struct device *const led_dev = DEVICE_DT_GET(LED0_DEV);
#endif /* LED0 */

#if DT_NODE_EXISTS(BUTTON0)
#define BUTTON0_DEV DT_PHANDLE(BUTTON0, gpios)
#define BUTTON0_PIN DT_PHA(BUTTON0, gpios, pin)
#define BUTTON0_FLAGS DT_PHA(BUTTON0, gpios, flags)

static const struct device *const button_dev = DEVICE_DT_GET(BUTTON0_DEV);
static struct k_work *button_work;

static void button_cb(const struct device *port, struct gpio_callback *cb,
		      gpio_port_pins_t pins)
{
	k_work_submit(button_work);
}
#endif /* BUTTON0 */

static int led_init(void)
{
#if DT_NODE_EXISTS(LED0)
	int err;

	if (!device_is_ready(led_dev)) {
		return -ENODEV;
	}

	err = gpio_pin_configure(led_dev, LED0_PIN,
				 LED0_FLAGS | GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
#else
	printk("WARNING: LEDs not supported on this board.\n");
#endif

	return 0;
}

static int button_init(struct k_work *button_pressed)
{
#if DT_NODE_EXISTS(BUTTON0)
	int err;

	err = gpio_pin_configure(button_dev, BUTTON0_PIN,
				 BUTTON0_FLAGS | GPIO_INPUT);
	if (err) {
		return err;
	}

	static struct gpio_callback gpio_cb;

	err = gpio_pin_interrupt_configure(button_dev, BUTTON0_PIN,
					   GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		return err;
	}

	button_work = button_pressed;

	gpio_init_callback(&gpio_cb, button_cb, BIT(BUTTON0_PIN));
	gpio_add_callback(button_dev, &gpio_cb);
#else
	printk("WARNING: Buttons not supported on this board.\n");
#endif

	return 0;
}

int board_init(struct k_work *button_pressed)
{
	int err;

	err = led_init();
	if (err) {
		return err;
	}

	return button_init(button_pressed);
}

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE); // This is your definition
void print_uart(char *buf)
{
    int msg_len = strlen(buf);
    for (int i = 0; i < msg_len; i++) {
        uart_poll_out(uart_dev, buf[i]);
    }
}
void board_led_set(bool val)
{
#if DT_NODE_EXISTS(LED0)
	// gpio_pin_set(led_dev, LED0_PIN, val); //Normal led

	if(val){
        print_uart("3");
        k_cycle_get_32();
        return;
    }
    else
    {
        print_uart("4");
        k_cycle_get_32();
        return;
    }

#endif
}
void board_output_number(bt_mesh_output_action_t action, uint32_t number)
{
}

void board_prov_complete(void)
{
}
