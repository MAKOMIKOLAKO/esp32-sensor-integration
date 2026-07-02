#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "pulse_sensor.h"

#define MAX30102_ADDR     0x57
#define REG_FIFO_WR_PTR   0x04
#define REG_OVF_COUNTER   0x05
#define REG_FIFO_RD_PTR   0x06
#define REG_FIFO_DATA     0x07
#define REG_MODE_CONFIG   0x09
#define REG_SPO2_CONFIG   0x0A
#define REG_LED1_PA       0x0C

/*
esp_err_t i2c_master_write_to_device(i2c_port_t i2c_num, uint8_t device_address,
                                      const uint8_t *write_buffer, size_t write_size,
                                      TickType_t ticks_to_wait);
*/

bool max30102_init(void) {
    uint8_t buf[2];
    esp_err_t ret;

    buf[0] = REG_MODE_CONFIG; buf[1] = 0x40; // RESET bit
    ret = i2c_master_write_to_device(I2C_NUM_0, MAX30102_ADDR, buf, 2, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(100)); // wait for reset to complete

    buf[0] = REG_LED1_PA; buf[1] = 0x1F;
    ret = i2c_master_write_to_device(I2C_NUM_0, MAX30102_ADDR, buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) { 
        printf("LED1_PA failed: %d\n", ret); 
        return false; 
    }


    buf[0] = REG_FIFO_WR_PTR; buf[1] = 0x00;
    ret = i2c_master_write_to_device(I2C_NUM_0, MAX30102_ADDR, buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        printf("Write pointer reset failed");
        return false;
    };

    buf[0] = REG_OVF_COUNTER; buf[1] = 0x00;
    ret = i2c_master_write_to_device(I2C_NUM_0, MAX30102_ADDR, buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        printf("OVF counter init failed");
        return false;
    };

    buf[0] = REG_FIFO_RD_PTR; buf[1] = 0x00;
    ret = i2c_master_write_to_device(I2C_NUM_0, MAX30102_ADDR, buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        printf("Read pointer reset failed");
        return false;
    };

    buf[0] = REG_SPO2_CONFIG; buf[1] = 0x47;
    ret = i2c_master_write_to_device(I2C_NUM_0, MAX30102_ADDR, buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        printf("SPO2 config failed");
        return false;
    };

    buf[0] = REG_MODE_CONFIG; buf[1] = 0x02;
    ret = i2c_master_write_to_device(I2C_NUM_0, MAX30102_ADDR, buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        printf("Reading mode config failed");
        return false;
    };

    return true;
}

/*

esp_err_t i2c_master_write_read_device(i2c_port_t i2c_num, uint8_t device_address,
                                        const uint8_t *write_buffer, size_t write_size,
                                        uint8_t *read_buffer, size_t read_size,
                                        TickType_t ticks_to_wait);

*/

bool max30102_read(uint32_t *samples) {

    uint8_t wr_reg;
    uint8_t rd_reg;
    uint8_t data_reg = REG_FIFO_DATA;
    
    uint8_t data[3];

    uint8_t reg = REG_FIFO_WR_PTR;
    esp_err_t ret = i2c_master_write_read_device(I2C_NUM_0, MAX30102_ADDR, &reg, 1, &wr_reg, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {return false;}

    reg = REG_FIFO_RD_PTR;
    ret = i2c_master_write_read_device(I2C_NUM_0, MAX30102_ADDR, &reg, 1, &rd_reg, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {return false;}

    uint8_t available = (wr_reg - rd_reg + 32) % 32;

    if (available == 0) {return false;}

    ret = i2c_master_write_read_device(I2C_NUM_0, MAX30102_ADDR, &data_reg, 1, data, 3, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {return false;}

    *samples = ((uint32_t)(data[0] & 0x03) << 16) |
                  ((uint32_t)data[1] << 8)             |
                   (uint32_t)data[2];

    
    return true;
}
