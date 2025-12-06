/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/ble.h>
#include "led_widgets.h"
#include "led_map.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct device *leds = DEVICE_DT_GET(DT_CHOSEN(zmk_led_widgets_dev));
extern const led_widget_t led_widgets[LED_EVENT_SIZE][CONFIG_ZMK_LED_WIDGETS_MAX_WIDGET_NUM];

#define PAUSE_TIMEOUT_MS 100
#define LED_ACTIVE_WIDGET_GET(i) (led_widgets[i][active_widgets_ind[i]])

// Work queue and state
static void led_widget_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(led_widget_work, led_widget_work_cb);
static led_state_t state = LED_STATE_IDLE;
static int8_t active_widget_type = -1;
static int8_t active_widgets_ind[LED_EVENT_SIZE];
static int8_t last_widgets_ind[LED_EVENT_SIZE];
static uint8_t led_cmd_ind = 0;
static struct k_timer loop_timers[LED_EVENT_SIZE];
static bool loop_timer_started[LED_EVENT_SIZE];

// Check if widget is a status widget (single command with no delay)
static bool widget_is_status(const led_widget_t *widget) {
    return widget->cmd_len == 1 && widget->commands[0].timeout == 0;
}

// Turn off all LEDs (map by physical key -> LED index)
static void led_off_all(void) {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        led_off(leds, led_index_by_key[i]);
    }
}

// Execute widget command
static void run_widget_cmd(const led_event_type_t ev, const uint8_t cmd_ind) {
    const led_widget_t *active_widget = &LED_ACTIVE_WIDGET_GET(ev);
    const uint8_t cmd_len = active_widget->cmd_len;
    const led_cmd_t *cmd = &active_widget->commands[cmd_ind];
    
    if (cmd_ind == 0) {
        LOG_DBG("run widget %u", ev);
        const uint32_t period = active_widget->period;
        if (period > 0) {
            LOG_DBG("start loop timer: %u ms", period);
            if (!loop_timer_started[ev]) {
                k_timer_start(&loop_timers[ev], K_MSEC(period), K_MSEC(period));
                loop_timer_started[ev] = true;
            }
        } else {
            k_timer_stop(&loop_timers[ev]);
            loop_timer_started[ev] = false;
        }
    }
    
    // Set LED brightness using serpentine mapping: physical key i -> led_index_by_key[i]
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        led_set_brightness(leds, led_index_by_key[i], cmd->brightness[i]);
    }
    
    // Schedule next command
    if (cmd->timeout > 0) {
        k_work_schedule(&led_widget_work, K_MSEC(cmd->timeout));
    }
    
    active_widget_type = ev;
    
    // Check if finished
    if (cmd_len == cmd_ind + 1) {
        state = LED_STATE_IDLE;
        return;
    }
    
    state = LED_STATE_ACTIVE;
    led_cmd_ind = cmd_ind;
}

// Pause current widget
static void led_widget_pause(void) {
    LOG_DBG("-> pause");
    led_off_all();
    state = LED_STATE_PAUSE;
    k_work_schedule(&led_widget_work, K_MSEC(PAUSE_TIMEOUT_MS));
}

// Work queue callback
static void led_widget_work_cb(struct k_work *_work) {
    switch (state) {
    case LED_STATE_IDLE:
        LOG_DBG("state: IDLE");
        led_off_all();
        if (active_widget_type >= 0) {
            last_widgets_ind[active_widget_type] = active_widgets_ind[active_widget_type];
            active_widgets_ind[active_widget_type] = -1;
        }
        active_widget_type = -1;
        
        // Find widget with highest priority
        uint8_t max_priority = 0;
        for (uint8_t i = 0; i < LED_EVENT_SIZE; i++) {
            if (active_widgets_ind[i] != -1 && LED_ACTIVE_WIDGET_GET(i).priority > max_priority) {
                max_priority = LED_ACTIVE_WIDGET_GET(i).priority;
                active_widget_type = i;
            }
        }
        
        if (active_widget_type != -1) {
            LOG_DBG("next widget: %d", active_widget_type);
            led_widget_pause();
        }
        break;
        
    case LED_STATE_PAUSE:
        LOG_DBG("state: PAUSE");
        run_widget_cmd(active_widget_type, 0);
        break;
        
    case LED_STATE_ACTIVE:
        LOG_DBG("state: ACTIVE");
        led_off_all();
        run_widget_cmd(active_widget_type, led_cmd_ind + 1);
        break;
    }
}

