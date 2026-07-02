#pragma once
#include <stdbool.h>
#include <stdint.h>

bool max30102_init(void);
bool max30102_read(uint32_t *samples);