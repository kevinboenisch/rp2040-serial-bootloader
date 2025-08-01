#pragma once

#include "jpo/oled_driver.h"

#define OLED_INDICATORS_ENABLED (0)

void indicators_add();
bool indicators_render(OLED_VTable oled_driver);