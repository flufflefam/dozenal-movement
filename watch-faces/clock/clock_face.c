/* SPDX-License-Identifier: MIT */

/*
 * MIT License
 *
 * Copyright © 2021-2023 Joey Castillo <joeycastillo@utexas.edu> <jose.castillo@gmail.com>
 * Copyright © 2022 David Keck <davidskeck@users.noreply.github.com>
 * Copyright © 2022 TheOnePerson <a.nebinger@web.de>
 * Copyright © 2023 Jeremy O'Brien <neutral@fastmail.com>
 * Copyright © 2023 Mikhail Svarichevsky <3@14.by>
 * Copyright © 2023 Wesley Aptekar-Cassels <me@wesleyac.com>
 * Copyright © 2024 Matheus Afonso Martins Moreira <matheus.a.m.moreira@gmail.com>
 * Copyright © 2026 flufflefam <flufflefam@users.noreply.github.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include "clock_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "watch_common_display.h"

static const char dozenal_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '2', 'E' };

// Table at the end of: https://clocks.dozenal.ca/pdf/watch.pdf
static uint32_t dig1_sec = 2 * 60 * 60;
static uint32_t dig2_sec = 10 * 60;
static uint32_t dig3_sec = 50;
static double dig4_sec = 4 + 1/(double)6;
//static double dig5_sec = 25 / 72;

// Cannot reliably process mode button presses at 64
static uint8_t dozenal_tick_frequency = 16;

static void clock_display_dozenal(watch_date_time_t date_time, uint8_t subsecond, clock_display_t current_display) {
    char buf[11];
    uint32_t tsec;
    double tsub;
    uint8_t dig0 = 0, dig1, dig2, dig3, dig4;
    uint8_t semidiurnal_adj = 1;
    if (current_display == CLOCK_DISPLAY_SEMIDIURNAL) {
        semidiurnal_adj = 2;
    }

    tsec = (((uint32_t)date_time.unit.hour * 60) + (uint32_t)date_time.unit.minute) * 60 + (uint32_t)date_time.unit.second;
    dig0 = 0;
    dig1 = tsec / (dig1_sec / semidiurnal_adj);
    tsec = tsec % (dig1_sec / semidiurnal_adj);
    if (dig1 > 11) {
        dig0 = 1;
        dig1 %= 12;
    }
    dig2 = tsec / (dig2_sec / semidiurnal_adj);
    tsec = tsec % (dig2_sec / semidiurnal_adj);
    dig3 = tsec / (dig3_sec / semidiurnal_adj);
    tsec = tsec % (dig3_sec / semidiurnal_adj);
    // leftover subseconds
    tsub = (double)tsec + (double)subsecond / (double)dozenal_tick_frequency;
    dig4 = tsub / (dig4_sec / semidiurnal_adj);
    if (current_display == CLOCK_DISPLAY_DIURNAL) {
        sprintf(buf, " %c%c%c%c ", dozenal_digits[dig1], dozenal_digits[dig2], dozenal_digits[dig3], dozenal_digits[dig4]);
    } else if (current_display == CLOCK_DISPLAY_SEMIDIURNAL) {
        sprintf(buf, "%c%c%c%c%c ", dozenal_digits[dig0], dozenal_digits[dig1], dozenal_digits[dig2], dozenal_digits[dig3], dozenal_digits[dig4]);
    }
    // To get 10 as "2 without top bar" first render as 2
    watch_display_text(WATCH_POSITION_BOTTOM, buf);
    // Then turn off top bar segments as needed
    if (dig0 == 10) {
        watch_clear_pixel(3, 16);
    }
    if (dig1 == 10) {
        watch_clear_pixel(3, 14);
    }
    if (dig2 == 10) {
        watch_clear_pixel(3, 1);
    }
    if (dig3 == 10) {
        watch_clear_pixel(3, 3);
    }
    if (dig4 == 10) {
        watch_clear_pixel(3, 10);
    }
}

// 2.4 volts seems to offer adequate warning of a low battery condition?
// refined based on user reports and personal observations; may need further adjustment.
#ifndef CLOCK_FACE_LOW_BATTERY_VOLTAGE_THRESHOLD
#define CLOCK_FACE_LOW_BATTERY_VOLTAGE_THRESHOLD 2400
#endif

static void clock_indicate(watch_indicator_t indicator, bool on) {
    if (on) {
        watch_set_indicator(indicator);
    } else {
        watch_clear_indicator(indicator);
    }
}

static void clock_indicate_alarm() {
    clock_indicate(WATCH_INDICATOR_SIGNAL, movement_alarm_enabled());
}

static void clock_indicate_time_signal(clock_state_t *state) {
    clock_indicate(WATCH_INDICATOR_BELL, state->time_signal_enabled);
}

static void clock_indicate_24h() {
    clock_indicate(WATCH_INDICATOR_24H, !!movement_clock_mode_24h());
}

static bool clock_is_pm(watch_date_time_t date_time) {
    return date_time.unit.hour >= 12;
}

static void clock_indicate_pm(watch_date_time_t date_time) {
    if (movement_clock_mode_24h()) { return; }
    clock_indicate(WATCH_INDICATOR_PM, clock_is_pm(date_time));
}

static void clock_indicate_low_available_power(clock_state_t *state) {
    // Set the low battery indicator if battery power is low
    if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM) {
        // interlocking arrows imply "exchange" the battery.
        clock_indicate(WATCH_INDICATOR_ARROWS, state->battery_low);
    } else {
        // LAP indicator on classic LCD is an adequate fallback.
        clock_indicate(WATCH_INDICATOR_LAP, state->battery_low);
    }
}

static watch_date_time_t clock_24h_to_12h(watch_date_time_t date_time) {
    date_time.unit.hour %= 12;

    if (date_time.unit.hour == 0) {
        date_time.unit.hour = 12;
    }

    return date_time;
}

static void clock_check_battery_periodically(clock_state_t *state, watch_date_time_t date_time) {
    // check the battery voltage once a day
    if (date_time.unit.day == state->last_battery_check) { return; }

    state->last_battery_check = date_time.unit.day;

    uint16_t voltage = watch_get_vcc_voltage();

    state->battery_low = voltage < CLOCK_FACE_LOW_BATTERY_VOLTAGE_THRESHOLD;

    clock_indicate_low_available_power(state);
}

static void clock_toggle_time_signal(clock_state_t *state) {
    state->time_signal_enabled = !state->time_signal_enabled;
    clock_indicate_time_signal(state);
}

static void clock_display_all(watch_date_time_t date_time) {
    char buf[8 + 1];

    snprintf(
        buf,
        sizeof(buf),
        movement_clock_mode_24h() == MOVEMENT_CLOCK_MODE_024H ? "%02d%02d%02d%02d" : "%2d%2d%02d%02d",
        date_time.unit.day,
        date_time.unit.hour,
        date_time.unit.minute,
        date_time.unit.second
    );

    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, watch_utility_get_long_weekday(date_time), watch_utility_get_weekday(date_time));
    watch_display_text(WATCH_POSITION_TOP_RIGHT, buf);
    watch_display_text(WATCH_POSITION_BOTTOM, buf + 2);
}

static bool clock_display_some(watch_date_time_t current, watch_date_time_t previous) {
    if ((current.reg >> 6) == (previous.reg >> 6)) {
        // everything before seconds is the same, don't waste cycles setting those segments.

        watch_display_character_lp_seconds('0' + current.unit.second / 10, 8);
        watch_display_character_lp_seconds('0' + current.unit.second % 10, 9);

        return true;

    } else if ((current.reg >> 12) == (previous.reg >> 12)) {
        // everything before minutes is the same.

        char buf[4 + 1];

        snprintf(
            buf,
            sizeof(buf),
            "%02d%02d",
            current.unit.minute,
            current.unit.second
        );

        watch_display_text(WATCH_POSITION_MINUTES, buf);
        watch_display_text(WATCH_POSITION_SECONDS, buf + 2);

        return true;

    } else {
        // other stuff changed; let's do it all.
        return false;
    }
}

static void clock_display_clock(clock_state_t *state, watch_date_time_t current, uint8_t subsecond) {
    if ((state->current_display == CLOCK_DISPLAY_DIURNAL) || (state->current_display == CLOCK_DISPLAY_SEMIDIURNAL)) {
        clock_display_dozenal(current, subsecond, state->current_display);
        return;
    }

    if (!clock_display_some(current, state->date_time.previous)) {
        if (state->current_display == CLOCK_DISPLAY_12H) {
            clock_indicate_pm(current);
            current = clock_24h_to_12h(current);
        }
        clock_display_all(current);
    }
}

static void clock_display_low_energy(watch_date_time_t date_time) {
    if (movement_clock_mode_24h() == MOVEMENT_CLOCK_MODE_12H) {
        clock_indicate_pm(date_time);
        date_time = clock_24h_to_12h(date_time);
    }
    char buf[8 + 1];

    snprintf(
        buf,
        sizeof(buf),
        movement_clock_mode_24h() == MOVEMENT_CLOCK_MODE_024H ? "%02d%02d%02d  " : "%2d%2d%02d  ",
        date_time.unit.day,
        date_time.unit.hour,
        date_time.unit.minute
    );

    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, watch_utility_get_long_weekday(date_time), watch_utility_get_weekday(date_time));
    watch_display_text(WATCH_POSITION_TOP_RIGHT, buf);
    watch_display_text(WATCH_POSITION_BOTTOM, buf + 2);
}

static void clock_start_tick_tock_animation(void) {
    if (!watch_sleep_animation_is_running()) {
        watch_start_sleep_animation(500);
        watch_start_indicator_blink_if_possible(WATCH_INDICATOR_COLON, 500);
    }
}

static void clock_stop_tick_tock_animation(void) {
    if (watch_sleep_animation_is_running()) {
        watch_stop_sleep_animation();
        watch_stop_blink();
    }
}

void clock_face_setup(uint8_t watch_face_index, void ** context_ptr) {
    (void) watch_face_index;

    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(clock_state_t));
        clock_state_t *state = (clock_state_t *) *context_ptr;
        state->time_signal_enabled = false;
        state->watch_face_index = watch_face_index;
    }
}

void clock_face_activate(void *context) {
    clock_state_t *state = (clock_state_t *) context;

    clock_stop_tick_tock_animation();

    clock_indicate_time_signal(state);
    clock_indicate_alarm();
    clock_indicate_24h();

    watch_set_colon();

    // this ensures that none of the timestamp fields will match, so we can re-render them all.
    state->date_time.previous.reg = 0xFFFFFFFF;
}

bool clock_face_loop(movement_event_t event, void *context) {
    clock_state_t *state = (clock_state_t *) context;
    watch_date_time_t current;

    switch (event.event_type) {
        case EVENT_LOW_ENERGY_UPDATE:
            clock_start_tick_tock_animation();
            clock_display_low_energy(movement_get_local_date_time());
            break;
        case EVENT_TICK:
        case EVENT_ACTIVATE:
            current = movement_get_local_date_time();

            clock_display_clock(state, current, event.subsecond);

            clock_check_battery_periodically(state, current);

            state->date_time.previous = current;

            break;
        case EVENT_ALARM_BUTTON_UP:
            // Cycle through decimal/dozenal display modes as listed in clock_display_t
            state->current_display = (state->current_display + 1) % CLOCK_DISPLAY_NUM_MODES;
            // Force re-render of all digits as in clock_face_activate()
            state->date_time.previous.reg = 0xFFFFFFFF;
            // Adjust tick frequencies & diplay for type of time
            if (state->current_display == CLOCK_DISPLAY_12H) {
                movement_request_tick_frequency(1);
                watch_set_colon();
                clock_indicate(WATCH_INDICATOR_24H, 0);
                clock_indicate(WATCH_INDICATOR_PM, clock_is_pm(current));
            } else if (state->current_display == CLOCK_DISPLAY_24H) {
                watch_set_colon();
                clock_indicate(WATCH_INDICATOR_24H, 1);
                clock_indicate(WATCH_INDICATOR_PM, 0);
            } else if (state->current_display == CLOCK_DISPLAY_DIURNAL) {
                movement_request_tick_frequency(dozenal_tick_frequency);
                watch_clear_colon();
                clock_indicate(WATCH_INDICATOR_24H, 0);
                clock_indicate(WATCH_INDICATOR_PM, 0);
            } else if (state->current_display == CLOCK_DISPLAY_SEMIDIURNAL) {
                watch_clear_colon();
                clock_indicate(WATCH_INDICATOR_24H, 0);
                clock_indicate(WATCH_INDICATOR_PM, 0);
            }
            //printf("EVENT_ALARM_BUTTON_UP - %d\r\n", state->current_display);
            break;
        case EVENT_ALARM_LONG_PRESS:
            clock_toggle_time_signal(state);
            break;
        case EVENT_BACKGROUND_TASK:
            // uncomment this line to snap back to the clock face when the hour signal sounds:
            // movement_move_to_face(state->watch_face_index);
            movement_play_signal();
            break;
        default:
            return movement_default_loop_handler(event);
    }

    return true;
}

void clock_face_resign(void *context) {
    (void) context;
}

movement_watch_face_advisory_t clock_face_advise(void *context) {
    movement_watch_face_advisory_t retval = { 0 };
    clock_state_t *state = (clock_state_t *) context;

    if (state->time_signal_enabled) {
        watch_date_time_t date_time = movement_get_local_date_time();
        retval.wants_background_task = date_time.unit.minute == 0;
    }

    return retval;
}
