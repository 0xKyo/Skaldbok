cmake_minimum_required(VERSION 3.24)
if(WIN32)
  project(Skaldbok VERSION 0.2.0 LANGUAGES C CXX RC)
else()
  project(Skaldbok VERSION 0.2.0 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Self-contained executable on Windows: no Visual C++ redistributable needed on the target machine.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(cmake/Dependencies.cmake)

# ---- the core: no UI, no server ------------------------------------------------------------------------------
# src/parsing reads the JSON (content packs, per-user files) into the internal model; src/game is the game logic on
# top of it. Every front-end (src/ui: the ImGui app, src/web: the players' server, a future API...) links this and
# only this; nothing in here may include from src/ui or src/web.
add_library(gm_core STATIC
  src/parsing/content.cpp
  src/parsing/packs.cpp
  src/parsing/fsutil.cpp
  src/game/model.cpp
  src/game/settings.cpp
  src/game/dice.cpp
  src/game/encounter.cpp
  src/game/character.cpp
  src/game/party.cpp
  src/game/changelog.cpp
  src/game/sheet_edit.cpp
  src/game/master_screen.cpp
  src/game/web_link.cpp
  src/game/creation.cpp
  src/game/messages.cpp
)
target_include_directories(gm_core PUBLIC src)
target_include_directories(gm_core SYSTEM PUBLIC ${JSON_INCLUDE_DIR})
target_link_libraries(gm_core PUBLIC SDL3::SDL3 sqlite3_static miniz_static)
target_compile_definitions(gm_core PUBLIC SKALDBOK_DEV_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
                                          SKALDBOK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(MSVC)
  target_compile_options(gm_core PRIVATE /W4 /utf-8 /permissive-)
  target_compile_definitions(gm_core PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(gm_core PRIVATE -Wall -Wextra)
endif()

# ---- front-end: the GM app (ImGui) ------------------------------------------------------------------------
option(GM_BUILD_APP "Build the window (turn off for a headless build that only runs the tests)" ON)
if(GM_BUILD_APP)
add_executable(skaldbok
  src/ui/main.cpp
  src/ui/app.cpp
  src/ui/fonts.cpp
  src/ui/textures.cpp
  src/ui/filedialog.cpp
  src/ui/ui_common.cpp
  src/ui/homebrew_forms.cpp
  src/ui/packs_panel.cpp
  src/ui/modules/registry.cpp
  src/ui/modules/search_module.cpp
  src/ui/modules/master_screen_module.cpp
  src/ui/modules/catalog_module.cpp
  src/ui/modules/gear_module.cpp
  src/ui/modules/rules_module.cpp
  src/ui/modules/encounter_module.cpp
  src/ui/modules/characters_module.cpp
  src/ui/modules/character_sheet.cpp
  src/ui/modules/party_module.cpp
  src/ui/modules/messages_module.cpp
  src/ui/modules/web_module.cpp
)
if(WIN32)
  target_sources(skaldbok PRIVATE assets/icons/skaldbok.rc)   # the exe's own icon: Explorer, the taskbar, alt-tab
endif()
target_include_directories(skaldbok PRIVATE src ${STB_INCLUDE_DIR})
target_link_libraries(skaldbok PRIVATE gm_core imgui_sdl3)
if(MSVC)
  target_compile_options(skaldbok PRIVATE /W4 /utf-8 /permissive-)
  # GUI subsystem in release so no console window pops up; keep the console for debug builds.
  set_target_properties(skaldbok PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
else()
  target_compile_options(skaldbok PRIVATE -Wall -Wextra)
endif()

# Linux desktop integration: `cmake --install` puts the binary on PATH and registers the app (icon, taskbar/alt-tab
# label, app menu entry) with the desktop environment. Windows gets its icon from the .rc resource above instead.
if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS skaldbok RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES assets/linux/skaldbok.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  foreach(size 16 32 48 64 128 256)
    install(FILES assets/icons/hicolor/${size}x${size}/apps/skaldbok.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
  endforeach()
endif()
endif()

# ---- front-end: the players' web server (reads the GM app's files, serves the Vue client and a small JSON API) ----
add_library(gm_web STATIC
  src/web/web_config.cpp
  src/web/web_access.cpp
  src/web/web_views.cpp
  src/web/web_app.cpp
  src/web/web_server.cpp
)
target_include_directories(gm_web PUBLIC src)
target_include_directories(gm_web SYSTEM PUBLIC ${HTTPLIB_INCLUDE_DIR})
target_link_libraries(gm_web PUBLIC gm_core)
if(WIN32)
  target_link_libraries(gm_web PUBLIC ws2_32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(gm_web PUBLIC Threads::Threads)
endif()
if(MSVC)
  target_compile_options(gm_web PRIVATE /W4 /utf-8 /permissive-)
else()
  target_compile_options(gm_web PRIVATE -Wall -Wextra)
endif()

add_executable(skaldbok_web tools/web_main.cpp)
target_link_libraries(skaldbok_web PRIVATE gm_web)
if(MSVC)
  target_compile_options(skaldbok_web PRIVATE /W4 /utf-8 /permissive-)
endif()

# ---- pack_check: validates a homebrew pack from a terminal, with the app's own loader -----------------------
add_executable(pack_check tools/pack_check.cpp)
target_link_libraries(pack_check PRIVATE gm_core)
if(MSVC)
  target_compile_options(pack_check PRIVATE /W4 /utf-8 /permissive-)
endif()

# ---- tests (no window) -------------------------------------------------------------------------------------
enable_testing()
foreach(t content_test character_test encounter_test screen_test messages_test sheet_test)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE gm_core)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W4 /utf-8 /permissive-)
  endif()
endforeach()
add_executable(web_test tests/web_test.cpp)
target_link_libraries(web_test PRIVATE gm_web)
if(MSVC)
  target_compile_options(web_test PRIVATE /W4 /utf-8 /permissive-)
endif()
add_test(NAME web COMMAND web_test)
add_test(NAME master_screen COMMAND screen_test)
add_test(NAME messages COMMAND messages_test)
add_test(NAME sheet COMMAND sheet_test)
add_test(NAME content COMMAND content_test)
add_test(NAME characters COMMAND character_test)
add_test(NAME encounter COMMAND encounter_test)

