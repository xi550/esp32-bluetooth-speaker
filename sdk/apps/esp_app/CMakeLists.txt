cmake_minimum_required(VERSION 3.17)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "${REPOSITORY_ROOT}/sdk/esp-idf")
endif()

set(IDF_TARGET "esp32" CACHE STRING "Classic ESP32 only")
set(PROJECT_VER "0.1.0")
set(BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/board/ESP32/board_esp32_speaker")
set(EXTRA_COMPONENT_DIRS "${BOARD_DIR}")
set(COMPONENTS board_esp32_speaker)
set(SDKCONFIG_DEFAULTS "${BOARD_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_speaker_foundation)

if(NOT IDF_TARGET STREQUAL "esp32")
    message(FATAL_ERROR "Requires classic ESP32-WROOM-32E/32D")
endif()

if(NOT "$ENV{IDF_VERSION}" STREQUAL "5.5.6")
    message(FATAL_ERROR "Use pinned ESP-IDF v5.5.6")
endif()
