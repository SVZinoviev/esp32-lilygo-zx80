#include <stdio.h>
#include <lvgl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "board_pins.h"

#include "esp_err.h"
#include "esp_log.h"

#include "tft_driver.h"

static const char *TAG = "main";

#define LVGL_TICK_PERIOD_MS         2
#define LVGL_TASK_MAX_DELAY_MS      500
#define LVGL_TASK_MIN_DELAY_MS      1
#define DISPLAY_TASK_STACK_SIZE     (4 * 1024)
#define DISPLAY_TASK_PRIORITY       2

static SemaphoreHandle_t lvgl_mtx = NULL;
static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)

lv_disp_drv_t disp_drv;      // contains callback functions

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    display_push_colors(offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, (uint16_t *)color_map);
}

static void increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

bool lvgl_lock(int timeout_ms)
{
    // Convert timeout in milliseconds to FreeRTOS ticks
    // If `timeout_ms` is set to -1, the program will block until the condition is met
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mtx, timeout_ticks) == pdTRUE;
}

void lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(lvgl_mtx);
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

static void display_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;

    static lv_style_t style;
    lv_style_init(&style);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_100, 0);

    lv_example_arc_2();

    while (1) {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (lvgl_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            // Release the mutex
            lvgl_unlock();
        }
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

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
    // --- 1. Hardware Power & GPIO Setup ---
    power_turn_on(true);

    // --- 2. Display & Library Initialization ---
    ESP_LOGI(TAG, "------ Initialize DISPLAY.");
    display_init();

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    // --- 3. Memory Allocation for Draw Buffers ---
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(LCD_HEIGHT * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(LCD_HEIGHT * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);
    
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_HEIGHT * 20);

    // --- 4. LVGL Display Driver Registration ---
    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    
    disp_drv.hor_res      = LCD_HEIGHT;
    disp_drv.ver_res      = LCD_WIDTH;
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.draw_buf     = &disp_buf;
    disp_drv.full_refresh = DISPLAY_FULLRESH;
    
    lv_disp_drv_register(&disp_drv);

    // --- 5. LVGL Tick Timer Setup ---
    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback               = &increase_lvgl_tick,
        .arg                    = NULL,
        .dispatch_method        = ESP_TIMER_TASK,
        .name                   = "lvgl_tick",
        .skip_unhandled_events  = false
    };
    
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // --- 6. Concurrency & Tasks ---
    lvgl_mtx = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mtx);

    ESP_LOGI(TAG, "Display task");
    xTaskCreate(
        display_task, 
        "DISP", 
        DISPLAY_TASK_STACK_SIZE, 
        NULL, 
        DISPLAY_TASK_PRIORITY, 
        NULL
    );
}
