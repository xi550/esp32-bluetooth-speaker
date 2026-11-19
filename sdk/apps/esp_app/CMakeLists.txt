cmake_minimum_required(VERSION 3.17)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "${REPOSITORY_ROOT}/sdk/esp-idf")
endif()

set(IDF_TARGET "esp32" CACHE STRING "Classic ESP32 only")
set(BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/board/ESP32/board_esp32_speaker")

# 直接读取产品头文件中的字符串宏；修改头文件后自动重新配置构建。
set(BOARD_CONFIG "${BOARD_DIR}/board_esp_proj_cfg.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BOARD_CONFIG}")
file(STRINGS "${BOARD_CONFIG}" VERSION_DEFINITION
    REGEX "^[ \t]*#[ \t]*define[ \t]+BOARD_FIRMWARE_VERSION[ \t]+\"[A-Za-z0-9.+_-]+\"[ \t]*$"
)
list(LENGTH VERSION_DEFINITION VERSION_DEFINITION_COUNT)
if(NOT VERSION_DEFINITION_COUNT EQUAL 1)
    message(FATAL_ERROR "Define BOARD_FIRMWARE_VERSION once as a quoted ASCII version in cfg.h")
endif()
string(REGEX REPLACE "^[^\"]*\"([^\"]+)\"[ \t]*$" "\\1" PROJECT_VER "${VERSION_DEFINITION}")
string(LENGTH "${PROJECT_VER}" VERSION_LENGTH)
if(VERSION_LENGTH GREATER 31)
    message(FATAL_ERROR "BOARD_FIRMWARE_VERSION must be at most 31 characters")
endif()

set(EXTRA_COMPONENT_DIRS "${BOARD_DIR}")
set(COMPONENTS board_esp32_speaker)
set(SDKCONFIG_DEFAULTS "${BOARD_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_speaker_foundation)

if(CONFIG_APP_PROJECT_VER_FROM_CONFIG OR CONFIG_APP_EXCLUDE_PROJECT_VER_VAR)
    message(FATAL_ERROR "Keep the app version enabled and sourced from BOARD_FIRMWARE_VERSION in cfg.h")
endif()

# 裁剪配置只传递给 LVGL 及其使用者，不改变其他 SDK 组件的编译参数。
idf_component_get_property(LVGL_TARGET lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${LVGL_TARGET} PUBLIC
    LV_KCONFIG_IGNORE
    LV_CONF_PATH="${CMAKE_CURRENT_LIST_DIR}/ui/lv_conf.h"
)

if(NOT IDF_TARGET STREQUAL "esp32")
    message(FATAL_ERROR "Requires classic ESP32-WROOM-32E/32D")
endif()

if(NOT "$ENV{IDF_VERSION}" STREQUAL "5.5.6")
    message(FATAL_ERROR "Use pinned ESP-IDF v5.5.6")
endif()
