#include "zx_keyboard.h"

#include <ctype.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"

/* ZX Spectrum 8x5 key matrix. Each row's bits 0..4 hold the 5 keys; bits 5..7
 * are unused-pulled-high. A bit reads 0 when the key is pressed. */
static volatile uint8_t s_rows[8] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

typedef struct {
    uint8_t row;
    uint8_t mask;
} zx_key_pos_t;

#define K(row, bit)  { (row), (uint8_t)(1u << (bit)) }

/* Matrix layout (row = address line A8+row, bit = data line D0..D4):
 *   row 0 (A8 ): CAPS, Z, X, C, V
 *   row 1 (A9 ): A, S, D, F, G
 *   row 2 (A10): Q, W, E, R, T
 *   row 3 (A11): 1, 2, 3, 4, 5
 *   row 4 (A12): 0, 9, 8, 7, 6
 *   row 5 (A13): P, O, I, U, Y
 *   row 6 (A14): ENTER, L, K, J, H
 *   row 7 (A15): SPACE, SYM, M, N, B */
static const zx_key_pos_t s_pos[ZX_KEY_COUNT] = {
    [ZX_KEY_CAPS_SHIFT] = K(0, 0), [ZX_KEY_Z] = K(0, 1), [ZX_KEY_X] = K(0, 2),
    [ZX_KEY_C]          = K(0, 3), [ZX_KEY_V] = K(0, 4),
    [ZX_KEY_A] = K(1, 0), [ZX_KEY_S] = K(1, 1), [ZX_KEY_D] = K(1, 2),
    [ZX_KEY_F] = K(1, 3), [ZX_KEY_G] = K(1, 4),
    [ZX_KEY_Q] = K(2, 0), [ZX_KEY_W] = K(2, 1), [ZX_KEY_E] = K(2, 2),
    [ZX_KEY_R] = K(2, 3), [ZX_KEY_T] = K(2, 4),
    [ZX_KEY_1] = K(3, 0), [ZX_KEY_2] = K(3, 1), [ZX_KEY_3] = K(3, 2),
    [ZX_KEY_4] = K(3, 3), [ZX_KEY_5] = K(3, 4),
    [ZX_KEY_0] = K(4, 0), [ZX_KEY_9] = K(4, 1), [ZX_KEY_8] = K(4, 2),
    [ZX_KEY_7] = K(4, 3), [ZX_KEY_6] = K(4, 4),
    [ZX_KEY_P] = K(5, 0), [ZX_KEY_O] = K(5, 1), [ZX_KEY_I] = K(5, 2),
    [ZX_KEY_U] = K(5, 3), [ZX_KEY_Y] = K(5, 4),
    [ZX_KEY_ENTER] = K(6, 0), [ZX_KEY_L] = K(6, 1), [ZX_KEY_K] = K(6, 2),
    [ZX_KEY_J]     = K(6, 3), [ZX_KEY_H] = K(6, 4),
    [ZX_KEY_SPACE] = K(7, 0), [ZX_KEY_SYM_SHIFT] = K(7, 1), [ZX_KEY_M] = K(7, 2),
    [ZX_KEY_N]     = K(7, 3), [ZX_KEY_B] = K(7, 4),
};

void zx_keyboard_press(zx_key_t key, bool down)
{
    if ((unsigned)key >= ZX_KEY_COUNT) return;
    zx_key_pos_t p = s_pos[key];
    if (down) {
        s_rows[p.row] &= (uint8_t)~p.mask;
    } else {
        s_rows[p.row] |= p.mask;
    }
}

uint8_t zx_keyboard_read(uint16_t port)
{
    uint8_t port_hi = (uint8_t)(port >> 8);
    uint8_t value   = 0xFF;
    for (int row = 0; row < 8; row++) {
        if (!(port_hi & (1u << row))) {
            value &= s_rows[row];
        }
    }
    return value;
}

/* ---- Console -> matrix bridge ------------------------------------------- */

typedef struct {
    int       ch;          /* matching input character */
    zx_key_t  key;
    bool      caps;        /* hold CAPS SHIFT while pressed */
    bool      sym;         /* hold SYM SHIFT while pressed */
} char_map_t;

