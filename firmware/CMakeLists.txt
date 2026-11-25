cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The release workflow stamps the tag into the image so /health and the admin UI
# report the release instead of the ESP-IDF default "1". Local builds keep the
# default: without the variable, esp-idf derives the version itself.
if(DEFINED ENV{PROJECT_VER} AND NOT "$ENV{PROJECT_VER}" STREQUAL "")
    set(PROJECT_VER "$ENV{PROJECT_VER}")
endif()

project(kaffeekasse)
