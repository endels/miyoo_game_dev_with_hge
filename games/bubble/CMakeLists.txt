# ===========================================================================
# bubble — the bubble (duel) game. Consumes the engine only through the hge
# target, exactly like a future standalone game repo will.
# ===========================================================================

set(BUBBLE_COMMON_SRC
    src/app/bubble/battle.cpp
    src/app/bubble/duel.cpp
    src/app/bubble/mode_select.cpp
    src/app/bubble/bubble_app.cpp
    src/app/bubble/game.cpp
    src/app/bubble/render.cpp)
set(BUBBLE_MAIN_SRC
    src/app/bubble/main.cpp)

if(HGE_HOST STREQUAL "standalone")
    if(WIN32)
        add_executable(bubble_level1 WIN32 ${BUBBLE_COMMON_SRC} ${BUBBLE_MAIN_SRC})
    else()
        add_executable(bubble_level1 ${BUBBLE_COMMON_SRC} ${BUBBLE_MAIN_SRC})
    endif()
    target_include_directories(bubble_level1 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(bubble_level1 PRIVATE hge)
    hge_set_warnings(bubble_level1)

    if(MSVC)
        # WIN32 subsystem + our own main(): use the CRT entry instead of WinMain
        target_link_options(bubble_level1 PRIVATE "/ENTRY:mainCRTStartup")
    elseif(MINGW)
        target_link_options(bubble_level1 PRIVATE -Wl,-e,mainCRTStartup)
        target_link_options(bubble_level1 PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    # Embed the asset packs into the desktop exe so it ships as a single file.
    # The packs are link dependencies: touching audio.pak must relink, otherwise
    # the exe keeps shipping the copies baked into it at the previous link.
    if(HGE_PLATFORM STREQUAL "desktop")
        set(BUBBLE_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        set_property(TARGET bubble_level1 APPEND PROPERTY LINK_DEPENDS
            "${BUBBLE_ASSET_DIR}/game.pak"
            "${BUBBLE_ASSET_DIR}/audio.pak")
        find_program(HGE_PYTHON NAMES python python3)
        if(HGE_PYTHON)
            add_custom_command(TARGET bubble_level1 POST_BUILD
                COMMAND "${HGE_PYTHON}" "${HGE_ENGINE_DIR}/tools/pack_exe.py"
                        "$<TARGET_FILE:bubble_level1>"
                        "${BUBBLE_ASSET_DIR}/game.pak"
                        "${BUBBLE_ASSET_DIR}/audio.pak"
                COMMENT "Embedding game.pak + audio.pak into bubble_level1")
        else()
            message(WARNING "python not found - bubble_level1 will need game.pak/audio.pak beside it")
        endif()
    endif()

    if(HGE_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING AND UNIX)
        add_test(NAME bubble_sim_golden
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_host_tests.sh"
                    "$<TARGET_FILE:bubble_level1>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/sim_5.txt")
        # Same idea for --duel-sim: freezes default AI play across a seed range.
        add_test(NAME bubble_duel_golden
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_duel_tests.sh"
                    "$<TARGET_FILE:bubble_level1>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/duel_default.txt")
    endif()
else()
    add_library(bubble_libretro MODULE
        ${BUBBLE_COMMON_SRC}
        src/app/bubble/bubble_libretro.cpp
        "${HGE_ENGINE_DIR}/src/host/libretro_core.cpp")
    target_include_directories(bubble_libretro PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${HGE_ENGINE_DIR}/src"
        "${HGE_LIBRETRO_INCLUDE_DIR}")
    target_link_libraries(bubble_libretro PRIVATE hge)
    set_target_properties(bubble_libretro PROPERTIES PREFIX "")
    hge_set_warnings(bubble_libretro)
endif()
