#include <stdint.h>
#include "tof_wrapper.h"
#include "VL53L0X.h"
static VL53L0X tof_sensor;

extern "C" bool tof_init(void){
    tof_sensor.i2cMasterInit(GPIO_NUM_23, GPIO_NUM_22, 100000);
    bool gen = tof_sensor.init();
    return gen;
}

extern "C" bool tof_read(uint16_t *range_mm) {

    return tof_sensor.read(range_mm);

}