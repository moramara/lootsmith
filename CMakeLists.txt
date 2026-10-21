# LOOT SMITH - CMake build (the Makefile keeps working as before).
#
#   cmake -S . -B build                      configure (game + tests)
#   cmake --build build                      build ./build/lootsmith and ./build/test_sim
#   ctest --test-dir build --output-on-failure   run the logic tests
#
# Audio: BGM + SFX live in assets/audio (original, see assets/audio/README.md); they are copied next to the executable
# after the build and installed to share/lootsmith/assets.  Volumes are stored in lootsmith.cfg (working directory).
#
# Options (-DNAME=VALUE):
#   LOOTSMITH_BUILD_GAME   ON   build the raylib game (needs raylib, see below)
#   LOOTSMITH_BUILD_TESTS  ON   build + register the logic tests (no raylib needed)
#   LOOTSMITH_SANITIZE     ON   build the tests with AddressSanitizer + UBSan (GCC / Clang), like `make test`
#   LOOTSMITH_FETCH_RAYLIB OFF  download and build raylib 5.5 when it is not found (needs network + git)
#   RAYLIB_PATH            ""   a raylib checkout with a built src/libraylib.a (same meaning as in the Makefile)
#
# raylib lookup order: RAYLIB_PATH > third_party/raylib (`make deps`) > find_package(raylib) > pkg-config >
# FetchContent (only with LOOTSMITH_FETCH_RAYLIB=ON).  Without raylib only the core library and the tests are built.
cmake_minimum_required(VERSION 3.16)
project(LootSmith VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)                       # plain -std=c11, as in the Makefile
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LOOTSMITH_BUILD_GAME   "Build the raylib game"              ON)
option(LOOTSMITH_BUILD_TESTS  "Build and register the logic tests" ON)
option(LOOTSMITH_SANITIZE     "Build the tests with ASan + UBSan"  ON)
option(LOOTSMITH_FETCH_RAYLIB "Download raylib if it is not found" OFF)
set(RAYLIB_PATH "" CACHE PATH "raylib checkout with a built src/libraylib.a")

set(LOOTSMITH_WARNINGS $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra> $<$<C_COMPILER_ID:MSVC>:/W3>)

# ---- game logic: no raylib dependency -------------------------------------------------------------
set(LOOTSMITH_CORE_SOURCES
    src/core/content_tables.c src/core/content_validate.c src/core/rng.c src/core/game_state.c
    src/core/upgrades.c src/core/market.c src/core/crafting.c src/core/inventory.c src/core/customers.c
    src/core/day_flow.c src/core/events.c src/core/guild_rank.c src/core/guild.c src/core/save.c src/core/icons.c)
set(LOOTSMITH_BOT_SOURCES src/bot/bot.c)
set(LOOTSMITH_AUDIO_LOGIC_SOURCES src/audio/settings.c src/audio/sfx.c src/audio/asset_path.c)   # volume settings + sound ids: pure C (no raylib), covered by tests
add_library(lootsmith_core STATIC ${LOOTSMITH_CORE_SOURCES})
target_include_directories(lootsmith_core PUBLIC src)
target_compile_options(lootsmith_core PRIVATE ${LOOTSMITH_WARNINGS})
if(UNIX)
  target_link_libraries(lootsmith_core PUBLIC m)
endif()

add_library(lootsmith_bot STATIC ${LOOTSMITH_BOT_SOURCES})
target_include_directories(lootsmith_bot PUBLIC src)
target_compile_options(lootsmith_bot PRIVATE ${LOOTSMITH_WARNINGS})
target_link_libraries(lootsmith_bot PUBLIC lootsmith_core)

add_library(lootsmith_audio_logic STATIC ${LOOTSMITH_AUDIO_LOGIC_SOURCES})
target_include_directories(lootsmith_audio_logic PUBLIC src)
target_compile_options(lootsmith_audio_logic PRIVATE ${LOOTSMITH_WARNINGS})
if(UNIX)
  target_link_libraries(lootsmith_audio_logic PUBLIC m)
endif()

