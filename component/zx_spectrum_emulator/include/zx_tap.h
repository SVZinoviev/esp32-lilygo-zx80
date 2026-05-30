#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pick up an embedded prog.tap (if any) and rewind the read cursor. Called
 * automatically by zx_spectrum_init(). */
void zx_tap_init(void);

/* Replace the active tape image with a buffer supplied at runtime. Pass
 * NULL/0 to detach the tape. */
void zx_tap_set_blob(const uint8_t *data, size_t size);

/* Move the read cursor back to the first block. Useful if the user wants to
 * re-LOAD the same tape from BASIC without rebooting. */
void zx_tap_rewind(void);

/* Pop the next block off the tape. Returns true and fills *out_flag, the
 * pointer to the block's body (without flag/checksum), and *out_len, on
 * success. Returns false at EOF or if the tape is malformed. */
bool zx_tap_next_block(uint8_t       *out_flag,
                       const uint8_t **out_data,
                       size_t         *out_len);

#ifdef __cplusplus
}
#endif
