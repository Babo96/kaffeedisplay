idf_component_register(
    SRCS "main.cpp" "platform_adapter.cpp" "api_controller.cpp" "serial_console.cpp"
    INCLUDE_DIRS "."
    REQUIRES board domain storage service ui net api reports webassets nvs_flash littlefs
             esp_app_format esp_event lvgl driver sdbackup
)

# The serial console's test-only write commands (book, void) stay locked by
# default. A build with HIL_ALLOW_WRITES=1 (or this CMake option) unlocks them;
# otherwise the runtime `unlock-writes <admin_password>` command is required.
option(KAFFEE_SERIAL_ALLOW_WRITES "Unlock book/void on the serial console" OFF)
if(KAFFEE_SERIAL_ALLOW_WRITES OR "$ENV{HIL_ALLOW_WRITES}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KAFFEE_SERIAL_ALLOW_WRITES=1)
endif()
