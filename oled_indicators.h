#pragma once
#include "config.h"

#if OLED_INDICATORS_ENABLED
#include "jpo/oled_driver.h"

void indicators_init(OLED_VTable indicators_driver);
void indicators_add();
bool indicators_render(OLED_VTable oled_driver);
void indicators_unregister_all();

#endif