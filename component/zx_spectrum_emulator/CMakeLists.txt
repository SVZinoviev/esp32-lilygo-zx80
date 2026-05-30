set(rom_image "${CMAKE_CURRENT_SOURCE_DIR}/rom/48.rom")

set(embed_files)
if(EXISTS "${rom_image}")
    set(embed_files "rom/48.rom")
endif()

idf_component_register(
    SRCS        "src/zx_spectrum_emulator.c"
                "vendor/z80emu/z80emu.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src" "vendor/z80emu"
    REQUIRES    log
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
