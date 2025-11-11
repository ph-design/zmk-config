# Copyright (c) 2025 The ZMK Contributors
# SPDX-License-Identifier: MIT

# Add LED widgets source files
if(CONFIG_ZMK_LED_WIDGETS)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/led_widgets.c)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/led_indicators.c)
endif()
