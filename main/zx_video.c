#include "zx_video.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "ili9341_esp_driver.h"
#include "zx_spectrum_emulator.h"

/* Freenove ESP32-S3 ILI9341 LCD in landscape orientation (post-swap_xy) is
 * 320x240, which comfortably fits the full 256x192 ZX Spectrum screen.
 * ZX_VIDEO_Y_OFFSET is kept as a configurable knob for cropping experiments
 * but defaults to 0 - no truncation. */
#ifndef ZX_VIDEO_Y_OFFSET
#define ZX_VIDEO_Y_OFFSET  0
#endif

#define RENDER_W           256
#define RENDER_H           192
#define DRAW_X0            0
#define DRAW_Y0            0

_Static_assert(ZX_VIDEO_Y_OFFSET >= 0 && (ZX_VIDEO_Y_OFFSET + RENDER_H) <= 192,
               "ZX_VIDEO_Y_OFFSET + RENDER_H must not exceed 192");

#define ZX_DIM             0xCD
#define ZX_BRIGHT          0xFF

static const char *TAG = "ZXVID";

static uint16_t          *s_fb;
static SemaphoreHandle_t  s_done;

/* The MIPI RAMWR path expects RGB565 with the high byte first on the wire.
 * On a little-endian host, storing 0xRRRRGGGGGGGBBBBB as uint16_t puts the
 * low byte first, so we pre-swap palette entries at compile time. */
#define RGB565(r, g, b)    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define SWAP16(x)          (uint16_t)((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))
#define PAL(r, g, b)       SWAP16(RGB565((r), (g), (b)))

static const uint16_t s_palette[16] = {
    /* INK 0..7, BRIGHT=0 */
    PAL(0, 0, 0),                PAL(0, 0, ZX_DIM),
    PAL(ZX_DIM, 0, 0),           PAL(ZX_DIM, 0, ZX_DIM),
    PAL(0, ZX_DIM, 0),           PAL(0, ZX_DIM, ZX_DIM),
    PAL(ZX_DIM, ZX_DIM, 0),      PAL(ZX_DIM, ZX_DIM, ZX_DIM),
    /* INK 0..7, BRIGHT=1 */
    PAL(0, 0, 0),                PAL(0, 0, ZX_BRIGHT),
    PAL(ZX_BRIGHT, 0, 0),        PAL(ZX_BRIGHT, 0, ZX_BRIGHT),
    PAL(0, ZX_BRIGHT, 0),        PAL(0, ZX_BRIGHT, ZX_BRIGHT),
    PAL(ZX_BRIGHT, ZX_BRIGHT, 0),PAL(ZX_BRIGHT, ZX_BRIGHT, ZX_BRIGHT),
};

/* ZX Spectrum screen byte address (offset from 0x4000) for character column
 * xc (0..31) on scanline y (0..191):
 *
 *   A12..A11 = y[7:6]   (which of 3 thirds)
 *   A10..A8  = y[2:0]   (scanline within character row)
 *   A7..A5   = y[5:3]   (character row within third)
 *   A4..A0   = xc       (character column)
 */
static inline uint16_t zx_pixel_offset(int xc, int y)
{
    return (uint16_t)(((y & 0xC0) << 5)
                    | ((y & 0x07) << 8)
                    | ((y & 0x38) << 2)
                    |  (xc & 0x1F));
}

void zx_video_init(void)
{
    s_done = xSemaphoreCreateBinary();
    assert(s_done);
    xSemaphoreGive(s_done);

    size_t fb_bytes = (size_t)RENDER_W * RENDER_H * sizeof(uint16_t);
    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_DMA);
    assert(s_fb);
    memset(s_fb, 0, fb_bytes);

    ESP_LOGI(TAG, "framebuffer %dx%d (%u bytes)", RENDER_W, RENDER_H, (unsigned)fb_bytes);
}

bool zx_video_on_lcd_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    BaseType_t hp_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &hp_woken);
    return hp_woken == pdTRUE;
}

void zx_video_render(void)
{
    const uint8_t *mem    = zx_spectrum_memory();
    const uint8_t *pixels = mem + 0x4000;
    const uint8_t *attrs  = mem + 0x5800;

    /* Block until the previous push has fully drained the framebuffer over DMA. */
    xSemaphoreTake(s_done, portMAX_DELAY);

    for (int row = 0; row < RENDER_H; row++) {
        int zx_y = row + ZX_VIDEO_Y_OFFSET;
        const uint8_t *attr_row = attrs + (zx_y >> 3) * 32;
        uint16_t *out = s_fb + row * RENDER_W;

        for (int xc = 0; xc < 32; xc++) {
            uint8_t  byte   = pixels[zx_pixel_offset(xc, zx_y)];
            uint8_t  attr   = attr_row[xc];
            uint8_t  bright = (attr & 0x40) ? 8 : 0;
            uint16_t ink    = s_palette[(attr & 0x07) | bright];
            uint16_t paper  = s_palette[((attr >> 3) & 0x07) | bright];

            for (int b = 0; b < 8; b++) {
                *out++ = (byte & (0x80 >> b)) ? ink : paper;
            }
        }
    }

    ili9341_esp_driver_draw_bitmap(DRAW_X0,             DRAW_Y0,
                                   DRAW_X0 + RENDER_W,  DRAW_Y0 + RENDER_H,
                                   s_fb);
}
