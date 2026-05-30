# ZX Spectrum 48K emulator for ESP32-S3 / LilyGo T-Display

A ZX Spectrum 48K emulator running on an ESP32-S3 with a ST7789v 170×320 LCD
(LilyGo T-Display S3 and compatible boards). The Z80 core is the public-
domain `z80emu` from Lin Ke-Fong, wired to a flat 64 KB memory bus with the
original Sinclair ROM loaded into the low 16 KB. Display output is rendered
in landscape (320×170), the keyboard is driven over USB-Serial-JTAG, and a
ROM-trap fast loader handles `.TAP` files.

## Hardware

* ESP32-S3, target `esp32s3`. Tested on a LilyGo T-Display S3.
* Built-in ST7789v 170×320 LCD driven by the on-chip i80 8-bit parallel bus
  (data lines GPIO39-42 / 45-48, control RST=5, CS=6, DC=7, WR=8, RD=9,
  backlight GPIO38). Board power-enable is GPIO15.
* Native USB on GPIO19/20, used as the primary console (USB-Serial-JTAG) for
  both `printf` log output and emulated-keyboard input.
* Pin definitions are in `main/st7789v_esp_driver_board.h` and
  `main/board_config.h`. Adjust if you wire it to a different board.

## Features and scope

* Full Z80 instruction set including documented and undocumented flags
  (passes zexdoc / zexall in upstream z80emu).
* 64 KB linear address space; writes below `0x4000` are silently dropped to
  preserve the ROM image.
* 50 Hz frame-boundary maskable interrupt drives the ROM's keyboard scan and
  FLASH timer (the FLASH attribute itself is not animated yet).
* Video renderer reads the ZX screen memory in the proper interleaved
  layout and converts to a byte-swapped RGB565 framebuffer pushed to the
  panel by DMA. Visible region is 256×170 - the bottom 22 ZX scanlines are
  clipped; `ZX_VIDEO_Y_OFFSET` (default `22`, range `0..22`) selects which
  170-line slice of the 192-line ZX screen is shown.
* Keyboard bridge maps USB-Serial-JTAG input bytes to the 8×5 ZX matrix,
  including SYM / CAPS combinations and ANSI cursor-arrow escape sequences.
  Held keys remain pressed in the matrix until the host stops repeating
  (idle timeout 50 ms).
* `.TAP` loader implemented as a ROM trap: the first two bytes of
  `LD-BYTES` (`0x0556`) are patched to `ED FE`, the `Z80_STATUS_ED_UNDEFINED`
  catch fires when the ROM calls `LD-BYTES`, and the trap handler memcpy's
  the next block from the embedded tape image. From BASIC this means
  `LOAD ""` behaves like fast-tape - chaining, autostart and CODE blocks all
  work because the ROM is still driving the load logic.

### Not implemented yet / known limitations

* **Not clock-accurate.** The Z80 core runs as fast as the host can drive it,
  not at a real 3.5 MHz. Cycles-per-frame is fixed at 70 000 with one
  maskable interrupt per outer loop iteration, which is enough for the ROM
  and BASIC, but anything relying on precise instruction timing (multicolour
  effects, raster splits, border-timing music, fast loaders that decode
  pulses, etc.) will not work correctly.
* **Right edge of the LCD is uninitialised garbage.** The ZX screen is
  256 px wide, the LCD is 320 px wide in landscape, and the renderer only
  pushes the 256×170 frame at `(0,0)` - the rightmost 64 px column is
  whatever was last in panel RAM (often boot garbage). A one-shot clear or
  centred render would fix this; not done yet.
* `OUT (0xFE)` is stubbed - no border colour, no beeper / MIC output.
* `SAVE` is not trapped - typing `SAVE ""` will hang.
* `VERIFY` returns success without comparing bytes.
* The FLASH attribute (bit 7) is ignored in rendering, so the BASIC cursor
  is solid rather than blinking.
* `kempston` / other joystick interfaces, AY chip, 128K paging - none of it.

## Layout

```
main/
  main.c                        app entry, power-on, init sequence
  st7789v_esp_driver.{c,h}      i80-bus ST7789v init + draw_bitmap wrapper
  st7789v_esp_driver_board.h    LCD pin numbers
  zx_video.{c,h}                ZX screen -> RGB565 framebuffer, DMA push
  board_config.h                generic board pins
component/zx_spectrum_emulator/
  include/
    zx_spectrum_emulator.h      init / run / interrupt / memory accessors
    zx_keyboard.h               matrix press/read + key enum
    zx_tap.h                    .tap init / rewind / next_block
  src/
    zx_spectrum_emulator.c      memory + run loop + LD-BYTES trap handler
    zx_keyboard.c               USB-JTAG console -> matrix
    zx_tap.c                    .tap blob parser
    z80user.h                   z80emu memory / I/O macros (matrix + writes)
  vendor/z80emu/                Lin Ke-Fong's Z80 core, unmodified except
                                z80config.h (catch ED_UNDEFINED + status
                                enum-name compatibility aliases)
  rom/                          drop 48.rom here (16384 bytes)
  tap/                          drop prog.tap here (optional)
```

## Building and flashing

Prerequisites: ESP-IDF v5.5 or later, target `esp32s3`.

1. Drop the original Sinclair 48K ROM image at
   `component/zx_spectrum_emulator/rom/48.rom` (exactly 16 384 bytes).
   Without it the build still succeeds, but the CPU executes NOPs from
   reset and you get no boot screen.
2. (Optional) Drop a tape image at
   `component/zx_spectrum_emulator/tap/prog.tap`. Without it the tape trap
   returns "no tape" on every `LD-BYTES` call.
3. Build and flash:

   ```
   idf.py set-target esp32s3
   idf.py build
   idf.py -p /dev/ttyACM0 flash monitor
   ```

   The console is on the chip's native USB; on Linux it enumerates as
   `/dev/ttyACM0`. The same endpoint carries log output and emulated
   keyboard input.

CMake re-checks the existence of `rom/48.rom` and `tap/prog.tap` on every
build via `CMAKE_CONFIGURE_DEPENDS`, so adding or replacing those files
does not require an explicit `idf.py reconfigure`.

## Using it

After flashing and resetting you should see the familiar `© 1982 Sinclair
Research Ltd` banner at the bottom of the LCD and a flashing `K` cursor in
the editing area.

Console input is forwarded to the ZX keyboard with the following
conventions:

| Host key                       | ZX key                |
| ------------------------------ | --------------------- |
| Letters, digits, space         | direct                |
| Enter                          | ENTER                 |
| Backspace / Delete             | CAPS SHIFT + 0        |
| Cursor arrows (`ESC [ A/B/C/D`)| CAPS SHIFT + 7/6/8/5  |
| `!@#$%&'()`                    | SYM SHIFT + 1..9      |
| `<>"+-=*/?,.;:`                | SYM SHIFT + matching ZX key |

Typing `LOAD ""` and pressing Enter at the BASIC prompt loads the embedded
tape. The ROM walks the headers, prints `Bytes: NAME` for code blocks,
splices BASIC into RAM, and runs autostart lines just like real hardware.
`zx_tap_rewind()` can be called from host code to restart the same tape
without a reboot.

## License

* z80emu - "this code is free, do whatever you want with it" (Lin
  Ke-Fong). Vendor copy under `component/zx_spectrum_emulator/vendor/z80emu`
  with the original copyright header preserved.
* The ZX Spectrum ROM is copyrighted by Amstrad plc, who granted permission
  for non-commercial emulator use in 1999. The ROM image is not
  redistributed here - you must supply your own `48.rom`.
* Original code in this repository is published under the project's `LICENSE`
  file.
