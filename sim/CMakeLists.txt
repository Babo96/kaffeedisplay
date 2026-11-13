cmake_minimum_required(VERSION 3.20)
project(kaffeekasse_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# LVGL is pinned to the same version the firmware uses (firmware/main/idf_component.yml).
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.6.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lvgl)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(ZLIB REQUIRED)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(UI_DIR "${REPO_ROOT}/firmware/components/ui")
set(DOMAIN_DIR "${REPO_ROOT}/firmware/components/domain")

add_executable(kaffeekasse-sim
  src/main.cpp
  src/fake_backend.cpp
  src/sdl_display.cpp
  src/png_writer.cpp
  src/click_test.cpp
  src/adapter_compile.cpp
  ${DOMAIN_DIR}/src/domain.cpp
  ${DOMAIN_DIR}/src/json.cpp
  ${DOMAIN_DIR}/src/sha256.cpp
  ${UI_DIR}/src/ui_app.cpp
  ${UI_DIR}/src/page_book.cpp
  ${UI_DIR}/src/page_stats.cpp
  ${UI_DIR}/src/page_system.cpp
  ${UI_DIR}/src/theme.cpp
  ${UI_DIR}/src/format.cpp
  ${UI_DIR}/src/member_order.cpp
  ${UI_DIR}/src/fonts/kaffee_font_14.c
  ${UI_DIR}/src/fonts/kaffee_font_18.c
  ${UI_DIR}/src/fonts/kaffee_font_22.c
  ${UI_DIR}/src/fonts/kaffee_font_18_bold.c
  ${UI_DIR}/src/fonts/kaffee_font_24_bold.c
  ${UI_DIR}/src/fonts/kaffee_font_34_bold.c
)

target_include_directories(kaffeekasse-sim PRIVATE
  ${UI_DIR}/include
  ${UI_DIR}/src
  ${REPO_ROOT}/firmware/components/board/include
  ${DOMAIN_DIR}/include
  ${DOMAIN_DIR}/third_party
  ${REPO_ROOT}/firmware/components/service/include
  ${REPO_ROOT}/firmware/components/storage/include
)

target_link_libraries(kaffeekasse-sim PRIVATE
  lvgl
  PkgConfig::SDL2
  ZLIB::ZLIB
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(kaffeekasse-sim PRIVATE -Wall -Wextra)
endif()
