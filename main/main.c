#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "dht11.h"
#include "esp_log.h"
#include "esp_sntp.h"


#define SERVO_GPIO       17
#define DHT_GPIO         13
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_DUTY_RES    LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY   50

// --- Type in your own WIFI and password ---
#define WIFI_SSID      ""
#define WIFI_PASS      ""


int current_humidity = 0;
int current_temperature = 0;
bool is_high_humidity_state = false;
float total_energy_kwh = 0.0f;
float total_cost_euro = 0.0f;
const float WATT = 200.0f;
const float KWH_RATE = 0.24f;

// Motor control caculation
uint32_t angle_to_duty(int angle) {
    int pulse_us = 500 + (angle * (2400 - 500) / 180);
    return (pulse_us * 8192) / 20000;
}

void servo_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE, 
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES, 
        .freq_hz = LEDC_FREQUENCY, 
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE, 
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER, 
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = SERVO_GPIO, 
        .duty = angle_to_duty(0), .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
}

void get_current_time_str(char* time_str, size_t max_len) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);


    if (timeinfo.tm_year < (2024 - 1900)) {
        strncpy(time_str, "Synchronizing the time...", max_len);
    } else {

        strftime(time_str, max_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
}


static esp_err_t root_get_handler(httpd_req_t *req) {
    char html_resp[1024];

    snprintf(html_resp, sizeof(html_resp),
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<meta http-equiv=\"refresh\" content=\"5\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Bathroom Humidity Monitoring</title>"
        "<style>body{font-family: sans-serif; padding: 20px;} h1{color: #0056b3;} .data{font-size: 24px; font-weight: bold;}</style>"
        "</head><body>"
        "<h1>Dehumidifier Live-Monitoring</h1>"
        "<p>Status： <span class=\"data\">%s</span></p>"
        "<hr>"
        "<p>Current Humidity： <span class=\"data\">%d %%</span></p>"
        "<p>Current Temperature： <span class=\"data\">%d &deg;C</span></p>"
        "<hr>"
        "<p>Total consumption of electricity： <span class=\"data\">%.4f kWh</span></p>"
        "<p>Total costs of electricity： <span class=\"data\">&euro; %.4f</span></p>"
        "</body></html>",
        is_high_humidity_state ? "Running 🔴" : "Standby 🟢",
        current_humidity, current_temperature,
        total_energy_kwh, total_cost_euro);

    httpd_resp_send(req, html_resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


void start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { 
            .uri = "/", 
            .method = HTTP_GET, 
            .handler = root_get_handler, 
            .user_ctx = NULL 
        };
        httpd_register_uri_handler(server, &uri_get);
    }
}

void initialize_sntp(void) {
    printf("Initializing the time setup...\n");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        printf("Wi-Fi interrupted, trying to reconnent...\n");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("\n=========================================\n");
        printf("✅ Wi-Fi connected！\n");
        printf("🌐 Please use zour electronic devices to open this website: http://" IPSTR "\n", IP2STR(&event->ip_info.ip));
        printf("=========================================\n\n");
        initialize_sntp();
    }
}

void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    wifi_config_t wifi_config = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS, }, };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}


void app_main(void) {

    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    nvs_flash_init();
    servo_init();
    DHT11_init(DHT_GPIO);
    gpio_set_pull_mode(DHT_GPIO, GPIO_PULLUP_ONLY);

  
    wifi_init_sta();
    start_webserver();

    uint64_t turn_on_time_us = 0;
    char console_time_str[64];

    while (1) {
        vTaskDelay(3000 / portTICK_PERIOD_MS); 

        struct dht11_reading reading = DHT11_read(); 
        if (reading.status != DHT11_OK) {
            printf("Cannot read DHT11, error code: %d\n", reading.status);
            continue; 
        }

        current_humidity = reading.humidity;
        current_temperature = reading.temperature;

  
        get_current_time_str(console_time_str, sizeof(console_time_str));
        printf("\r[%s] Humidity: %d%%  Temp: %d°C   ", console_time_str, current_humidity, current_temperature);
        fflush(stdout);


        if (current_humidity >= 60 && !is_high_humidity_state) {
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(65));
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            vTaskDelay(2000 / portTICK_PERIOD_MS); 
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(0));
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

            turn_on_time_us = esp_timer_get_time(); 
            is_high_humidity_state = true;
        } 

        else if (current_humidity <= 49 && is_high_humidity_state) {
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(65));
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            vTaskDelay(2000 / portTICK_PERIOD_MS); 
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, angle_to_duty(0));
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

            uint64_t duration_us = esp_timer_get_time() - turn_on_time_us;
            float duration_hours = (float)duration_us / 3600000000.0f; 
            float energy_kwh = (WATT / 1000.0f) * duration_hours;
            float cost_euro = energy_kwh * KWH_RATE;
            
            total_energy_kwh += (WATT / 1000.0f) * duration_hours;
            total_cost_euro += ((WATT / 1000.0f) * duration_hours) * KWH_RATE;

            printf("\n[%s] ☀️ The humidity is decreased to %d%%, turning off the machine.\n", console_time_str, current_humidity);
            printf(" -> Duration: %.4f hour(s) | Electricity: %.4f kWh | Costs: €%.4f\n", duration_hours, energy_kwh, cost_euro);

            is_high_humidity_state = false;
        }
    }
}