cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components" "Middlewares")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(01_led)

target_compile_options(${CMAKE_PROJECT_NAME}.elf PRIVATE
    -fdiagnostics-color=always
)
