# OPL2 music synth for the DOOM port — sources from prboom-plus (GPL-2.0,
# headers intact): DBOPL emulator (DOSBox lineage), chocolate-doom-lineage
# OPL driver + MIDI/MUS handling, prboom-plus oplplayer (music_player_t).
# Rendering is synchronous (OPL_Render_Samples clocked by sample time), so
# it drops straight into our I2S mixer task — no timers/threads needed.
idf_component_register(SRCS src/dbopl.c src/opl.c src/opl_queue.c
                            src/midifile.c src/mus2mid.c src/memio.c
                            src/oplplayer.c
                       INCLUDE_DIRS src
                       REQUIRES prboom)

# dboolean/PACKEDATTR come from prboom-plus's doomtype.h; our prboom 2.5.0
# header predates them. LittleShort/LittleLong (m_swap.h drift) live in
# gf_compat.h, force-included below — CMake silently drops function-like
# macros from COMPILE_DEFINITIONS.
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H
    dboolean=int "PACKEDATTR=__attribute__((packed))")

# Same C standard/warning set as the prboom core (doomtype.h boolean enum
# vs C23; vintage code trips modern -Werror defaults).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu17
    -include ${CMAKE_CURRENT_LIST_DIR}/src/gf_compat.h
    -Wno-pointer-sign
    -Wno-error=char-subscripts -Wno-error=unused-value -Wno-error=parentheses
    -Wno-error=int-to-pointer-cast -Wno-error=maybe-uninitialized
    -Wno-error=unused-but-set-parameter -Wno-error=misleading-indentation
    -Wno-error=stringop-truncation -Wno-error=address
    -Wno-error=duplicate-decl-specifier -Wno-error=sizeof-pointer-div
    -Wno-error=nonnull -Wno-error=dangling-else -Wno-error=unused-const-variable
    -Wno-error=format-overflow -Wno-error=missing-field-initializers
    -Wno-error=implicit-fallthrough -Wno-error=type-limits
    -Wno-error=format)
