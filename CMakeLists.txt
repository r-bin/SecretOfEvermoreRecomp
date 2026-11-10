cmake_minimum_required(VERSION 3.20)
project(SecretOfEvermoreSNESRecomp C CXX)

set(CMAKE_C_STANDARD 11)

# Release pin for lobby matching. Release builds stamp the VERSION file so two
# peers can prove they are the same build; everything else stamps "dev".
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _GAME_VER_RAW)
string(STRIP "${_GAME_VER_RAW}" GAME_RELEASE_VERSION)
set(SNESRECOMP_BUILD_VERSION "" CACHE STRING
    "Lobby release pin (empty = Release uses VERSION, else dev)")
if(SNESRECOMP_BUILD_VERSION STREQUAL "")
    get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_is_multi)
        set(SNESRECOMP_BUILD_VERSION "$<IF:$<CONFIG:Release>,${GAME_RELEASE_VERSION},dev>")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(SNESRECOMP_BUILD_VERSION "${GAME_RELEASE_VERSION}")
    else()
        set(SNESRECOMP_BUILD_VERSION "dev")
    endif()
endif()

set(SNESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")
if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
    message(FATAL_ERROR
        "snesrecomp submodule missing at ${SNESRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive")
endif()
# Mod packages are part of every project, not an option. The launcher's Mods
# page, the .snesmod loader, the trusted-plugin registry and netplay's mod-set
# gate all hang off this one switch; a build without it cannot exchange mods
# with a peer that has them, and the framework says so at every lobby init.
# Must precede the include: runner.cmake reads it to add the loader sources.
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL
    "Mod package loader + trusted plugins (required for every title)" FORCE)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

add_executable(SecretOfEvermoreSNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    src/main.c
    src/host_contract.c
    src/game_rtl.c
    src/soe_frame_driver.c
    src/soe_last_cycle.c
    src/soe_ppu_fixes.c
    src/soe_bindings.c
    src/soe_cost.c
    src/soe_mesen_state.c
    src/soe_widescreen.c
    src/soe_ws_sprites.c
    src/soe_ws_alive.c
    src/soe_cheats.c
    src/gen_stubs.c
)

# Routines replaced with reconstructed C (src/soe_bindings.c, docs/game-rtl.md 4b). src/bound-c/ is an imported copy:
# everscript-vscode's reconstructed-c is the source, tools/import.mts refreshes the copy. Point SOE_RECONSTRUCTED_C
# at that folder to try C before exporting it.
set(SOE_RECONSTRUCTED_C "${CMAKE_SOURCE_DIR}/src/bound-c" CACHE PATH "Folder with the bound routines' C and include/")
set(SOE_BOUND_C 80/80859B_random_number_generator.c 8F/8FA914_update_entity_plane.c 90/909DE8_eval_tile_collision.c
    80/809685_init_entity_pool.c 8F/8FC773_entity_sprite_depth.c 8F/8F9F05_companion_metatile_raycast_line_stepper.c
    8F/8F9817_field_spare_time_loop.c 80/80858C_wait_for_nmi.c 80/808650_vram_dma_upload_tile_block.c
    90/90A1B4_map_anim_tile_channel_tick_and_vram_stream.c 80/80960A_oam_depth_sorted_sprite_insert_node.c
    8F/8FC6C5_sprite_sorting_canopy_occlusion_and_render_depth.c 8F/8FAA51_direction_index_from_signed_byte_pair.c
    90/908124_anim_start_record.c 8F/8FA723_companion_waypoint_history_poll.c
    8E/8EE5FC_audio_master_volume_fade_tick.c 8F/8FA963_elevation_vector_shear_step.c
    8F/8FA97D_elevation_manhattan_distance_eval.c 8F/8FA999_elevation_compass_direction_resolve.c)
foreach(f ${SOE_BOUND_C})
    target_sources(SecretOfEvermoreSNESRecomp PRIVATE "${SOE_RECONSTRUCTED_C}/${f}")
    set_source_files_properties("${SOE_RECONSTRUCTED_C}/${f}" PROPERTIES COMPILE_DEFINITIONS SOE_COST)
