#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool tof_init(void);
bool tof_read(uint16_t *range_mm);

#ifdef __cplusplus
}
#endif