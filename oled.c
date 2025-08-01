#include "oled.h"

#include <stdint.h>
#include "jpo/oled_driver.h"
#include "oled_indicators.h"
#include "iic.h"

#include "dino_symbols.h"
static const uint8_t SYMBOL_FILL50[] = {
    0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55
};
static const uint8_t SYMBOL_FILL[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#define WS(x,y,sym) oled_driver_write_symbol(&_oled_driver, (x), (y), (sym))

static OLED_VTable_Obj _oled_driver = {
    .i2c_write = oled_i2c_write,
    .inverted = false
};

bool brain_render_oled()
{
#if OLED_INDICATORS_ENABLED
    return indicators_render(&_oled_driver);
#else
    // normal case
    return oled_driver_render(&_oled_driver);
#endif
}

void draw_dino(int r, int c)
{
    WS(r, c, SYMBOL_DINO_0_0);
    WS(r, c+1, SYMBOL_DINO_1_0);
    WS(r, c+2, SYMBOL_DINO_2_0);
    WS(r, c+3, SYMBOL_DINO_3_0);
    WS(r, c+4, SYMBOL_DINO_4_0);
    WS(r, c+5, SYMBOL_DINO_5_0);
    WS(r, c+6, SYMBOL_DINO_6_0);
    WS(r, c+7, SYMBOL_DINO_7_0);
    WS(r, c+8, SYMBOL_DINO_8_0);
    WS(r, c+9, SYMBOL_DINO_9_0);
    WS(r, c+10, SYMBOL_DINO_10_0);

    //WS(r+1, c, SYMBOL_DINO_0_1);
    //WS(r+1, c+1, SYMBOL_DINO_1_1);
    WS(r+1, c+2, SYMBOL_DINO_2_1);
    WS(r+1, c+3, SYMBOL_DINO_3_1);
    WS(r+1, c+4, SYMBOL_DINO_4_1);
    WS(r+1, c+5, SYMBOL_DINO_5_1);
    WS(r+1, c+6, SYMBOL_DINO_6_1);
    //WS(r+1, c+7, SYMBOL_DINO_7_1);
    //WS(r+1, c+8, SYMBOL_DINO_8_1);
    //WS(r+1, c+9, SYMBOL_DINO_9_1);
    //WS(r+1, c+10, SYMBOL_DINO_10_1);

    //WS(r+2, c, SYMBOL_DINO_0_2);
    //WS(r+2, c+1, SYMBOL_DINO_1_2);
    WS(r+2, c+2, SYMBOL_DINO_2_2);
    //WS(r+2, c+3, SYMBOL_DINO_3_2);
    WS(r+2, c+4, SYMBOL_DINO_4_2);
    WS(r+2, c+5, SYMBOL_DINO_5_2);
    //WS(r+2, c+6, SYMBOL_DINO_6_2);
    //WS(r+2, c+7, SYMBOL_DINO_7_2);
    //WS(r+2, c+8, SYMBOL_DINO_8_2);
    //WS(r+2, c+9, SYMBOL_DINO_9_2);
    //WS(r+2, c+10, SYMBOL_DINO_10_2);

    // WS(r+3, c, SYMBOL_DINO_0_3);
    // WS(r+3, c+1, SYMBOL_DINO_1_3);
    // WS(r+3, c+2, SYMBOL_DINO_2_3);
    WS(r+3, c+3, SYMBOL_DINO_3_3);
    WS(r+3, c+4, SYMBOL_DINO_4_3);
    WS(r+3, c+5, SYMBOL_DINO_5_3);
    // WS(r+3, c+6, SYMBOL_DINO_6_3);
    // WS(r+3, c+7, SYMBOL_DINO_7_3);
    // WS(r+3, c+8, SYMBOL_DINO_8_3);
    // WS(r+3, c+9, SYMBOL_DINO_9_3);
    // WS(r+3, c+10, SYMBOL_DINO_10_3);
}

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

void oled_draw_progress_bar(int percent)
{
    // For perf, do not redraw if not needed
    static int _cur_filled = -1;

    // Draw a progress bar at row r, starting at column c.
    // Percent is from 0 to 100.
    int filled = (percent * PROGRESS_BAR_WIDTH) / 100; // 16 columns wide
    if (filled == _cur_filled) {
        return; // No change, skip redraw
    }
    for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
        if (i < filled) {
            WS(PROGRESS_BAR_ROW, i, SYMBOL_FILL);
        } else {
            WS(PROGRESS_BAR_ROW, i, SYMBOL_FILL50);
        }
    }

    brain_render_oled();
}

void oled_start()
{
    static bool oled_initialized = false;
    if (oled_initialized)
    {
        return;
    }

    // Initialize main I2C bus.
    iic_init();

    oled_driver_init(&_oled_driver);

#if OLED_INDICATORS_ENABLED
    indicators_add();
#endif

    oled_initialized = true;
    oled_driver_clear(&_oled_driver);
    
    draw_dino(2, 3);
    //draw_big_b(2, 3);
    //draw_pattern();
    //draw_progress_bar(0);

    brain_render_oled();
}

