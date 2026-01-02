#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lcd_lv_thread.h"

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

static void set_angle(void * obj, int32_t v)
{
    lv_arc_set_value(obj, v);
}

/**
 * Create an arc which acts as a loader.
 */
void lv_example_arc_2(void)
{
    /*Create an Arc*/
    lv_obj_t * arc = lv_arc_create(lv_scr_act());
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);   /*Be sure the knob is not displayed*/
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);  /*To not allow adjusting by click*/
    lv_obj_center(arc);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, set_angle);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);    /*Just for the demo*/
    lv_anim_set_repeat_delay(&a, 500);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_start(&a);

}

void app_main(void)
{
    power_turn_on(true);
    lcd_lv_thread_start();

    lv_example_arc_2();

    while(true) {
        vTaskDelay(100);
    }
}
