#pragma once

#include <stdint.h>

#define PROGRESS_BAR_ROW (7)
#define PROGRESS_BAR_WIDTH (16)

void oled_start();
void oled_draw_progress_bar(int percent);
