#include "esp_timer.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dht11.h"

static gpio_num_t dht_gpio;

void DHT11_init(gpio_num_t gpio_num) {
    // 等待 1 秒讓硬體穩定
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    dht_gpio = gpio_num;
}

static int _waitOrTimeout(uint16_t microSeconds, int level) {
    int micros_ticks = 0;
    while(gpio_get_level(dht_gpio) == level) { 
        if(micros_ticks++ > microSeconds) return DHT11_TIMEOUT_ERROR;
        ets_delay_us(1);
    }
    return micros_ticks;
}

static int _checkCRC(uint8_t data[]) {
    if(data[4] == (data[0] + data[1] + data[2] + data[3]))
        return DHT11_OK;
    else
        return DHT11_CRC_ERROR;
}

struct dht11_reading DHT11_read() {
    struct dht11_reading last_read;
    uint8_t data[5] = {0,0,0,0,0};

    // 發送開始訊號
    gpio_set_direction(dht_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(dht_gpio, 0);
    ets_delay_us(20000); // 20ms
    gpio_set_level(dht_gpio, 1);
    ets_delay_us(40);
    gpio_set_direction(dht_gpio, GPIO_MODE_INPUT);

    // 讀取回應
    if(_waitOrTimeout(80, 0) == DHT11_TIMEOUT_ERROR) {
        last_read.status = DHT11_TIMEOUT_ERROR;
        return last_read;
    }

    if(_waitOrTimeout(80, 1) == DHT11_TIMEOUT_ERROR) {
        last_read.status = DHT11_TIMEOUT_ERROR;
        return last_read;
    }

    // 讀取 40 bits 資料
    for(int i = 0; i < 40; i++) {
        if(_waitOrTimeout(50, 0) == DHT11_TIMEOUT_ERROR) {
            last_read.status = DHT11_TIMEOUT_ERROR;
            return last_read;
        }

        if(_waitOrTimeout(70, 1) > 28) {
            // Bit 1
            data[i/8] |= (1 << (7-(i%8)));
        } // else Bit 0
    }

    if(_checkCRC(data) != DHT11_CRC_ERROR) {
        last_read.status = DHT11_OK;
        last_read.temperature = data[2];
        last_read.humidity = data[0];
        return last_read;
    } else {
        last_read.status = DHT11_CRC_ERROR;
        return last_read;
    }
}