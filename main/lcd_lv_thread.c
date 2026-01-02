#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_dma_utils.h"
#include "esp_timer.h"

#include "driver/gpio.h"

#include "lvgl.h"
#include "st7789v_esp_driver_board.h"
#include "st7789v_esp_driver.h"

#define LVGL_TIMER_PERIOD_ms        10
#define DISPLAY_TASK_STACK_SIZE     (4 * 1024)
#define DISPLAY_TASK_PRIORITY       2

static SemaphoreHandle_t lvgl_mtx = NULL;
static lv_disp_draw_buf_t display_buffer;
static lv_disp_drv_t display_driver;
static const char *TAG = "LVLCD";

// ESP32 wrapper for the notifying that data is transmitted
bool display_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_flush_ready(&display_driver);
    return false;
}

// Update LCD callback
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    st7789v_esp_driver_draw_bitmap(offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, (uint16_t *)color_map);
}

static void increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TIMER_PERIOD_ms);
}

bool lcd_lv_lock()
{
    return xSemaphoreTakeRecursive(lvgl_mtx, portMAX_DELAY) == pdTRUE;
}

void lcd_lv_unlock()
{
    xSemaphoreGiveRecursive(lvgl_mtx);
}

static void display_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Starting LVGL task");

    while (1) {
        // Block task until it'll be unblocked by other task
        if (xSemaphoreTakeRecursive(lvgl_mtx, portMAX_DELAY) == pdPASS) {
            lv_timer_handler_run_in_period(LVGL_TIMER_PERIOD_ms);
            xSemaphoreGiveRecursive(lvgl_mtx);
        }
        vTaskDelay(pdMS_TO_TICKS(LVGL_TIMER_PERIOD_ms));
    }
}

/*
    Inits hardware and LVGL instances
*/
static lv_color_t *buffers[2];
#define BUF_MULTIPLIER 15
#define BUF_SIZE (LCD_HEIGHT * BUF_MULTIPLIER * sizeof(lv_color_t))
#define DRAW_BUF_SIZE (LCD_HEIGHT * BUF_MULTIPLIER)
int lcd_lv_thread_start()
{
    int ret = -1;

    st7789v_esp_driver_init(display_notify_lvgl_flush_ready);

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
    display_driver.full_refresh = false;
    
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
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_timer, LVGL_TIMER_PERIOD_ms * 1000));

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














