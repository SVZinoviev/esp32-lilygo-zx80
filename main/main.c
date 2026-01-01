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
static lv_disp_draw_buf_t display_buffer;
static lv_disp_drv_t display_driver;

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
void lv_example_arc(void)
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

    lv_example_arc();

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

/*
    Inits hardware and LVGL instances
*/
static lv_color_t *buffers[2];
#define BUF_SIZE (LCD_HEIGHT * 20 * sizeof(lv_color_t))
#define DRAW_BUF_SIZE (LCD_HEIGHT * 20)
int graphics_init()
{
    int ret = -1;

    display_init(&display_driver);
    lv_init();

    ESP_LOGI(TAG, "Allocate LVGL buffers");
    buffers[0] = (lv_color_t *)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
    assert(buffers[0]);

    buffers[1] = (lv_color_t *)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA);
    assert(buffers[1]);

    ESP_LOGI(TAG, "Setup LVGL buffers");
    lv_disp_draw_buf_init(&display_buffer, buffers[0], buffers[1], DRAW_BUF_SIZE);

    lv_disp_drv_init(&display_driver);
    display_driver.hor_res      = LCD_HEIGHT;
    display_driver.ver_res      = LCD_WIDTH;
    display_driver.flush_cb     = lvgl_flush_cb;
    display_driver.draw_buf     = &display_buffer;
    display_driver.full_refresh = DISPLAY_FULLRESH;
    
    ESP_LOGI(TAG, "Register LVGL driver");
    lv_disp_drv_register(&display_driver);

    const esp_timer_create_args_t lvgl_timer_args = {
        .callback               = &increase_lvgl_tick,
        .arg                    = NULL,
        .dispatch_method        = ESP_TIMER_TASK,
        .name                   = "lvgl_tick",
        .skip_unhandled_events  = false
    };
    
    ESP_LOGI(TAG, "Create LVGL timer");
    esp_timer_handle_t lvgl_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_timer_args, &lvgl_timer));
    ESP_LOGI(TAG, "Start LVGL timer");
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Create LVGL mutex");
    lvgl_mtx = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mtx);

    xTaskCreate(
        display_task, 
        "DISP", 
        DISPLAY_TASK_STACK_SIZE, 
        NULL, 
        DISPLAY_TASK_PRIORITY, 
        NULL
    );

    return ret;
}

void app_main(void)
{
    power_turn_on(true);
    graphics_init();

    while(true) {
        vTaskDelay(100);
    }
}
