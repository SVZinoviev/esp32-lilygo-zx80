set(rom_image "${CMAKE_CURRENT_SOURCE_DIR}/rom/48.rom")
set(tap_image "${CMAKE_CURRENT_SOURCE_DIR}/tap/prog.tap")

# Re-run CMake configure whenever these files appear, disappear, or change,
# so dropping a new prog.tap / 48.rom into the source tree picks up without
# a manual `idf.py reconfigure`.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${rom_image}"
    "${tap_image}")

set(embed_files)
if(EXISTS "${rom_image}")
    list(APPEND embed_files "rom/48.rom")
endif()
if(EXISTS "${tap_image}")
    list(APPEND embed_files "tap/prog.tap")
endif()

idf_component_register(
    SRCS        "src/zx_spectrum_emulator.c"
                "src/zx_keyboard.c"
                "src/zx_tap.c"
                "vendor/z80emu/z80emu.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src" "vendor/z80emu"
    REQUIRES    log driver
    EMBED_FILES ${embed_files}
)

if(EXISTS "${rom_image}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ZX_ROM_EMBEDDED=1)
    message(STATUS "zx_spectrum_emulator: embedding ROM from ${rom_image}")
else()
    message(WARNING
        "zx_spectrum_emulator: rom/48.rom not found - ROM region will be zero-filled. "
        "Drop a 16384-byte ZX Spectrum 48K ROM at "
        "component/zx_spectrum_emulator/rom/48.rom to embed it.")
endif()

if(EXISTS "${tap_image}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ZX_TAP_EMBEDDED=1)
    message(STATUS "zx_spectrum_emulator: embedding TAP from ${tap_image}")
else()
    message(STATUS
        "zx_spectrum_emulator: no tap/prog.tap - LD-BYTES trap will return tape errors. "
        "Drop a .TAP file at component/zx_spectrum_emulator/tap/prog.tap to embed it.")
endif()
