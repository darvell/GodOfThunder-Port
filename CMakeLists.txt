cmake_minimum_required(VERSION 3.20)

project(got LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GOT_BUILD_RAYLIB "Build the native raylib targets" ON)

#
# Raylib build (native, not DOS)
#
if(GOT_BUILD_RAYLIB)
  include(FetchContent)

  # raylib uses this variable name to select its backend when building for web.
  if(EMSCRIPTEN OR (DEFINED PLATFORM AND PLATFORM STREQUAL "Web"))
    set(PLATFORM "Web" CACHE STRING "" FORCE)
    # For Emscripten we want an .html launcher alongside the .js/.wasm/.data.
    # Force it so the single-page web build emits got_raylib.html.
    set(CMAKE_EXECUTABLE_SUFFIX ".html" CACHE STRING "" FORCE)
  endif()

  option(GOT_FETCH_RAYLIB "Fetch raylib via FetchContent when not found locally" ON)
  set(GOT_RAYLIB_SOURCE_DIR "" CACHE PATH "Optional local raylib source dir (contains raylib CMakeLists.txt)")

  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

  set(GOT_RAYLIB_TARGET "")

  if(GOT_RAYLIB_SOURCE_DIR AND EXISTS "${GOT_RAYLIB_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${GOT_RAYLIB_SOURCE_DIR}" raylib-build)
    set(GOT_RAYLIB_TARGET raylib)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/raylib/CMakeLists.txt")
    add_subdirectory(third_party/raylib)
    set(GOT_RAYLIB_TARGET raylib)
  else()
    find_package(raylib QUIET)
    if(TARGET raylib::raylib)
      set(GOT_RAYLIB_TARGET raylib::raylib)
    elseif(TARGET raylib)
      set(GOT_RAYLIB_TARGET raylib)
    elseif(GOT_FETCH_RAYLIB)
      FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
      )
      FetchContent_MakeAvailable(raylib)
      set(GOT_RAYLIB_TARGET raylib)
    else()
      message(FATAL_ERROR "raylib not found. Provide -DGOT_RAYLIB_SOURCE_DIR=/path/to/raylib or add third_party/raylib, or set GOT_FETCH_RAYLIB=ON.")
    endif()
  endif()

  # ── shared compile options (GCC/Clang vs MSVC) ──
  set(GOT_COMPILE_OPTS
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
      -funsigned-char
      -Wno-implicit-int
      -Wno-implicit-function-declaration
      -Wno-return-type
      -Wno-incompatible-library-redeclaration
      -Wno-deprecated-non-prototype
      -Wno-int-conversion
      -Wno-pointer-sign
      -Wno-format
    >
    $<$<C_COMPILER_ID:MSVC>:
      /J
      /wd4013 /wd4033 /wd4047 /wd4024 /wd4133 /wd4267 /wd4244 /wd4996
    >
  )

  if(NOT EMSCRIPTEN)
    add_executable(got_raylib_g1
      src/native/main_raylib.c
      src/native/emscripten_fs.c
      src/native/platform_raylib.c
      src/native/audio_raylib.c
      src/native/web_gamepad.c
      src/native/dos_compat.c
      src/native/far_compat.c
      src/native/digisnd_native.c
      src/native/voc_decode.c
      src/native/mixer.c
      src/native/adlib_native.c
      src/native/opl2_emu.cpp
      third_party/ymfm/src/ymfm_opl.cpp
      third_party/ymfm/src/ymfm_misc.cpp
      third_party/ymfm/src/ymfm_adpcm.cpp
      third_party/ymfm/src/ymfm_pcm.cpp
      third_party/ymfm/src/ymfm_ssg.cpp
      src/native/joy_stub.c
      src/native/gui.c
      src/native/gui_settings.c

      reference/src/_g1/1_back.c
      reference/src/_g1/1_boss1.c
      reference/src/_g1/1_boss21.c
      reference/src/_g1/1_boss22.c
      reference/src/_g1/1_dialog.c
      reference/src/_g1/1_file.c
      reference/src/_g1/1_grp.c
      reference/src/_g1/1_image.c
      reference/src/_g1/1_init.c
      reference/src/_g1/1_main.c
      reference/src/_g1/1_move.c
      reference/src/_g1/1_movpat.c
      reference/src/_g1/1_music.c
      reference/src/_g1/1_object.c
      reference/src/_g1/1_panel.c
      reference/src/_g1/1_script.c
      reference/src/_g1/1_shtmov.c
      reference/src/_g1/1_shtpat.c
      reference/src/_g1/1_sound.c
      reference/src/_g1/1_sptile.c

      src/utility/modern.c
      src/utility/lzss.c
      src/utility/lzss.h
      src/utility/res_abrt.c
      src/utility/res_add.c
      src/utility/res_crea.c
      src/utility/res_del.c
      src/utility/res_enco.c
      src/utility/res_err.c
      src/utility/res_extr.c
      src/utility/res_fall.c
      src/utility/res_find.c
      src/utility/res_init.c
      src/utility/res_int.c
      src/utility/res_pack.c
      src/utility/res_read.c
      src/utility/res_renm.c
      src/utility/res_repl.c
      src/utility/res_writ.c
      src/utility/mu_man.c
    )

    target_compile_definitions(got_raylib_g1 PRIVATE
      __llvm__=1
      GOT_EPISODE=1
    )

    target_include_directories(got_raylib_g1 PRIVATE
      third_party/ymfm/src
      src/native/include
      src/native
      reference/src/_g1
      src/digisnd
      src/utility
      src
    )

    target_compile_options(got_raylib_g1 PRIVATE ${GOT_COMPILE_OPTS})

    target_link_libraries(got_raylib_g1 PRIVATE
      ${GOT_RAYLIB_TARGET}
    )
  endif()

  # ── shared native + utility sources for all episodes ──
  set(GOT_NATIVE_SOURCES
    src/native/emscripten_fs.c
    src/native/platform_raylib.c
    src/native/audio_raylib.c
    src/native/web_gamepad.c
    src/native/dos_compat.c
    src/native/far_compat.c
    src/native/digisnd_native.c
    src/native/voc_decode.c
    src/native/mixer.c
    src/native/adlib_native.c
    src/native/opl2_emu.cpp
    third_party/ymfm/src/ymfm_opl.cpp
    third_party/ymfm/src/ymfm_misc.cpp
    third_party/ymfm/src/ymfm_adpcm.cpp
    third_party/ymfm/src/ymfm_pcm.cpp
    third_party/ymfm/src/ymfm_ssg.cpp
    src/native/joy_stub.c
    src/native/gui.c
    src/native/gui_settings.c
  )
  set(GOT_UTILITY_SOURCES
    src/utility/modern.c
    src/utility/lzss.c
    src/utility/lzss.h
    src/utility/res_abrt.c
    src/utility/res_add.c
    src/utility/res_crea.c
    src/utility/res_del.c
    src/utility/res_enco.c
    src/utility/res_err.c
    src/utility/res_extr.c
    src/utility/res_fall.c
    src/utility/res_find.c
    src/utility/res_init.c
    src/utility/res_int.c
    src/utility/res_pack.c
    src/utility/res_read.c
    src/utility/res_renm.c
    src/utility/res_repl.c
    src/utility/res_writ.c
    src/utility/mu_man.c
  )
  # ── Unified single-binary (all episodes) ──
  set(GOT_GAME_SOURCES
    src/game/back.c
    src/game/boss_ep1.c
    src/game/boss_ep2.c
    src/game/boss_ep3.c
    src/game/dialog.c
    src/game/episode.c
    src/game/file.c
    src/game/grp.c
    src/game/image.c
    src/game/init.c
    src/game/main.c
    src/game/move.c
    src/game/movpat.c
    src/game/music.c
    src/game/object.c
    src/game/panel.c
    src/game/script.c
    src/game/shtmov.c
    src/game/shtpat.c
    src/game/sound.c
    src/game/sptile.c
  )

  add_executable(got_raylib
    src/native/main_raylib_unified.c
    src/native/launcher.c
    src/native/launcher_extras.c
    src/native/graphics_got.c
    ${GOT_NATIVE_SOURCES}
    ${GOT_GAME_SOURCES}
    ${GOT_UTILITY_SOURCES}
  )
  target_compile_definitions(got_raylib PRIVATE __llvm__=1)
  target_include_directories(got_raylib PRIVATE
    third_party/ymfm/src
    src/native/include src/native src/game src/digisnd src/utility src
  )
  target_compile_options(got_raylib PRIVATE ${GOT_COMPILE_OPTS})
  target_link_libraries(got_raylib PRIVATE ${GOT_RAYLIB_TARGET})

  if(NOT EMSCRIPTEN)
    # ── Episode 2 ──
    add_executable(got_raylib_g2
      src/native/main_raylib_g2.c
      ${GOT_NATIVE_SOURCES}

      reference/src/_g2/2_back.c
      reference/src/_g2/2_boss.c
      reference/src/_g2/2_dialog.c
      reference/src/_g2/2_file.c
      reference/src/_g2/2_grp.c
      reference/src/_g2/2_image.c
      reference/src/_g2/2_init.c
      reference/src/_g2/2_main.c
      reference/src/_g2/2_move.c
      reference/src/_g2/2_movpat.c
      reference/src/_g2/2_music.c
      reference/src/_g2/2_object.c
      reference/src/_g2/2_panel.c
      reference/src/_g2/2_script.c
      reference/src/_g2/2_shtmov.c
      reference/src/_g2/2_shtpat.c
      reference/src/_g2/2_sound.c
      reference/src/_g2/2_sptile.c

      ${GOT_UTILITY_SOURCES}
    )
    target_compile_definitions(got_raylib_g2 PRIVATE __llvm__=1 GOT_EPISODE=2)
    target_include_directories(got_raylib_g2 PRIVATE
      third_party/ymfm/src
      src/native/include src/native reference/src/_g2 src/digisnd src/utility src
    )
    target_compile_options(got_raylib_g2 PRIVATE ${GOT_COMPILE_OPTS})
    target_link_libraries(got_raylib_g2 PRIVATE ${GOT_RAYLIB_TARGET})

    # ── Episode 3 ──
    add_executable(got_raylib_g3
      src/native/main_raylib_g3.c
      ${GOT_NATIVE_SOURCES}

      reference/src/_g3/3_back.c
      reference/src/_g3/3_boss.c
      reference/src/_g3/3_dialog.c
      reference/src/_g3/3_file.c
      reference/src/_g3/3_grp.c
      reference/src/_g3/3_image.c
      reference/src/_g3/3_init.c
      reference/src/_g3/3_main.c
      reference/src/_g3/3_move.c
      reference/src/_g3/3_movpat.c
      reference/src/_g3/3_music.c
      reference/src/_g3/3_object.c
      reference/src/_g3/3_panel.c
      reference/src/_g3/3_script.c
      reference/src/_g3/3_shtmov.c
      reference/src/_g3/3_shtpat.c
      reference/src/_g3/3_sound.c
      reference/src/_g3/3_sptile.c

      ${GOT_UTILITY_SOURCES}
    )
    target_compile_definitions(got_raylib_g3 PRIVATE __llvm__=1 GOT_EPISODE=3)
    target_include_directories(got_raylib_g3 PRIVATE
      third_party/ymfm/src
      src/native/include src/native reference/src/_g3 src/digisnd src/utility src
    )
    target_compile_options(got_raylib_g3 PRIVATE ${GOT_COMPILE_OPTS})
    target_link_libraries(got_raylib_g3 PRIVATE ${GOT_RAYLIB_TARGET})
  endif()

  # ── Emscripten / WASM specifics ──
  if(EMSCRIPTEN)
    # raylib's default frame limiter uses nanosleep() which can be very coarse
    # under Emscripten, leading to inconsistent frame pacing. Force the busy
    # wait loop for more stable timing.
    if(TARGET raylib)
      target_compile_definitions(raylib PRIVATE SUPPORT_BUSY_WAIT_LOOP)
    endif()

    set(GOT_EM_LINK_FLAGS
      "-sFORCE_FILESYSTEM=1"
      "-sALLOW_MEMORY_GROWTH=1"
      "-sINITIAL_MEMORY=33554432"
      "-sTOTAL_STACK=4194304"
      "-sUSE_GLFW=3"
      # We run the original synchronous DOS-style main loop. On the web we
      # must yield back to the browser event loop in wait/delay paths.
      "-sASYNCIFY=1"
      # We start the game via Module.callMain() from the HTML shell.
      "-sEXPORTED_RUNTIME_METHODS=['callMain']"
      # Use a custom HTML shell so the page is full-bleed (no scrollbars,
      # no debug console), with pixelated canvas scaling.
      "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/emscripten_shell.html"
      # Patch runtime heap view mirrors (Module.HEAPF32 etc) for raylib/miniaudio.
      "SHELL:--post-js ${CMAKE_SOURCE_DIR}/web/emscripten_post.js"
      "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/GOTRES.DAT@/GOTRES.DAT"
    )
    # Optionally preload VERSION.GOT if present.
    if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION.GOT")
      list(APPEND GOT_EM_LINK_FLAGS
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/VERSION.GOT@/VERSION.GOT"
      )
    endif()
    # Unified launcher/menu assets (opening sequence + episode menu).
    if(EXISTS "${CMAKE_SOURCE_DIR}/GRAPHICS.GOT")
      list(APPEND GOT_EM_LINK_FLAGS
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/GRAPHICS.GOT@/GRAPHICS.GOT"
      )
    endif()

    target_link_options(got_raylib PRIVATE ${GOT_EM_LINK_FLAGS})
  endif()
endif()
