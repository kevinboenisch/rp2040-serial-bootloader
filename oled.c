#include "oled.h"

#include <stdint.h>
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "jpo/oled_driver.h"

#define MAIN_I2C i2c0
#define MAIN_I2C_SDA 4
#define MAIN_I2C_SCL 5
#define MAIN_I2C_CLK 400000

#define OLED_I2C_ADDR 0x3Cu

static void iic_init()
{
    i2c_init(MAIN_I2C, MAIN_I2C_CLK);
    gpio_set_function(MAIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(MAIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(MAIN_I2C_SDA);
    gpio_pull_up(MAIN_I2C_SCL);
}

static bool oled_i2c_write(void *data, const uint8_t *src, size_t len)
{
    return i2c_write_blocking(MAIN_I2C, OLED_I2C_ADDR, src, len, false) >= 0;
}

static OLED_VTable_Obj _oled_driver = {
    .i2c_write = oled_i2c_write,
    .inverted = false
};

static const uint8_t SYMBOL_FILL[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

void draw_big_b(int r, int c)
{
    // Stylized large "B"
    oled_driver_write_symbol(&_oled_driver, r, c, SYMBOL_FILL);
    oled_driver_write_symbol(&_oled_driver, r, c+1, SYMBOL_FILL);
    oled_driver_write_symbol(&_oled_driver, r, c+2, SYMBOL_FILL);
    r++;
    oled_driver_write_symbol(&_oled_driver, r, c, SYMBOL_FILL);
    oled_driver_write_symbol(&_oled_driver, r, c+3, SYMBOL_FILL);
    r++;
    oled_driver_write_symbol(&_oled_driver, r, c, SYMBOL_FILL);
    oled_driver_write_symbol(&_oled_driver, r, c+1, SYMBOL_FILL);
    oled_driver_write_symbol(&_oled_driver, r, c+2, SYMBOL_FILL);
    r++;
    oled_driver_write_symbol(&_oled_driver, r, c, SYMBOL_FILL);
    oled_driver_write_symbol(&_oled_driver, r, c+3, SYMBOL_FILL);
    r++;
    oled_driver_write_symbol(&_oled_driver, r, c, SYMBOL_FILL);
    oled_driver_write_symbol(&_oled_driver, r, c+1, SYMBOL_FILL);
    oled_driver_write_symbol(&_oled_driver, r, c+2, SYMBOL_FILL);
}

void draw_pattern()
{
    for (int r = 0; r < 8; r++) {
        int c = r % 2;
        oled_driver_write_symbol(&_oled_driver, r, c, SYMBOL_FILL);
    }
}

void oled_start()
{
    // Initialize main I2C bus.
    iic_init();

    oled_driver_init(&_oled_driver);
    oled_driver_clear(&_oled_driver);

    draw_big_b(2, 3);
    draw_pattern();

    oled_driver_render(&_oled_driver);
}