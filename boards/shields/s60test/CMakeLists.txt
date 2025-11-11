# Copyright (c) 2025 The ZMK Contributors
# SPDX-License-Identifier: MIT

# Add LED widgets source files
if(CONFIG_ZMK_LED_WIDGETS)
    zephyr_library_sources(${CMAKE_CURRENT_SOURCE_DIR}/led_indicators.c)
    zephyr_library_sources(${ZMKFIRMWARE_ZEPHYR_MODULE_DIR}/app/src/led_widgets.c)
    zephyr_include_directories(${ZMKFIRMWARE_ZEPHYR_MODULE_DIR}/app/include)
endif()
