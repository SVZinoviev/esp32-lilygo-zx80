#pragma once

/*
 * ILI ESP32-S3 2.8" LCD driver.
*/
#define BOARD_LCD_SCK         12
#define BOARD_LCD_MOSI        11
#define BOARD_LCD_MISO        13
#define BOARD_LCD_CS          10
#define BOARD_LCD_DC          46
#define BOARD_LCD_BACKLIGHT   45

/* Native (portrait) panel dimensions. zx_video / draw_bitmap operates in
 * the post-swap_xy landscape orientation, so the *effective* width is
 * LCD_HEIGHT (320) and the effective height is LCD_WIDTH (240) - matching
 * the same convention as st7789v_esp_driver_board.h. */
#define LCD_WIDTH             240
#define LCD_HEIGHT            320
