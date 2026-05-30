#include "zx_spectrum_emulator.h"

#include <string.h>

#include "esp_log.h"

#include "z80emu.h"

static const char *TAG = "ZXEMU";

static Z80_STATE  s_cpu;
static uint8_t    s_memory[ZX_MEMORY_SIZE];

#ifdef ZX_ROM_EMBEDDED
extern const uint8_t zx_rom_start[] asm("_binary_48_rom_start");
extern const uint8_t zx_rom_end[]   asm("_binary_48_rom_end");
#endif

static void load_rom(void)
{
    memset(s_memory, 0, sizeof(s_memory));
#ifdef ZX_ROM_EMBEDDED
    size_t len = (size_t)(zx_rom_end - zx_rom_start);
    if (len > ZX_ROM_SIZE) {
        ESP_LOGW(TAG, "ROM image is %u bytes, truncating to %u", (unsigned)len, ZX_ROM_SIZE);
        len = ZX_ROM_SIZE;
    }
    memcpy(s_memory, zx_rom_start, len);
    ESP_LOGI(TAG, "Loaded %u bytes of ROM at 0x0000", (unsigned)len);
#else
    ESP_LOGW(TAG, "No ROM embedded; ROM region is zero-filled (CPU will execute NOPs)");
#endif
}

void zx_spectrum_init(void)
{
    load_rom();
    Z80Reset(&s_cpu);
}

int zx_spectrum_run(int cycles)
{
    return Z80Emulate(&s_cpu, cycles, s_memory);
}

int zx_spectrum_interrupt(uint8_t data_on_bus)
{
    return Z80Interrupt(&s_cpu, data_on_bus, s_memory);
}

uint8_t zx_spectrum_mem_read(uint16_t address)
{
    return s_memory[address];
}

void zx_spectrum_mem_write(uint16_t address, uint8_t value)
{
    if (address >= ZX_RAM_BASE) {
        s_memory[address] = value;
    }
}

const uint8_t *zx_spectrum_memory(void)
{
    return s_memory;
}
