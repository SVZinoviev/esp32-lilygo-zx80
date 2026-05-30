#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZX_KEY_CAPS_SHIFT, ZX_KEY_Z, ZX_KEY_X, ZX_KEY_C, ZX_KEY_V,
    ZX_KEY_A, ZX_KEY_S, ZX_KEY_D, ZX_KEY_F, ZX_KEY_G,
    ZX_KEY_Q, ZX_KEY_W, ZX_KEY_E, ZX_KEY_R, ZX_KEY_T,
    ZX_KEY_1, ZX_KEY_2, ZX_KEY_3, ZX_KEY_4, ZX_KEY_5,
    ZX_KEY_0, ZX_KEY_9, ZX_KEY_8, ZX_KEY_7, ZX_KEY_6,
    ZX_KEY_P, ZX_KEY_O, ZX_KEY_I, ZX_KEY_U, ZX_KEY_Y,
    ZX_KEY_ENTER, ZX_KEY_L, ZX_KEY_K, ZX_KEY_J, ZX_KEY_H,
    ZX_KEY_SPACE, ZX_KEY_SYM_SHIFT, ZX_KEY_M, ZX_KEY_N, ZX_KEY_B,
    ZX_KEY_COUNT
} zx_key_t;

/* Start the matrix scanner and the UART0 console-reader task. */
void zx_keyboard_init(void);

/* Directly drive the matrix from any source (UART task, on-screen kbd, etc). */
void zx_keyboard_press(zx_key_t key, bool down);

/* Read the matrix as the Z80 IN A,(0xFE)-family ports see it. The full 16-bit
 * port is passed; only the high byte selects half-rows. */
uint8_t zx_keyboard_read(uint16_t port);

#ifdef __cplusplus
}
#endif
