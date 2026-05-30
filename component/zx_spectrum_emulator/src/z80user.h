/* z80user.h
 * Memory and I/O bus glue between z80emu and the ZX Spectrum memory map.
 *
 * The `context` pointer passed to Z80Emulate() is treated as `uint8_t *`
 * pointing at the start of the 64k linear address space. Writes below
 * ZX_RAM_BASE (the 16k ROM region) are silently dropped. I/O ports are
 * stubbed: input returns 0xFF (idle bus), output is ignored.
 */

#ifndef ZX_SPECTRUM_Z80USER_H_
#define ZX_SPECTRUM_Z80USER_H_

#include <stdint.h>

#include "zx_keyboard.h"
#include "zx_spectrum_emulator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define Z80_READ_BYTE(address, x)                                          \
{                                                                          \
    (x) = ((const uint8_t *)context)[(address) & 0xffff];                  \
}

#define Z80_FETCH_BYTE(address, x)  Z80_READ_BYTE((address), (x))

#define Z80_READ_WORD(address, x)                                          \
{                                                                          \
    const uint8_t *_m = (const uint8_t *)context;                          \
    (x) = _m[(address) & 0xffff]                                           \
        | (_m[((address) + 1) & 0xffff] << 8);                             \
}

#define Z80_FETCH_WORD(address, x)  Z80_READ_WORD((address), (x))

#define Z80_WRITE_BYTE(address, x)                                         \
{                                                                          \
    uint16_t _a = (address) & 0xffff;                                      \
    if (_a >= ZX_RAM_BASE) {                                               \
        ((uint8_t *)context)[_a] = (uint8_t)(x);                           \
    }                                                                      \
}

#define Z80_WRITE_WORD(address, x)                                         \
{                                                                          \
    uint16_t _a0 = (address) & 0xffff;                                     \
    uint16_t _a1 = ((address) + 1) & 0xffff;                               \
    uint8_t *_m = (uint8_t *)context;                                      \
    if (_a0 >= ZX_RAM_BASE) _m[_a0] = (uint8_t)(x);                        \
    if (_a1 >= ZX_RAM_BASE) _m[_a1] = (uint8_t)((x) >> 8);                 \
}

#define Z80_READ_WORD_INTERRUPT(address, x)   Z80_READ_WORD((address), (x))
#define Z80_WRITE_WORD_INTERRUPT(address, x)  Z80_WRITE_WORD((address), (x))

/* z80emu hands us only the low byte of the I/O port (the `n` for IN A,(n)
 * or the `C` for IN r,(C) / INI / IND / INIR / INDR). The Z80 actually places
 * a full 16-bit address on the bus: A on the high byte for IN A,(n), B on
 * the high byte for the BC-addressed forms. The ZX Spectrum ULA decodes the
 * keyboard half-rows from those high address lines, so we must reconstruct
 * the full port before handing it to the matrix reader.
 *
 * `A` and `B` here are the z80emu register accessors from macros.h, which is
 * always included in the same translation unit (z80emu.c) before this macro
 * is expanded. `instruction` and `IN_A_N` come from the local int and the
 * instructions.h enum, both in scope at expansion site. */
#define Z80_INPUT_BYTE(port, x)                                            \
{                                                                          \
    uint8_t _hi = (instruction == IN_A_N) ? A : B;                         \
    (x) = zx_keyboard_read(((uint16_t)_hi << 8) | (uint8_t)(port));        \
}

#define Z80_OUTPUT_BYTE(port, x)                                           \
{                                                                          \
    (void)(port);                                                          \
    (void)(x);                                                              \
}

#ifdef __cplusplus
}
#endif

#endif
