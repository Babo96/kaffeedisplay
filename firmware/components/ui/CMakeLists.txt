set(UI_SOURCES
    "src/ui_app.cpp"
    "src/standby.cpp"
    "src/page_book.cpp"
    "src/page_stats.cpp"
    "src/page_system.cpp"
    "src/theme.cpp"
    "src/format.cpp"
    "src/member_order.cpp"
    "src/fonts/kaffee_font_14.c"
    "src/fonts/kaffee_font_18.c"
    "src/fonts/kaffee_font_22.c"
    "src/fonts/kaffee_font_18_bold.c"
    "src/fonts/kaffee_font_24_bold.c"
    "src/fonts/kaffee_font_34_bold.c"
)

idf_component_register(
    SRCS ${UI_SOURCES}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES service lvgl board
)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/standby-cup.cmake")
kaffee_add_standby_cup_codegen(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../../..")
