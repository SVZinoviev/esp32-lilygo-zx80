/**
 * @file      display_s3.c
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2024-01-08
 *
 */
#include <sdkconfig.h>
#include "freertos/FreeRTOS.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_dma_utils.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "board_pins.h"
#include "initSequence.h"

#include "lvgl.h"
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (10 * 1000 * 1000)

static const char *TAG = "TFT";

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
extern lv_disp_drv_t disp_drv;

lcd_cmd_t lcd_st7789v[] = {
    {0x11, {0}, 0 | 0x80},
    {0x3A, {0x05}, 1},
    {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x75}, 1},
    {0xBB, {0x28}, 1},
    {0xC0, {0x2C}, 1},
    {0xC2, {0x01}, 1},
    {0xC3, {0x1F}, 1},
    {0xC6, {0x13}, 1},
    {0xD0, {0xA7}, 1},
    {0xD0, {0xA4, 0xA1}, 2},
    {0xD6, {0xA1}, 1},
    {0xE0, {0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B, 0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32}, 14},
    {0xE1, {0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B, 0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37}, 14},
};

void display_push_colors(uint16_t x, uint16_t y, uint16_t width, uint16_t hight, uint16_t *data)
{
    esp_lcd_panel_draw_bitmap(panel_handle, x, y, width, hight, data);
}

bool display_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_flush_ready(&disp_drv);
    return false;
}

void display_init()
{
    ESP_LOGI(TAG, "============T-Display-S3============");

    gpio_config_t rd_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BOARD_LCD_RD
    };
    ESP_ERROR_CHECK(gpio_config(&rd_gpio_config));
    gpio_set_level(BOARD_LCD_RD, 1);


    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = BOARD_LCD_DC,
        .wr_gpio_num = BOARD_LCD_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums =
        {
            BOARD_LCD_DATA0,
            BOARD_LCD_DATA1,
            BOARD_LCD_DATA2,
            BOARD_LCD_DATA3,
            BOARD_LCD_DATA4,
            BOARD_LCD_DATA5,
            BOARD_LCD_DATA6,
            BOARD_LCD_DATA7,
        },
        .bus_width = 8,
        .max_transfer_bytes = LCD_WIDTH  * 100 * sizeof(uint16_t),
        .psram_trans_align = 64,
        .sram_trans_align = 4
    };
    esp_lcd_new_i80_bus(&bus_config, &i80_bus);


    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = BOARD_LCD_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels =
        {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .on_color_trans_done = display_notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_LCD_RST,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
        .vendor_config = NULL
    };
    esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);

    esp_lcd_panel_reset(panel_handle);

    esp_lcd_panel_init(panel_handle);

    esp_lcd_panel_invert_color(panel_handle, true);

    esp_lcd_panel_swap_xy(panel_handle, true);

    //The screen faces you, and the USB is on the left
    esp_lcd_panel_mirror(panel_handle, false, true);

    //The screen faces you, the USB is to the right
    // esp_lcd_panel_mirror(panel_handle, true, false);

    // the gap is LCD panel specific, even panels with the same driver IC, can
    // have different gap value
    esp_lcd_panel_set_gap(panel_handle, 0, 35);

    for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++) {
        esp_lcd_panel_io_tx_param(io_handle, lcd_st7789v[i].addr, lcd_st7789v[i].param, lcd_st7789v[i].len & 0x7f);
        if (lcd_st7789v[i].len & 0x80)
            vTaskDelay(pdMS_TO_TICKS(120));
    }

    esp_lcd_panel_disp_on_off(panel_handle, true);

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BOARD_LCD_BACKLIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(BOARD_LCD_BACKLIGHT, 1);
}
















