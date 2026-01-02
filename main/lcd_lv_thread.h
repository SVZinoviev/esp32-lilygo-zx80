/**
 * @file      tft_driver.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2024-01-08
 *
 */
#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
    int lcd_lv_thread_start();
    bool lcd_lv_lock();
    void lcd_lv_unlock();
#ifdef __cplusplus
}
#endif












