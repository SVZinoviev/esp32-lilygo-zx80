#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "st7789v_esp_driver.h"
#include "zx_keyboard.h"
#include "zx_spectrum_emulator.h"
#include "zx_video.h"

// TODO: move pins to the board pins file
#define BOARD_POWERON        15

void power_turn_on(bool is_on)
{
    gpio_config_t poweron_gpio_config = {0};
    poweron_gpio_config.pin_bit_mask  = 1ULL << BOARD_POWERON;
    poweron_gpio_config.mode          = GPIO_MODE_OUTPUT;

    ESP_ERROR_CHECK(gpio_config(&poweron_gpio_config));
    gpio_set_level(BOARD_POWERON, is_on);
}

void app_main(void)
{
    power_turn_on(true);
    zx_video_init();
    st7789v_esp_driver_init(zx_video_on_lcd_trans_done);
    zx_keyboard_init();
    zx_spectrum_init();

    int cycles = 70000;
    while(true) {
        zx_spectrum_run(cycles);
        zx_spectrum_interrupt(0xFF);
        zx_video_render();
    }
}
