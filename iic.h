#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void iic_init();

bool oled_i2c_write(void *data, const uint8_t *src, size_t len);
