#include "serial.h"

K_MSGQ_DEFINE(uart_msgq, 2, 270, 4);
char send_buf[256];

void send_str(const struct device *dev, const char *str) {  //This functions sends the string over uart
    int msg_len = strlen(str);
    
    printk("Device %s sending: \"%s\"\n", dev->name, str);
    for (int i = 0; i < msg_len; i++) {
        uart_poll_out(dev, str[i]);
    }
 
}

char* sendUart(const char* str, const struct device *uart_dev)//Send string to ESP and return the response
{
    
    snprintf(send_buf, 200, str);
    send_str(uart_dev, send_buf);
    
    return returnUsartStr();
}

char* returnUsartStr()   //put characters from messagequeue into string and return it
{
    int ret;
    uint8_t ok = 0;
    uint16_t c;
    rx_buf_pos = 0;
    for(int i = 0; i< sizeof(rx_buf); i++)
    {
        rx_buf[i] = '\0';
    }
    
   
    while(1)
    {
        ret = k_msgq_get(&uart_msgq, &c, K_FOREVER);
        printk("%c", c);
        if (ret == 0) {
            // printk("Received: %c\n", uart_data);
            rx_buf[rx_buf_pos] = c;
            rx_buf_pos++;
        }

        return rx_buf;
    }
}

void readUsart(const struct device *uart_dev, void *user_data)//The usart interrupt calss this function and will put the character into the messagequeue
{
    printk("Interrupt triggered\n");
    int ret;
    uint16_t c;
    
    ret = uart_poll_in(uart_dev, &c);
    if (ret == 0) {
        
        ret = k_msgq_put(&uart_msgq, &c, K_NO_WAIT);
        if (ret != 0) {
            printk("Failed to put data into message queue\n");
        }
        

    } else if (ret == -EAGAIN) {
        // No data available, handle it accordingly
    } else {
        // Handle error, if any
        printk("UART read error: %d\n", ret);
    }
}
