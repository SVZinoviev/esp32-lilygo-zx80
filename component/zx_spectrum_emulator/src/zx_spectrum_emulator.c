#include "zx_spectrum_emulator.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#include "z80emu.h"
#include "zx_tap.h"

static const char *TAG = "ZXEMU";

/* LD-BYTES entry in the 48K ROM. We overwrite the first two bytes (INC D /
 * EX AF,AF') with ED FE so the catch-undefined-ED hook fires here and never
 * elsewhere in normal ROM execution. */
#define ZX_TAP_TRAP_ADDR  0x0556

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

/* Emulate LD-BYTES (0x0556) from the C side. Z80 register conventions on
 * entry (matching the real ROM routine):
 *   IX  = destination address
 *   DE  = number of bytes to load (excludes the flag and checksum)
 *   A   = expected flag (0x00 = header, 0xFF = data)
 *   F.C = 1 -> LOAD, 0 -> VERIFY
 * On exit we set F.C to indicate success/failure, decrement DE and bump IX
 * past the loaded data on success (matching what ROM consumers expect), then
 * RET by popping the saved address. */
static void handle_tap_trap(void)
{
    uint16_t ix   = s_cpu.registers.word[Z80_IX];
    uint16_t de   = s_cpu.registers.word[Z80_DE];
    uint8_t  want = s_cpu.registers.byte[Z80_A];
    bool     load_mode = (s_cpu.registers.byte[Z80_F] & Z80_C_FLAG) != 0;

    uint8_t       got_flag = 0;
    const uint8_t *got_data = NULL;
    size_t        got_len   = 0;
    bool got = zx_tap_next_block(&got_flag, &got_data, &got_len);
    bool ok  = got && (got_flag == want);

    if (ok && load_mode && got_data) {
        size_t n = (de < got_len) ? de : got_len;
        for (size_t i = 0; i < n; i++) {
            s_memory[(uint16_t)(ix + i)] = got_data[i];
        }
        s_cpu.registers.word[Z80_IX] = (uint16_t)(ix + n);
        s_cpu.registers.word[Z80_DE] = (uint16_t)(de - n);
    }

    uint8_t f = s_cpu.registers.byte[Z80_F];
    if (ok) f |= Z80_C_FLAG;
    else    f &= (uint8_t)~Z80_C_FLAG;
    s_cpu.registers.byte[Z80_F] = f;

    /* Pop return address, RET */
    uint16_t sp  = s_cpu.registers.word[Z80_SP];
    uint16_t ret = (uint16_t)(s_memory[sp] | ((uint16_t)s_memory[(uint16_t)(sp + 1)] << 8));
    s_cpu.registers.word[Z80_SP] = (uint16_t)(sp + 2);
    s_cpu.pc     = ret;
    s_cpu.status = 0;

    ESP_LOGI(TAG, "LD-BYTES: want=0x%02x req_len=%u ix=0x%04x  got=0x%02x got_len=%u  %s%s",
             want, (unsigned)de, ix, got_flag, (unsigned)got_len,
             ok ? "OK" : "FAIL", load_mode ? "" : " (verify)");
}

void zx_spectrum_init(void)
{
    load_rom();

    /* Plant the LD-BYTES trap. ED FE is an undefined ED-prefixed opcode and
     * raises Z80_STATUS_ED_UNDEFINED with PC pointing at the ED byte. */
    s_memory[ZX_TAP_TRAP_ADDR]     = 0xED;
    s_memory[ZX_TAP_TRAP_ADDR + 1] = 0xFE;

    zx_tap_init();

    Z80Reset(&s_cpu);
}

int zx_spectrum_run(int cycles)
{
    int total = 0;
    while (cycles > 0) {
        int used = Z80Emulate(&s_cpu, cycles, s_memory);
        total  += used;
        cycles -= used;

        if (s_cpu.status == Z80_STATUS_ED_UNDEFINED) {
            if (s_cpu.pc == ZX_TAP_TRAP_ADDR) {
                handle_tap_trap();
            } else {
                /* Stray undefined ED prefix elsewhere - treat as NOP and move on. */
                s_cpu.pc     = (uint16_t)(s_cpu.pc + 2);
                s_cpu.status = 0;
            }
            continue;
        }

        /* status == 0 means cycles exhausted; any other status is unexpected
         * given our z80config, so bail in either case. */
        break;
    }
    return total;
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