endforeach()
target_include_directories(SecretOfEvermoreSNESRecomp PRIVATE "${SOE_RECONSTRUCTED_C}/include")
target_compile_definitions(SecretOfEvermoreSNESRecomp PRIVATE SOE_RECONSTRUCTED_C)

# Tests (docs/testing.md). compare_units needs node only; the others your own ROM: -DSOE_TEST_ROM=/path/to/rom.sfc.
enable_testing()
find_program(SOE_NODE node)
if(SOE_NODE)
    add_test(NAME compare_units COMMAND ${SOE_NODE} ${CMAKE_SOURCE_DIR}/tests/compare-units.test.mts)
    add_test(NAME import_units COMMAND ${SOE_NODE} ${CMAKE_SOURCE_DIR}/tests/import.test.mts)
endif()
set(SOE_TEST_ROM "" CACHE FILEPATH "Your own ROM, for the ROM-backed ctest tests (empty: none run)")
if(SOE_TEST_ROM)
    add_test(NAME reset_equals_boot
             COMMAND bash ${CMAKE_SOURCE_DIR}/tests/reset_equals_boot.sh
                     $<TARGET_FILE:SecretOfEvermoreSNESRecomp> ${SOE_TEST_ROM})
    set_tests_properties(reset_equals_boot PROPERTIES TIMEOUT 120)
    add_test(NAME save_state_resume
             COMMAND bash ${CMAKE_SOURCE_DIR}/tests/save_state_resume.sh
                     $<TARGET_FILE:SecretOfEvermoreSNESRecomp> ${SOE_TEST_ROM})
    set_tests_properties(save_state_resume PROPERTIES TIMEOUT 240)
    if(SOE_NODE)
        foreach(replay 01-no-inputs-1-loop 02-new-game-to-flowers)
            string(SUBSTRING ${replay} 0 2 n)
            add_test(NAME replay_${n}_recorded
                     COMMAND bash ${CMAKE_SOURCE_DIR}/tests/replay_recorded.sh
                             $<TARGET_FILE:SecretOfEvermoreSNESRecomp> ${SOE_TEST_ROM} ${replay})
            set_tests_properties(replay_${n}_recorded PROPERTIES TIMEOUT 1800)
            # The same stored MesenCE run with widescreen on (21:9): widescreen may change only
            # how the frame is drawn, never the game (docs/widescreen/progress.md).
            add_test(NAME replay_${n}_widescreen
                     COMMAND bash ${CMAKE_SOURCE_DIR}/tests/replay_recorded.sh
                             $<TARGET_FILE:SecretOfEvermoreSNESRecomp> ${SOE_TEST_ROM} ${replay})
            set_tests_properties(replay_${n}_widescreen PROPERTIES TIMEOUT 1800
                                 ENVIRONMENT "SOE_WIDESCREEN=1;SNESRECOMP_WS_EXTRA=96")
        endforeach()
    endif()
endif()

# ROM identity. rom_identity.txt is the one place the digests live: the build
# turns it into snesrecomp_rom_identity.h (SNESRECOMP_ROM_EXPECTED_SHA256 and
# friends) for src/main.c, and tools/regen.sh, scripts/package_release.sh and
# the release workflow read the same file. A revision bump is one edit there.
snesrecomp_rom_identity(SecretOfEvermoreSNESRecomp ${CMAKE_SOURCE_DIR}/rom_identity.txt)

# Generated recompiler output. Not committed: it is derived from a copyrighted
# ROM, so every developer regenerates it locally from their own copy -- and a
# release does not ship it either. With -DSNESRECOMP_SETUP_HOST=ON and an empty
# src/gen/, this builds the SETUP HOST instead: the same executable minus the
# recompiled code, whose only path is the launcher's Generate & rebuild wizard.
# That is what release zips contain; the player's machine produces the
# playable binary from their own ROM. Without the option an empty src/gen/ is
# still a hard error (the framework owns the message).
snesrecomp_target_generated_code(SecretOfEvermoreSNESRecomp ${CMAKE_SOURCE_DIR}/src/gen)

