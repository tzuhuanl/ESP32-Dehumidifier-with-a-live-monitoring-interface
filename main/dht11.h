#ifndef DHT11_H
#define DHT11_H

#include "driver/gpio.h"

enum dht11_status {
    DHT11_CRC_ERROR = -2,
    DHT11_TIMEOUT_ERROR = -1,
    DHT11_OK = 0
};

struct dht11_reading {
    int status;
    int temperature;
    int humidity;
};

void DHT11_init(gpio_num_t gpio_num);
struct dht11_reading DHT11_read();

#endif