#include "iic.h"

#include "hardware/i2c.h"
#include "hardware/gpio.h"

// iic
#define MAIN_I2C i2c0
#define MAIN_I2C_SDA 4
#define MAIN_I2C_SCL 5
#define MAIN_I2C_CLK 400000
#define OLED_I2C_ADDR 0x3Cu

void iic_init()
{
    i2c_init(MAIN_I2C, MAIN_I2C_CLK);
    gpio_set_function(MAIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(MAIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(MAIN_I2C_SDA);
    gpio_pull_up(MAIN_I2C_SCL);
}

bool oled_i2c_write(void *data, const uint8_t *src, size_t len)
{
    return i2c_write_blocking(MAIN_I2C, OLED_I2C_ADDR, src, len, false) >= 0;
}