static const char_map_t s_char_map[] = {
    /* Unshifted alphanumerics and whitespace */
    { ' ',  ZX_KEY_SPACE }, { '\r', ZX_KEY_ENTER }, { '\n', ZX_KEY_ENTER },
    { '0',  ZX_KEY_0 }, { '1', ZX_KEY_1 }, { '2', ZX_KEY_2 }, { '3', ZX_KEY_3 },
    { '4',  ZX_KEY_4 }, { '5', ZX_KEY_5 }, { '6', ZX_KEY_6 }, { '7', ZX_KEY_7 },
    { '8',  ZX_KEY_8 }, { '9', ZX_KEY_9 },
    { 'A',  ZX_KEY_A }, { 'B', ZX_KEY_B }, { 'C', ZX_KEY_C }, { 'D', ZX_KEY_D },
    { 'E',  ZX_KEY_E }, { 'F', ZX_KEY_F }, { 'G', ZX_KEY_G }, { 'H', ZX_KEY_H },
    { 'I',  ZX_KEY_I }, { 'J', ZX_KEY_J }, { 'K', ZX_KEY_K }, { 'L', ZX_KEY_L },
    { 'M',  ZX_KEY_M }, { 'N', ZX_KEY_N }, { 'O', ZX_KEY_O }, { 'P', ZX_KEY_P },
    { 'Q',  ZX_KEY_Q }, { 'R', ZX_KEY_R }, { 'S', ZX_KEY_S }, { 'T', ZX_KEY_T },
    { 'U',  ZX_KEY_U }, { 'V', ZX_KEY_V }, { 'W', ZX_KEY_W }, { 'X', ZX_KEY_X },
    { 'Y',  ZX_KEY_Y }, { 'Z', ZX_KEY_Z },
    /* Backspace / DEL -> CAPS SHIFT + 0 */
    { '\b', ZX_KEY_0,  .caps = true },
    { 0x7F, ZX_KEY_0,  .caps = true },
    /* SYM SHIFT combinations (ZX Spectrum 48K keyboard print) */
    { '!',  ZX_KEY_1,  .sym = true }, { '@', ZX_KEY_2, .sym = true },
    { '#',  ZX_KEY_3,  .sym = true }, { '$', ZX_KEY_4, .sym = true },
    { '%',  ZX_KEY_5,  .sym = true }, { '&', ZX_KEY_6, .sym = true },
    { '\'', ZX_KEY_7,  .sym = true }, { '(', ZX_KEY_8, .sym = true },
    { ')',  ZX_KEY_9,  .sym = true },
    { '_',  ZX_KEY_0,  .sym = true }, { '<', ZX_KEY_R, .sym = true },
    { '>',  ZX_KEY_T,  .sym = true }, { '"', ZX_KEY_P, .sym = true },
    { '+',  ZX_KEY_K,  .sym = true }, { '-', ZX_KEY_J, .sym = true },
    { '=',  ZX_KEY_L,  .sym = true }, { '*', ZX_KEY_B, .sym = true },
    { '/',  ZX_KEY_V,  .sym = true }, { '?', ZX_KEY_C, .sym = true },
    { ',',  ZX_KEY_N,  .sym = true }, { '.', ZX_KEY_M, .sym = true },
    { ';',  ZX_KEY_O,  .sym = true }, { ':', ZX_KEY_Z, .sym = true },
};

static const char_map_t *lookup_char(int ch)
{
    if (ch >= 'a' && ch <= 'z') ch -= 32;       /* fold lowercase to upper */
    for (size_t i = 0; i < (sizeof s_char_map) / (sizeof s_char_map[0]); i++) {
        if (s_char_map[i].ch == ch) return &s_char_map[i];
    }
    return NULL;
}

#define KEY_PRESS_MS       40   /* needs to span at least one 20 ms ROM scan */
#define KEY_GAP_MS         60   /* release time to defeat key-repeat debounce */

static const char *TAG = "ZXKBD";

static void keyboard_task(void *arg)
{
    (void)arg;
    uint8_t ch;
    while (1) {
        int n = usb_serial_jtag_read_bytes(&ch, 1, portMAX_DELAY);
        if (n != 1) continue;

        const char_map_t *m = lookup_char(ch);
        if (!m) {
            ESP_LOGD(TAG, "unmapped char 0x%02x", ch);
            continue;
        }
        if (m->caps) zx_keyboard_press(ZX_KEY_CAPS_SHIFT, true);
        if (m->sym)  zx_keyboard_press(ZX_KEY_SYM_SHIFT,  true);
        zx_keyboard_press(m->key, true);

        vTaskDelay(pdMS_TO_TICKS(KEY_PRESS_MS));

        zx_keyboard_press(m->key, false);
        if (m->sym)  zx_keyboard_press(ZX_KEY_SYM_SHIFT,  false);
        if (m->caps) zx_keyboard_press(ZX_KEY_CAPS_SHIFT, false);

        vTaskDelay(pdMS_TO_TICKS(KEY_GAP_MS));
    }
}

void zx_keyboard_init(void)
{
    /* When CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is the primary console, the
     * system console init has already installed this driver via VFS. Tolerate
     * the resulting ESP_ERR_INVALID_STATE so this stays correct either way. */
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    xTaskCreate(keyboard_task, "zxkbd", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "console keyboard ready on USB-Serial-JTAG");
}