# ---- tests ---------------------------------------------------------------------------------------
if(LOOTSMITH_BUILD_TESTS)
  enable_testing()
  # the sources are compiled into the test itself so that the sanitizers cover the game logic too
  add_executable(test_sim tests/test_sim.c ${LOOTSMITH_CORE_SOURCES})
  target_include_directories(test_sim PRIVATE src)
  target_compile_options(test_sim PRIVATE ${LOOTSMITH_WARNINGS})
  if(UNIX)
    target_link_libraries(test_sim PRIVATE m)
  endif()
  if(LOOTSMITH_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_sim PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_sim PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME logic COMMAND test_sim WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(logic PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL ")

  # The Ascendant Era: new rank / blueprints / materials / Relic Expedition / daily events
  add_executable(test_ascendant tests/test_ascendant.c ${LOOTSMITH_CORE_SOURCES})
  target_include_directories(test_ascendant PRIVATE src)
  target_compile_options(test_ascendant PRIVATE ${LOOTSMITH_WARNINGS})
  if(UNIX)
    target_link_libraries(test_ascendant PRIVATE m)
  endif()
  if(LOOTSMITH_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_ascendant PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_ascendant PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME ascendant COMMAND test_ascendant WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(ascendant PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL ")

  # settings file, sound ids / one-sound-per-event logic, and the shipped audio files (assets/audio)
  add_executable(test_audio tests/test_audio.c ${LOOTSMITH_AUDIO_LOGIC_SOURCES})
  target_include_directories(test_audio PRIVATE src)
  target_compile_options(test_audio PRIVATE ${LOOTSMITH_WARNINGS})
  target_compile_definitions(test_audio PRIVATE LOOTSMITH_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  if(UNIX)
    target_link_libraries(test_audio PRIVATE m)
  endif()
  if(LOOTSMITH_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_audio PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_audio PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME audio_settings COMMAND test_audio WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(audio_settings PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL ")

  # The watch-mode policy is compiled into its test so sanitizer checks cover the BOT implementation.
  add_executable(test_bot tests/test_bot.c ${LOOTSMITH_BOT_SOURCES} ${LOOTSMITH_CORE_SOURCES})
  target_include_directories(test_bot PRIVATE src)
  target_compile_options(test_bot PRIVATE ${LOOTSMITH_WARNINGS})
  if(UNIX)
    target_link_libraries(test_bot PRIVATE m)
  endif()
  if(LOOTSMITH_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_bot PRIVATE -g -fsanitize=address,undefined)
    target_link_options(test_bot PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME bot_watch COMMAND test_bot WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(bot_watch PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL ")
endif()

# ---- the game ------------------------------------------------------------------------------------
if(LOOTSMITH_BUILD_GAME)
  add_library(lootsmith_raylib INTERFACE)
  set(_rl_found FALSE)

  if(NOT RAYLIB_PATH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/raylib/src/libraylib.a")
    set(RAYLIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/raylib")
  endif()
  if(RAYLIB_PATH AND EXISTS "${RAYLIB_PATH}/src/libraylib.a")
    target_include_directories(lootsmith_raylib INTERFACE "${RAYLIB_PATH}/src")
    target_link_libraries(lootsmith_raylib INTERFACE "${RAYLIB_PATH}/src/libraylib.a")
    if(APPLE)
      target_link_libraries(lootsmith_raylib INTERFACE "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
    elseif(WIN32)
      target_link_libraries(lootsmith_raylib INTERFACE opengl32 gdi32 winmm)
    else()
      target_link_libraries(lootsmith_raylib INTERFACE GL pthread dl rt X11)
    endif()
    set(_rl_found TRUE)
    message(STATUS "raylib: ${RAYLIB_PATH}/src/libraylib.a")
  endif()

  if(NOT _rl_found)
    find_package(raylib 5 QUIET CONFIG)
    if(TARGET raylib)
      target_link_libraries(lootsmith_raylib INTERFACE raylib)
      set(_rl_found TRUE)
      message(STATUS "raylib: found by find_package")
    endif()
  endif()

  if(NOT _rl_found)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(LS_RAYLIB QUIET IMPORTED_TARGET raylib)
      if(TARGET PkgConfig::LS_RAYLIB)
        target_link_libraries(lootsmith_raylib INTERFACE PkgConfig::LS_RAYLIB)
        set(_rl_found TRUE)
        message(STATUS "raylib: found by pkg-config")
      endif()
    endif()
  endif()

  if(NOT _rl_found AND LOOTSMITH_FETCH_RAYLIB)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib GIT_REPOSITORY https://github.com/raysan5/raylib.git GIT_TAG 5.5 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(raylib)
    target_link_libraries(lootsmith_raylib INTERFACE raylib)
    set(_rl_found TRUE)
    message(STATUS "raylib: fetched 5.5")
  endif()

  if(_rl_found)
    add_executable(lootsmith
      src/main.c src/audio/audio.c src/ui/screen_bot.c
      src/ui/ui_draw.c src/ui/ui_widgets.c src/ui/ui_chrome.c src/ui/ui_app.c
      src/ui/screen_home.c src/ui/screen_shop.c src/ui/screen_craft.c src/ui/screen_inventory.c
      src/ui/screen_customer.c src/ui/screen_report.c src/ui/screen_guild.c src/ui/screen_options.c
      src/ui/screen_help.c)
    target_include_directories(lootsmith PRIVATE src)
    target_compile_options(lootsmith PRIVATE ${LOOTSMITH_WARNINGS})
    target_link_libraries(lootsmith PRIVATE lootsmith_core lootsmith_bot lootsmith_audio_logic lootsmith_raylib)
    # Where the game looks for assets/ (src/audio/audio.c): $LOOTSMITH_ASSETS, <exe>/assets, <exe>/../assets,
    # <exe>/../share/lootsmith/assets, ./assets, then this compile-time default (the source tree).
    target_compile_definitions(lootsmith PRIVATE LOOTSMITH_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    # build tree: put a copy of assets/ next to the executable, so build/lootsmith runs from any working directory
    add_custom_command(TARGET lootsmith POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:lootsmith>/assets"
      COMMENT "Copying assets next to lootsmith")
    install(TARGETS lootsmith RUNTIME DESTINATION bin)
    install(DIRECTORY assets DESTINATION share/lootsmith)               # <prefix>/bin/lootsmith finds <prefix>/share/lootsmith/assets
  else()
    message(WARNING "raylib not found: building the core library and tests only. "
                    "Set -DRAYLIB_PATH=<checkout>, install raylib, or use -DLOOTSMITH_FETCH_RAYLIB=ON.")
  endif()
endif()
