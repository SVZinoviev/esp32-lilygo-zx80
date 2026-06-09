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
#define DRAW_X0            ((LCD_WIDTH - RENDER_W) / 2)
#define DRAW_Y0            ((LCD_HEIGHT - RENDER_H) / 2)

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
union video_addr_u {
    uint16_t address;
    struct {
        uint16_t xcol : 5;
        uint16_t y3_y5: 3;
        uint16_t y0_y2: 3;
        uint16_t y6_y7: 2;
    };
};

static inline uint16_t zx_pixel_offset(int xc, int y)
{
    union video_addr_u va;

    va.address = 0;
    va.xcol = xc;
    va.y0_y2 = y & 0x07;
    va.y3_y5 = (y >> 3) & 0x07;
    va.y6_y7 = (y >> 6) & 0x03;

    return va.address;
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

    /* One-shot full-screen clear so the panel boot garbage doesn't show
     * around the ZX area. Tiled with a small black buffer to keep the DMA
     * scratch tiny; serialised through s_done. */
    const size_t tile = 32;
    size_t tile_bytes = tile * tile * sizeof(uint16_t);
    uint16_t *black_tile = heap_caps_malloc(tile_bytes, MALLOC_CAP_DMA);
    assert(black_tile);
    memset(black_tile, 0, tile_bytes);

    for (int y = 0; y < LCD_HEIGHT; y += tile) {
        for (int x = 0; x < LCD_WIDTH; x += tile) {
            xSemaphoreTake(s_done, portMAX_DELAY);
            ili9341_esp_driver_draw_bitmap(x, y, tile, tile, black_tile);
        }
    }
    /* Wait for the last tile to drain before freeing the buffer, then
     * restore the semaphore to "free" for the renderer. */
    xSemaphoreTake(s_done, portMAX_DELAY);
    xSemaphoreGive(s_done);
    heap_caps_free(black_tile);
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

union attr_u {
    uint8_t attr;
    struct {
        uint8_t ink         :3;
        uint8_t paper       :3;
        uint8_t brightness  :1;
        uint8_t flash       :1;
    };
};

void zx_video_render(void)
{
    const uint8_t *mem    = zx_spectrum_memory();
    const uint8_t *pixels = mem + 0x4000;
    const uint8_t *attrs  = mem + 0x5800;

    /* Block until the previous push has fully drained the framebuffer over DMA. */
    xSemaphoreTake(s_done, portMAX_DELAY);

    union attr_u attr;

    for (int row = 0; row < RENDER_H; row++) {
        int zx_y = row + ZX_VIDEO_Y_OFFSET;
        const uint8_t *attr_row = attrs + ((zx_y >> 3) << 5);
        uint16_t *out = s_fb + (row << 8);

        for (int xc = 0; xc < 32; xc++) {
            uint8_t byte = pixels[zx_pixel_offset(xc, zx_y)];
            attr.attr = attr_row[xc];
            uint16_t ink = s_palette[attr.ink | (attr.brightness ? 8 : 0)];
            uint16_t paper = s_palette[attr.paper | (attr.brightness ? 8 : 0)];

            for (int b = 0; b < 8; b++) {
                *out++ = (byte & (0x80 >> b)) ? ink : paper;
            }
        }
    }

    ili9341_esp_driver_draw_bitmap(DRAW_X0, DRAW_Y0, RENDER_W, RENDER_H, s_fb);
}
