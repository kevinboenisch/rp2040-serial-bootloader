#pragma once
#include "config.h"

#if OLED_ENABLED
#include <stdint.h>
#include "jpo/oled_driver.h"

#define PROGRESS_BAR_ROW (7)
#define PROGRESS_BAR_WIDTH (16)

void oled_init(OLED_VTable oled_driver);
void oled_draw_progress_bar(int percent);
#endif