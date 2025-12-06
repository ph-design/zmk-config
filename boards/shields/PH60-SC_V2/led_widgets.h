/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/devicetree.h>
#include <stdint.h>

typedef enum {
    LED_EVENT_USB = 0,
    LED_EVENT_BLE,
    LED_EVENT_BATTERY,
    LED_EVENT_SIZE,
} led_event_type_t;

typedef enum {
    LED_STATE_IDLE = 0,
    LED_STATE_PAUSE,
    LED_STATE_ACTIVE,
} led_state_t;

// Get LED device count
#define LEDS LIST_DROP_EMPTY(DT_SUPPORTS_DEP_ORDS(DT_CHOSEN(zmk_led_widgets_dev)))
#define _NARG(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, N, ...) N
#define NARG(...) _NARG(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define NUM_LEDS UTIL_EVAL(NARG(LEDS))

// LED command structure
typedef struct {
    uint8_t brightness[NUM_LEDS];  // Brightness value (0-100)
    uint16_t timeout;               // Delay time (milliseconds)
} led_cmd_t;

// LED Widget structure
typedef struct {
    uint8_t arg;                    // Argument (e.g., connection state, battery level)
    uint8_t priority;               // Priority (higher value = higher priority)
    uint32_t period;                // Loop period (milliseconds, 0 = no loop)
    uint8_t cmd_len;                // Command count
    led_cmd_t commands[10];         // Command sequence (max 10 commands)
} led_widget_t;

// Helper macros
#define _ZERO(a) 0
#define WAIT(t) \
    { {FOR_EACH(_ZERO, (, ), LEDS)}, t }
#define CMD(t, ...) \
    { {__VA_ARGS__}, t }
#define WIDGET(a, prio, per, len, ...) \
    { \
        .arg = a, .priority = prio, .period = per, .cmd_len = len, .commands = { __VA_ARGS__ } \
    }
