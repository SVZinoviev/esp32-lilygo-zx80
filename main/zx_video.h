#pragma once

#include <stdbool.h>

#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

void zx_video_init(void);
void zx_video_render(void);

/* Pass this to st7789v_esp_driver_init() so the renderer can block until the
 * previous DMA push has finished before overwriting the framebuffer. */
bool zx_video_on_lcd_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx);

#ifdef __cplusplus
}
#endif
