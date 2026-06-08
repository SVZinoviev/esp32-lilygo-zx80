#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ili9341_esp_driver.h"
#include "zx_keyboard.h"
#include "zx_spectrum_emulator.h"
#include "zx_video.h"

void app_main(void)
{
    zx_video_init();
    ili9341_esp_driver_init(zx_video_on_lcd_trans_done);
    zx_keyboard_init();
    zx_spectrum_init();

    int cycles = 70000;
    while(true) {
        zx_spectrum_run(cycles);
        zx_spectrum_interrupt(0xFF);
        zx_video_render();
    }
}
