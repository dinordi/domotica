#ifndef serial_H_
#define serial_H_

#include <zephyr/kernel.h>
// #include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdio.h>
#include <time.h>


#define MSG_SIZE 512



 
static char rx_buf[MSG_SIZE];
static int rx_buf_pos = 0;

void send_str(const struct device *dev, const char *str);
void readUsart();
char* returnUsartStr();
char* sendUart(const char* str, const struct device *uart_dev);

#endif 