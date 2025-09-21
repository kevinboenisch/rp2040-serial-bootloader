#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void iic_init();

bool oled_i2c_write(void *data, const uint8_t *src, size_t len);

int i2c_write_main_locked(uint8_t addr, const uint8_t *src, size_t len, bool nostop);

int i2c_read_main_locked(uint8_t addr, uint8_t *dst, size_t len, bool nostop);
