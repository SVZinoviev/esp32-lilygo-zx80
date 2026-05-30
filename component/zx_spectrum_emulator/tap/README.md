# Embedded ZX Spectrum tape

Drop a `.TAP` file here as `prog.tap`. The build system embeds it into flash
via `EMBED_FILES`; at runtime the LD-BYTES trap in `zx_spectrum_emulator.c`
patches the ROM at `0x0556` with `ED FE` (an undefined Z80 opcode that
triggers `Z80_STATUS_ED_UNDEFINED`) and synthesises each `LD-BYTES` call from
the next block of this file.

## Using it from BASIC

After power-on, at the BASIC prompt:

    LOAD ""             (J then SYM+P twice)

Just like a real Spectrum: the ROM walks the headers it sees, prints
`Bytes: NAME` for code, splices BASIC into RAM, runs autostart lines etc.
The trap doesn't fast-forward the rest of the ROM logic, so anything that
worked on real hardware should work here.

`SAVE` is **not** trapped, so it will hang trying to drive the tape OUT
port. `VERIFY` works but doesn't actually verify - it just reports success.

If you want to re-load without a power-cycle, call `zx_tap_rewind()` from
your host code (or extend BASIC to call it).

## Tape format reminder

```
[length: 2 bytes LE][flag: 1 byte][data: length-2 bytes][checksum: 1 byte]
```

flag = `0x00` for the 17-byte header, `0xFF` for data. Multiple
header/data pairs may be concatenated for programs that load CODE blocks
after a BASIC loader.
