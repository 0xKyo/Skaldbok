cmake_minimum_required(VERSION 3.24)
project(Skaldbok VERSION 0.2.0 LANGUAGES C CXX)

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

# ---- everything that is not drawing: books, content packs, characters, rules, messages ----------------
add_library(gm_core STATIC
  src/model.cpp
  src/db.cpp
  src/content.cpp
  src/packs.cpp
  src/settings.cpp
  src/dice.cpp
  src/encounter.cpp
  src/character.cpp
  src/party.cpp
  src/changelog.cpp
  src/sheet_edit.cpp
  src/master_screen.cpp
  src/web_link.cpp
  src/creation.cpp
  src/messages.cpp
  src/fsutil.cpp
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

# ---- the app ----------------------------------------------------------------------------------------------
option(GM_BUILD_APP "Build the window (turn off for a headless build that only runs the tests)" ON)
if(GM_BUILD_APP)
add_executable(skaldbok
  src/main.cpp
  src/app.cpp
  src/fonts.cpp
  src/textures.cpp
  src/filedialog.cpp
  src/ui_common.cpp
  src/modules/registry.cpp
  src/modules/search_module.cpp
  src/modules/master_screen_module.cpp
  src/modules/creatures_module.cpp
  src/modules/compendium_module.cpp
  src/modules/tables_module.cpp
  src/modules/rules_module.cpp
  src/modules/encounter_module.cpp
  src/modules/characters_module.cpp
  src/modules/character_sheet.cpp
  src/modules/party_module.cpp
  src/modules/messages_module.cpp
  src/modules/web_module.cpp
  src/modules/settings_module.cpp
)
target_include_directories(skaldbok PRIVATE src ${STB_INCLUDE_DIR})
target_link_libraries(skaldbok PRIVATE gm_core imgui_sdl3)
if(MSVC)
  target_compile_options(skaldbok PRIVATE /W4 /utf-8 /permissive-)
  # GUI subsystem in release so no console window pops up; keep the console for debug builds.
  set_target_properties(skaldbok PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
else()
  target_compile_options(skaldbok PRIVATE -Wall -Wextra)
endif()
endif()

# ---- the players' web server: reads the GM app's files, serves the Vue client and a small read-only JSON API --------
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

