cmake_minimum_required(VERSION 3.20)
project(kaffee_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

set(DOMAIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/domain)
set(STORAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/storage)
set(SERVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/service)
set(REPORTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/reports)
set(NET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/net)
set(API_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/api)
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/ui)
set(BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/board)

add_executable(kaffee_tests
  doctest_main.cpp
  domain_test.cpp
  storage_test.cpp
  service_test.cpp
  auth_test.cpp
  reports_test.cpp
  net_pure_test.cpp
  api_util_test.cpp
  api_contract_test.cpp
  ui_format_test.cpp
  member_order_test.cpp
  touch_transform_test.cpp
  storage_boot_test.cpp
  ${DOMAIN_DIR}/src/domain.cpp
  ${DOMAIN_DIR}/src/json.cpp
  ${DOMAIN_DIR}/src/sha256.cpp
  ${STORAGE_DIR}/src/journal_store.cpp
  ${STORAGE_DIR}/src/file_store.cpp
  ${SERVICE_DIR}/src/auth.cpp
  ${SERVICE_DIR}/src/service.cpp
  ${REPORTS_DIR}/src/reports.cpp
  ${NET_DIR}/src/net_pure.cpp
  ${API_DIR}/src/api_util.cpp
  ${API_DIR}/src/api_routes.cpp
  ${API_DIR}/src/api_multipart.cpp
  ${UI_DIR}/src/format.cpp
  ${UI_DIR}/src/member_order.cpp
)

target_include_directories(kaffee_tests PRIVATE
  ${DOMAIN_DIR}/include
  ${DOMAIN_DIR}/third_party
  ${STORAGE_DIR}/include
  ${SERVICE_DIR}/include
  ${REPORTS_DIR}/include
  ${NET_DIR}/include
  ${API_DIR}/include
  ${API_DIR}/src
  ${UI_DIR}/src
  ${BOARD_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/main
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)

target_compile_definitions(kaffee_tests PRIVATE
  GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

find_package(Threads REQUIRED)
target_link_libraries(kaffee_tests PRIVATE Threads::Threads)
