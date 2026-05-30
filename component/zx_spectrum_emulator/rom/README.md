# ZX Spectrum 48K ROM

Drop the original 16384-byte ZX Spectrum 48K ROM here as `48.rom`. The build
system will embed it into flash via ESP-IDF `EMBED_FILES` and the emulator
will copy it into the 0x0000-0x3FFF ROM region at reset.

If `48.rom` is absent, the component still builds but the ROM region is
zero-filled and the CPU will execute NOPs from reset.

This file is not redistributed here; the ROM is copyrighted by Amstrad plc,
who granted permission for non-commercial emulator use.
