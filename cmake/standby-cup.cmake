function(kaffee_add_standby_cup_codegen target repository_root)
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(generated_header "${generated_dir}/standby_cup_generated.hpp")

    if(DEFINED ENV{STANDBY_PYTHON} AND NOT "$ENV{STANDBY_PYTHON}" STREQUAL "")
        set(standby_python "$ENV{STANDBY_PYTHON}")
    else()
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(standby_python "${Python3_EXECUTABLE}")
    endif()

    add_custom_command(
        OUTPUT "${generated_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}"
        COMMAND "${standby_python}"
                "${repository_root}/tools/generate_standby_cup.py"
                --output "${generated_header}"
        DEPENDS
            "${repository_root}/assets/standby/espresso.svg"
            "${repository_root}/tools/generate_standby_cup.py"
            "${repository_root}/tools/requirements-standby.txt"
        COMMENT "Generating the native LVGL standby cup image"
        VERBATIM
    )

    set(codegen_target "${target}_standby_cup_codegen")
    add_custom_target(${codegen_target} DEPENDS "${generated_header}")
    target_include_directories(${target} PRIVATE "${generated_dir}")
    add_dependencies(${target} ${codegen_target})
endfunction()