# The mod catalog. This names WHERE the packages are; the framework owns the
# layout they are staged into beside the executable, and stages them on every
# build OF THE EXECUTABLE -- including the launcher's own Generate & rebuild on
# a player's machine, which builds only this target and would skip a sibling
# ALL target. mods/preloaded/packages/ holds reviewed, default-disabled
# packages; players install .snesmod archives through the launcher.
# snesrecomp/docs/MOD_PACKAGES.md.
#
# It used to be `snesrecomp_target_stage_dir(... ${CMAKE_SOURCE_DIR}/mods mods)`
# -- the destination spelled per title, which is how three ports in the same
# workspace ended up shipping an empty Mods page and nothing said so. Declaring
# it is now also enforced: a project with packages that never calls this fails
# to configure.
snesrecomp_target_mod_catalog(SecretOfEvermoreSNESRecomp ${CMAKE_SOURCE_DIR}/mods/preloaded)
# Any other directory the game reads exe-relative (a translations/ table, ...)
# is staged with snesrecomp_target_stage_dir, never with a separate ALL target.

target_include_directories(SecretOfEvermoreSNESRecomp PRIVATE
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    # common_rtl.c includes "config.h": the desktop key-map / Config contract
    # the framework host supplies (snesrecomp_target_desktop_host below).
    ${SNESRECOMP_ROOT}/runner/src/desktop
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/recomp
)
target_link_libraries(SecretOfEvermoreSNESRecomp PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES})
target_compile_definitions(SecretOfEvermoreSNESRecomp PRIVATE
    SNES_GAME_VERSION="${SNESRECOMP_BUILD_VERSION}"
    SNES_GAME_TITLE="Secret of Evermore"
)
snesrecomp_target_sdl(SecretOfEvermoreSNESRecomp)

# The desktop host: launcher flow, ROM resolution, config.ini + [KeyMap],
# window + SDL/OpenGL presenters, audio, gamepads, the save-state browser and
# rewind filmstrip, the pacing clock, crash handlers and the post-mortem
# report. src/main.c is the shim that names this title and supplies its
# hooks; the host is the framework's and improves on a submodule pull.
# Add TIER2 once this port reads the interpreter-gap manifest after each run.
snesrecomp_target_desktop_host(SecretOfEvermoreSNESRecomp)
# rom.cfg (the ROM the wizard set this project up from; git-ignored) is
# staged beside the executable when none is there yet, so the first launch
# opens on that dump rather than asking for one.
snesrecomp_target_rom_cache(SecretOfEvermoreSNESRecomp)

# Shared Dear ImGui pre-boot launcher (ROM picker, verification, display /
# audio / input settings). Self-contained: it brings its own ImGui, GL link and
# staged assets, and defines RECOMP_LAUNCHER so src/main.c compiles its GUI path.
set(RECOMP_UI_ROOT "${CMAKE_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Root directory of the recomp-ui launcher repo" FORCE)
if(NOT EXISTS "${RECOMP_UI_ROOT}/recomp_ui.cmake")
    message(FATAL_ERROR
        "recomp-ui missing at ${RECOMP_UI_ROOT}.\n"
        "Run: git submodule update --init --recursive recomp-ui")
endif()
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
# BOXART stages launcher_assets/img/boxart.tga beside the exe; the launcher
# shows assets/img/boxart.tga by default. EXISTS-guarded in recomp_ui.cmake,
# so this configures cleanly before any art is fetched.
recomp_target_launcher_ui(SecretOfEvermoreSNESRecomp CONSOLE snes
    BOXART "${CMAKE_SOURCE_DIR}/launcher_assets/img/boxart.tga")

# Generate & rebuild from inside the launcher: pick ROM -> recompile locally
# -> cmake --build -> relaunch. The implementation is the framework's; this
# project contributes no configuration (snesrecomp_codegen_host_autowire in
# src/main.c). recomp-ui/src is on the include path because the host speaks
# RecompLauncherCGameInfo.
target_sources(SecretOfEvermoreSNESRecomp PRIVATE
    ${SNESRECOMP_ROOT}/host/snesrecomp_codegen_host.c)
target_include_directories(SecretOfEvermoreSNESRecomp PRIVATE
    ${SNESRECOMP_ROOT}/host
    ${RECOMP_UI_ROOT}/src)

# Netplay is not built into this target.
# No multitap: two seats, one controller per port.
