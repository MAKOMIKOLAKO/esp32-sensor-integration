#include <stdio.h>
#include "tof_wrapper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "pulse_sensor.h"
#include "driver/gpio.h"
#include "freertos/timers.h"
#include "esp_timer.h"


static float dc_mean = 0.0f;
static float filtered = 0.0f;
static float signal_min = 0.0f;
static float signal_max = 0.0f;
static float prev_filtered = 0.0f;
static int64_t prev_time = 0;

static void i2c_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_23,
        .scl_io_num = GPIO_NUM_22,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}

void sampling_callback(TimerHandle_t xTimer) {
    uint32_t sample;
    float ac_signal;
    float threshold;
    int64_t now;
    int64_t ibi;
    uint32_t bpm;

    if (max30102_read(&sample)) {
        // process sample here
        printf("Sample: %lu\n", sample);
        
        dc_mean = (dc_mean * 99 + (float)sample) / 100;
        ac_signal = (float)sample - dc_mean;
        filtered = filtered * 0.85 + ac_signal * 0.15;
        if (filtered < signal_min) {
            signal_min = filtered;
        } 
        if (filtered > signal_max) {
            signal_max = filtered;
        }
        threshold = (signal_max + signal_min) / 2.0f + 0.3f * (signal_max - signal_min);
        now = esp_timer_get_time();
        if (filtered > threshold && prev_filtered < threshold) {
            ibi = now - prev_time; 
            if (prev_time != 0 && ibi > 300000 && ibi < 1500000) {
                bpm = 60000000 / ibi;
                printf("BPM: %lu\n", bpm);
            }
        }
        prev_time = now;
        prev_filtered = filtered;
    } else {
        return;
    }

}

void app_main(void)
{

    TimerHandle_t sample_timer = xTimerCreate(
        "ppg_timer",           // name for debugging
        pdMS_TO_TICKS(10),     // period — 10ms = 100Hz
        pdTRUE,                // auto-reload (repeating, not one-shot)
        NULL,                  // timer ID, not needed here
        sampling_callback      // function to call
    );
    /*uint16_t distance;
    if (!tof_init()) {
        printf("ToF sensor init failed\n");
        return;
    }*/
    i2c_init();
    
    vTaskDelay(pdMS_TO_TICKS(100));

    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(10));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            printf("I2C device found at address: 0x%02X\n", addr);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }   
    
    if (!max30102_init()) {
        printf("MAX30102 init failed\n");
        return;
    }
    xTimerStart(sample_timer, 0);
    while(1) vTaskDelay(pdMS_TO_TICKS(1000));
}

