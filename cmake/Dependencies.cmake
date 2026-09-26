# Third-party code is downloaded as plain source archives (no git needed) into .deps/ and built statically.
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "Where third-party sources are cached")
set(FETCHCONTENT_QUIET OFF)

# ---- SDL3 ---------------------------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)      # the app makes no sound
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---- Dear ImGui -----------------------------------------------------------------------------------
FetchContent_Declare(imgui_src
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---- SQLite amalgamation --------------------------------------------------------------------------
FetchContent_Declare(sqlite_src
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---- stb_image (single header) ----------------------------------------------------------------------
FetchContent_Declare(stb_image_h
  URL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
  DOWNLOAD_NO_EXTRACT TRUE)

# ---- nlohmann/json (single header): internal in-memory representation for parsed data -----------------
FetchContent_Declare(json_h
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
  DOWNLOAD_NO_EXTRACT TRUE)

# ---- yaml-cpp: pack, character and settings files are YAML -----------------------------------------------
# yaml-cpp 0.8.0 uses cmake_minimum_required(2.8.12); CMake 4.x rejects < 3.5. We populate the source
# ourselves, patch the cmake_minimum_required line to 3.5, then add_subdirectory normally.
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml_cpp_src
  URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---- miniz (amalgamated zip/deflate): importing homebrew packs from a .zip ----------------------------
FetchContent_Declare(miniz_src
  URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---- cpp-httplib (single header): the web server that shows players their characters -----------------------
FetchContent_Declare(httplib_h
  URL https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.26.0/httplib.h
  DOWNLOAD_NO_EXTRACT TRUE)

FetchContent_MakeAvailable(SDL3 imgui_src sqlite_src stb_image_h json_h yaml_cpp_src miniz_src httplib_h)

# Silence yaml-cpp warnings; add the namespaced alias if yaml-cpp didn't create it itself.
if(TARGET yaml-cpp)
  if(MSVC)
    target_compile_options(yaml-cpp PRIVATE /W0)
  else()
    target_compile_options(yaml-cpp PRIVATE -w)
  endif()
  if(NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
  endif()
endif()

# imgui + the SDL3 / SDL_Renderer backends
add_library(imgui_sdl3 STATIC
  ${imgui_src_SOURCE_DIR}/imgui.cpp
  ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_src_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_src_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui_sdl3 PUBLIC ${imgui_src_SOURCE_DIR} ${imgui_src_SOURCE_DIR}/backends)
target_link_libraries(imgui_sdl3 PUBLIC SDL3::SDL3)
target_compile_definitions(imgui_sdl3 PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# sqlite3 with full-text search (the database uses FTS5 with the porter tokenizer) and JSON
add_library(sqlite3_static STATIC ${sqlite_src_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_static PUBLIC ${sqlite_src_SOURCE_DIR})
target_compile_definitions(sqlite3_static PUBLIC
  SQLITE_ENABLE_FTS5 SQLITE_THREADSAFE=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0)
set_target_properties(sqlite3_static PROPERTIES C_STANDARD 11)

# stb_image lives next to the downloaded header
set(STB_INCLUDE_DIR "${stb_image_h_SOURCE_DIR}" CACHE INTERNAL "")

# nlohmann/json.hpp sits next to the downloaded header (included as "json.hpp" through src/parsing/jsonutil.h)
set(JSON_INCLUDE_DIR "${json_h_SOURCE_DIR}" CACHE INTERNAL "")

# cpp-httplib sits next to the downloaded header (included as <httplib.h>); no TLS: use a tunnel or a reverse proxy for HTTPS
set(HTTPLIB_INCLUDE_DIR "${httplib_h_SOURCE_DIR}" CACHE INTERNAL "")

add_library(miniz_static STATIC ${miniz_src_SOURCE_DIR}/miniz.c)
target_include_directories(miniz_static SYSTEM PUBLIC ${miniz_src_SOURCE_DIR})
target_compile_definitions(miniz_static PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_WRITING_APIS)
set_target_properties(miniz_static PROPERTIES C_STANDARD 11)
