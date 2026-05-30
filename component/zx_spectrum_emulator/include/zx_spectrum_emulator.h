#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZX_MEMORY_SIZE   (64 * 1024)
#define ZX_ROM_SIZE      (16 * 1024)
#define ZX_RAM_SIZE      (ZX_MEMORY_SIZE - ZX_ROM_SIZE)
#define ZX_ROM_BASE      0x0000u
#define ZX_RAM_BASE      0x4000u

void     zx_spectrum_init(void);
int      zx_spectrum_run(int cycles);
int      zx_spectrum_interrupt(uint8_t data_on_bus);

uint8_t  zx_spectrum_mem_read(uint16_t address);
void     zx_spectrum_mem_write(uint16_t address, uint8_t value);

const uint8_t *zx_spectrum_memory(void);

#ifdef __cplusplus
}
#endif
