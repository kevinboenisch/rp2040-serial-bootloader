#include "oled_indicators.h"

// Code here is repeaded from HAL/JCOMP. 
// Ideally, we'd factor out a library for OLED indicators.

#if OLED_INDICATORS_ENABLED

#include "iic.h"
#include "jpo/oled_indicators.h"
#include "jpo/oled_brain_indicators.h"
#include "jpo/jcomp/core1.h"
#include "pico/mutex.h"

#define INDICATOR_UPDATE_MS 1000
static OLED_VTable _indicators_driver = NULL;
auto_init_mutex(_indicators_mutex);

static void update_indicators_handler()
{
    mutex_enter_blocking(&_indicators_mutex);
    bool drawn = oled_indicators_draw(_indicators_driver, false);
    if (drawn)
    {
        oled_driver_render(_indicators_driver);
    }
    mutex_exit(&_indicators_mutex);
}

void indicators_add()
{
    oled_indicator_register(oi_draw_radio_status_w1, 1);
    oled_indicator_register(oi_draw_usbhost_status_w1, 1);
    oled_indicator_register(oi_draw_rs485_status_w1, 1);
    // Skip the redraw counter

    // Add a callback to update them from core1
    core1_add_alarm(INDICATOR_UPDATE_MS, update_indicators_handler, true, NULL);
}

bool indicators_render(OLED_VTable oled_driver)
{
    mutex_enter_blocking(&_indicators_mutex);

    // "commit" the user buffer to the overlay buffer
    oled_indicators_copy_buffer(_indicators_driver, oled_driver);

    // Force a redraw, since user's buffer might have overwritten the indicators area.
    oled_indicators_draw(_indicators_driver, true);
    bool done = oled_driver_render(_indicators_driver);

    mutex_exit(&_indicators_mutex);
    return done;
}

void indicators_init(OLED_VTable indicators_driver)
{
    // Initialize the OLED driver
    _indicators_driver = indicators_driver;
    _indicators_driver->i2c_write = oled_i2c_write;
    _indicators_driver->inverted = true;
}

#endif