// Schedule widget
static void led_widget_schedule(const led_event_type_t ev, const uint8_t widget) {
    LOG_DBG("schedule: event=%u widget=%u", ev, widget);
    
    // Skip if same status widget
    if (active_widgets_ind[ev] == widget && widget_is_status(&LED_ACTIVE_WIDGET_GET(ev))) {
        return;
    }
    
    active_widgets_ind[ev] = widget;
    
    if (active_widget_type >= 0) {
        // Already have active widget
        if (state == LED_STATE_PAUSE || 
            LED_ACTIVE_WIDGET_GET(ev).priority <= LED_ACTIVE_WIDGET_GET(active_widget_type).priority) {
            return;
        }
        
        // If status widget, show immediately
        if (widget_is_status(&LED_ACTIVE_WIDGET_GET(ev))) {
            led_off_all();
            run_widget_cmd(ev, 0);
            return;
        }
        
        active_widget_type = ev;
        led_widget_pause();
    } else {
        run_widget_cmd(ev, 0);
    }
}

// Loop timer handler
static void loop_timer_handler(struct k_timer *timer) {
    const led_event_type_t ev = (timer - loop_timers);
    LOG_DBG("loop timer: event=%u", ev);
    if (last_widgets_ind[ev] >= 0) {
        led_widget_schedule(ev, last_widgets_ind[ev]);
    }
}

// Event listener
static int led_widgets_event_listener(const zmk_event_t *ev) {
    // USB connection state
    const struct zmk_usb_conn_state_changed *usb_ev = as_zmk_usb_conn_state_changed(ev);
    if (usb_ev) {
        uint8_t widget_idx = usb_ev->conn_state == ZMK_USB_CONN_HID ? 0 : 1;
        LOG_INF("USB: %s", usb_ev->conn_state == ZMK_USB_CONN_HID ? "connected" : "disconnected");
        led_widget_schedule(LED_EVENT_USB, widget_idx);
        return ZMK_EV_EVENT_BUBBLE;
    }
    
    // Battery state
    const struct zmk_battery_state_changed *bat_ev = as_zmk_battery_state_changed(ev);
    if (bat_ev) {
        LOG_INF("Battery: %u%%", bat_ev->state_of_charge);
        // Select widget based on battery level: 0=normal, 1=low
        uint8_t widget_idx = bat_ev->state_of_charge < 20 ? 1 : 0;
        led_widget_schedule(LED_EVENT_BATTERY, widget_idx);
        return ZMK_EV_EVENT_BUBBLE;
    }
    
    // BLE state - need to manually check connection status
    const struct zmk_ble_active_profile_changed *ble_ev = as_zmk_ble_active_profile_changed(ev);
    if (ble_ev) {
        bool connected = zmk_ble_active_profile_is_connected();
        LOG_INF("BLE: %s", connected ? "connected" : "disconnected");
        // 0=connected, 1=disconnected
        uint8_t widget_idx = connected ? 0 : 1;
        led_widget_schedule(LED_EVENT_BLE, widget_idx);
        return ZMK_EV_EVENT_BUBBLE;
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

// Initialization
static int led_widgets_init(void) {
    if (!device_is_ready(leds)) {
        LOG_ERR("LED device not ready");
        return -ENODEV;
    }
    
    // Test LED on startup - blink first mapped LED 3 times
    LOG_INF("Testing LED...");
    for (int i = 0; i < 3; i++) {
        led_set_brightness(leds, led_index_by_key[0], 100);
        k_sleep(K_MSEC(200));
        led_set_brightness(leds, led_index_by_key[0], 0);
        k_sleep(K_MSEC(200));
    }
    
    for (uint8_t i = 0; i < LED_EVENT_SIZE; i++) {
        active_widgets_ind[i] = -1;
        last_widgets_ind[i] = -1;
        loop_timer_started[i] = false;
        k_timer_init(&loop_timers[i], loop_timer_handler, NULL);
    }
    
    LOG_INF("LED widgets initialized");
    return 0;
}

ZMK_LISTENER(led_widgets_event, led_widgets_event_listener);
ZMK_SUBSCRIPTION(led_widgets_event, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(led_widgets_event, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(led_widgets_event, zmk_ble_active_profile_changed);

SYS_INIT(led_widgets_init, APPLICATION, CONFIG_ZMK_LED_WIDGETS_INIT_PRIORITY);
