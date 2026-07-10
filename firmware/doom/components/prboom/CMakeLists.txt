# PrBoom 2.5.0 core (esp32-doom lineage, GPL-2.0 — see COPYING).
# gl_main.c/gl_texture.c are fully #if 0-wrapped upstream; excluded here.
# native/ (desktop platform layer) and w_memcache.c.disabled must stay out.
set(PRBOOM_SRCS
    am_map.c d_client.c d_deh.c d_items.c d_main.c doomdef.c doomstat.c
    dstrings.c f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c info.c
    lprintf.c m_argv.c m_bbox.c m_cheat.c m_menu.c m_misc.c m_random.c
    md5.c mmus2mid.c p_ceilng.c p_checksum.c p_doors.c p_enemy.c p_floor.c
    p_genlin.c p_inter.c p_lights.c p_map.c p_maputl.c p_mobj.c p_plats.c
    p_pspr.c p_saveg.c p_setup.c p_sight.c p_spec.c p_switch.c p_telept.c
    p_tick.c p_user.c r_bsp.c r_data.c r_demo.c r_draw.c r_filter.c r_fps.c
    r_main.c r_patch.c r_plane.c r_segs.c r_sky.c r_things.c s_sound.c
    sounds.c st_lib.c st_stuff.c tables.c v_video.c version.c w_mmap.c
    w_wad.c wi_stuff.c z_bmalloc.c z_zone.c)

idf_component_register(SRCS ${PRBOOM_SRCS}
                       INCLUDE_DIRS include
                       REQUIRES prboom-wad-tables esp_common)

target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H)

# IDF v6 defaults to -std=gnu23, which rejects `typedef enum {false, true}
# boolean` in doomtype.h; gnu17 compiles all sources clean (verified with
# the v6 toolchain, GCC 15.2).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu17
    -Wno-pointer-sign
    -Wno-error=char-subscripts -Wno-error=unused-value -Wno-error=parentheses
    -Wno-error=int-to-pointer-cast -Wno-error=maybe-uninitialized
    -Wno-error=unused-but-set-parameter -Wno-error=misleading-indentation
    -Wno-error=stringop-truncation -Wno-error=address
    -Wno-error=duplicate-decl-specifier -Wno-error=sizeof-pointer-div
    -Wno-error=nonnull -Wno-error=dangling-else -Wno-error=unused-const-variable
    -Wno-error=format-overflow -Wno-error=missing-field-initializers
    -Wno-error=implicit-fallthrough -Wno-error=type-limits)
