#include "zx_tap.h"

#include "esp_log.h"

static const char *TAG = "ZXTAP";

#ifdef ZX_TAP_EMBEDDED
extern const uint8_t zx_tap_blob_start[] asm("_binary_prog_tap_start");
extern const uint8_t zx_tap_blob_end[]   asm("_binary_prog_tap_end");
#endif

static const uint8_t *s_buf;
static size_t         s_size;
static size_t         s_pos;

void zx_tap_set_blob(const uint8_t *data, size_t size)
{
    s_buf  = data;
    s_size = size;
    s_pos  = 0;
}

void zx_tap_init(void)
{
#ifdef ZX_TAP_EMBEDDED
    zx_tap_set_blob(zx_tap_blob_start, (size_t)(zx_tap_blob_end - zx_tap_blob_start));
    ESP_LOGI(TAG, "embedded TAP: %u bytes", (unsigned)s_size);
#else
    zx_tap_set_blob(NULL, 0);
    ESP_LOGI(TAG, "no embedded TAP; LD-BYTES trap will report tape errors");
#endif
}

void zx_tap_rewind(void)
{
    s_pos = 0;
}

bool zx_tap_next_block(uint8_t       *out_flag,
                       const uint8_t **out_data,
                       size_t         *out_len)
{
    if (!s_buf || s_pos + 2 > s_size) {
        return false;
    }
    uint16_t block_len = (uint16_t)(s_buf[s_pos]
                                  | ((uint16_t)s_buf[s_pos + 1] << 8));
    s_pos += 2;
    if (block_len < 2 || s_pos + block_len > s_size) {
        ESP_LOGW(TAG, "malformed TAP at offset %u (block_len=%u)",
                 (unsigned)(s_pos - 2), block_len);
        s_pos = s_size; /* stop further reads */
        return false;
    }

    *out_flag = s_buf[s_pos];
    *out_data = &s_buf[s_pos + 1];
    *out_len  = (size_t)(block_len - 2); /* exclude flag and checksum */

    s_pos += block_len;
    return true;
}
