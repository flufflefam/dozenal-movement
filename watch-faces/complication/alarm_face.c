/*
 * MIT License
 *
 * Copyright (c) 2022 Josh Berson, building on Wesley Ellis’ countdown_face.c
 * Copyright (c) 2025 Joey Castillo
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
#include <string.h>
#include "alarm_face.h"
#include "clock_face.h"
#include "watch.h"
#include "watch_common_display.h"
#include "watch_utility.h"

//
// Private
//

static bool _alarm_face_is_dozenal_mode(void) {
    clock_display_t display_mode = clock_face_get_display_mode();
    return display_mode == CLOCK_DISPLAY_DIURNAL || display_mode == CLOCK_DISPLAY_SEMIDIURNAL;
}

static uint32_t _alarm_face_third_digit_increment(void) {
    return clock_face_get_display_mode() == CLOCK_DISPLAY_DIURNAL ? 50 : 25;
}

static void _alarm_face_normalize_time(alarm_face_state_t *state) {
    if (!_alarm_face_is_dozenal_mode()) {
        state->second = 0;
        return;
    }

    state->second = (state->second / _alarm_face_third_digit_increment()) * _alarm_face_third_digit_increment();
}

static void _alarm_face_display_alarm_time(alarm_face_state_t *state) {
    clock_display_t display_mode = clock_face_get_display_mode();
    _alarm_face_normalize_time(state);

    watch_clear_indicator(WATCH_INDICATOR_24H);
    watch_clear_indicator(WATCH_INDICATOR_PM);

    if (display_mode == CLOCK_DISPLAY_DIURNAL || display_mode == CLOCK_DISPLAY_SEMIDIURNAL) {
        uint32_t seconds = (((uint32_t) state->hour * 60 + state->minute) * 60) + state->second;
        watch_clear_colon();
        clock_display_dozenal_duration(seconds, 0, display_mode, false);
        watch_display_character(' ', 8);
        return;
    }

    uint8_t hour = state->hour;
    if (display_mode == CLOCK_DISPLAY_24H) {
        watch_set_indicator(WATCH_INDICATOR_24H);
    } else {
        if (hour >= 12) watch_set_indicator(WATCH_INDICATOR_PM);
        hour = hour % 12 ? hour % 12 : 12;
    }

    static char lcdbuf[7];
    sprintf(lcdbuf, "%2d%02d  ", hour, state->minute);

    watch_set_colon();
    watch_display_text(WATCH_POSITION_BOTTOM, lcdbuf);
}

static void _alarm_face_display_modes(void) {
    if (movement_time_signal_enabled()) watch_set_indicator(WATCH_INDICATOR_BELL);
    else watch_clear_indicator(WATCH_INDICATOR_BELL);
    if (movement_alarm_enabled()) watch_set_indicator(WATCH_INDICATOR_SIGNAL);
    else watch_clear_indicator(WATCH_INDICATOR_SIGNAL);
}

static void _alarm_face_advance_time(alarm_face_state_t *state, uint32_t seconds) {
    uint32_t alarm_seconds = (((uint32_t) state->hour * 60 + state->minute) * 60) + state->second;
    alarm_seconds = (alarm_seconds + seconds) % (24 * 60 * 60);
    state->hour = alarm_seconds / (60 * 60);
    state->minute = (alarm_seconds / 60) % 60;
    state->second = alarm_seconds % 60;
}

static uint32_t _alarm_face_hour_increment(void) {
    clock_display_t display_mode = clock_face_get_display_mode();
    if (display_mode == CLOCK_DISPLAY_DIURNAL) return 2 * 60 * 60;
    return 60 * 60;
}

static uint32_t _alarm_face_minute_increment(void) {
    clock_display_t display_mode = clock_face_get_display_mode();
    if (display_mode == CLOCK_DISPLAY_DIURNAL) return 50;
    if (display_mode == CLOCK_DISPLAY_SEMIDIURNAL) return 25;
    return 60;
}

static void _alarm_face_advance_selected_value(alarm_face_state_t *state) {
    if (state->setting_mode == ALARM_FACE_SETTING_MODE_SETTING_HOUR) {
        _alarm_face_advance_time(state, _alarm_face_hour_increment());
    } else if (state->setting_mode == ALARM_FACE_SETTING_MODE_SETTING_MINUTE) {
        _alarm_face_advance_time(state, _alarm_face_minute_increment());
    }
}

static void _alarm_face_schedule_next_alarm(alarm_face_state_t *state) {
    if (!state->alarm_is_on) {
        movement_cancel_background_task_for_face(state->watch_face_index);
        return;
    }

    _alarm_face_normalize_time(state);

    watch_date_time_t now = movement_get_local_date_time();
    watch_date_time_t target = now;
    target.unit.hour = state->hour;
    target.unit.minute = state->minute;
    target.unit.second = state->second;

    uint32_t now_timestamp = watch_utility_date_time_to_unix_time(now, 0);
    uint32_t target_timestamp = watch_utility_date_time_to_unix_time(target, 0);
    if (target_timestamp <= now_timestamp) {
        target = watch_utility_date_time_from_unix_time(now_timestamp + 24 * 60 * 60, 0);
        target.unit.hour = state->hour;
        target.unit.minute = state->minute;
        target.unit.second = state->second;
    }

    movement_schedule_background_task_for_face(state->watch_face_index, target);
}

static void _alarm_face_blink_setting(alarm_face_state_t *state) {
    if (_alarm_face_is_dozenal_mode()) {
        if (state->setting_mode == ALARM_FACE_SETTING_MODE_SETTING_MINUTE) {
            watch_display_character(' ', 6);
            watch_display_character(' ', 7);
        } else if (state->setting_mode == ALARM_FACE_SETTING_MODE_SETTING_HOUR) {
            if (clock_face_get_display_mode() == CLOCK_DISPLAY_SEMIDIURNAL) {
                watch_display_character(' ', 4);
            }
            watch_display_character(' ', 5);
        }
    } else {
        watch_display_text((state->setting_mode == ALARM_FACE_SETTING_MODE_SETTING_HOUR) ? WATCH_POSITION_HOURS : WATCH_POSITION_MINUTES, "  ");
    }
}

static inline void button_beep() {
    // play a beep as confirmation for a button press (if applicable)
    if (movement_button_should_sound()) watch_buzzer_play_note_with_volume(BUZZER_NOTE_C7, 50, movement_button_volume());
}

//
// Exported
//

void alarm_face_setup(uint8_t watch_face_index, void **context_ptr) {
    (void) watch_face_index;

    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(alarm_face_state_t));
        alarm_face_state_t *state = (alarm_face_state_t *)*context_ptr;
        memset(*context_ptr, 0, sizeof(alarm_face_state_t));
        state->watch_face_index = watch_face_index;

        // default to an 8:00 AM alarm time.
        state->hour = 8;
    }
}

void alarm_face_activate(void *context) {
    alarm_face_state_t *state = (alarm_face_state_t *)context;
    state->setting_mode = ALARM_FACE_SETTING_MODE_NONE;
    state->upper_button_pressed = false;
    state->alarm_button_pressed = false;
    state->alarm_button_repeat_active = false;
    state->alarm_button_repeat_ticks = 0;
}
void alarm_face_resign(void *context) {
    (void) context;
}

bool alarm_face_loop(movement_event_t event, void *context) {
    alarm_face_state_t *state = (alarm_face_state_t *)context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "ALM", "AL");
            state->alarm_is_on = movement_alarm_enabled();
            _alarm_face_display_modes();
            watch_set_colon();
            _alarm_face_schedule_next_alarm(state);
            _alarm_face_display_alarm_time(state);
            break;
        case EVENT_TICK:
            // No action needed for tick events in normal mode; we displayed our stuff in EVENT_ACTIVATE.
            if (state->setting_mode == ALARM_FACE_SETTING_MODE_NONE)
                break;

            // but in settings mode, we need to blink up the parameter we're setting.
            _alarm_face_display_alarm_time(state);
            if (state->alarm_button_repeat_active) {
                state->alarm_button_repeat_ticks++;
                if (state->alarm_button_repeat_ticks >= 4) {
                    state->alarm_button_repeat_ticks = 0;
                    _alarm_face_advance_selected_value(state);
                    _alarm_face_display_alarm_time(state);
                }
            }
            if (event.subsecond % 2 == 0) _alarm_face_blink_setting(state);
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            state->upper_button_pressed = true;
            state->alarm_button_repeat_active = false;
            state->alarm_button_repeat_ticks = 0;
            switch (state->setting_mode) {
                case ALARM_FACE_SETTING_MODE_NONE:
                    state->setting_mode = ALARM_FACE_SETTING_MODE_SETTING_HOUR;
                    movement_request_tick_frequency(4);
                    button_beep();
                    break;
                case ALARM_FACE_SETTING_MODE_SETTING_HOUR:
                    // If we're setting the hour, advance to minute set mode.
                    state->setting_mode = ALARM_FACE_SETTING_MODE_SETTING_MINUTE;
                    break;
                case ALARM_FACE_SETTING_MODE_SETTING_MINUTE:
                    state->setting_mode = ALARM_FACE_SETTING_MODE_NONE;
                    break;
            }
            if (state->setting_mode == ALARM_FACE_SETTING_MODE_NONE) {
                movement_request_tick_frequency(1);
                button_beep();
                state->alarm_is_on = 1;
                movement_set_alarm_enabled(true);
                _alarm_face_schedule_next_alarm(state);
                _alarm_face_display_alarm_time(state);
                _alarm_face_display_modes();
            }
            break;
        case EVENT_MODE_BUTTON_UP:
            if (state->upper_button_pressed) {
                state->upper_button_pressed = false;
                movement_move_to_face(0);
            } else {
                movement_default_loop_handler(event);
            }
            break;
        case EVENT_ALARM_BUTTON_UP:
        case EVENT_ALARM_LONG_UP:
            state->alarm_button_pressed = false;
            state->alarm_button_repeat_active = false;
            state->alarm_button_repeat_ticks = 0;
            if (state->setting_mode == ALARM_FACE_SETTING_MODE_NONE) {
                if (!movement_time_signal_enabled() && !movement_alarm_enabled()) {
                    movement_set_time_signal_enabled(true);
                } else if (movement_time_signal_enabled() && !movement_alarm_enabled()) {
                    movement_set_time_signal_enabled(false);
                    movement_set_alarm_enabled(true);
                    state->alarm_is_on = 1;
                    _alarm_face_schedule_next_alarm(state);
                } else if (!movement_time_signal_enabled() && movement_alarm_enabled()) {
                    movement_set_time_signal_enabled(true);
                } else {
                    movement_set_time_signal_enabled(false);
                    movement_set_alarm_enabled(false);
                    state->alarm_is_on = 0;
                    movement_cancel_background_task_for_face(state->watch_face_index);
                }
                _alarm_face_display_modes();
            }
            break;
        case EVENT_ALARM_BUTTON_DOWN:
            if (state->alarm_button_pressed) {
                break;
            }
            state->upper_button_pressed = true;
            state->alarm_button_pressed = true;
            state->alarm_button_repeat_ticks = 0;
            _alarm_face_advance_selected_value(state);
            if (state->setting_mode != ALARM_FACE_SETTING_MODE_NONE) {
                _alarm_face_display_alarm_time(state);
            }
            break;
        case EVENT_ALARM_LONG_PRESS:
            if (state->alarm_button_pressed && state->setting_mode != ALARM_FACE_SETTING_MODE_NONE) {
                state->alarm_button_repeat_active = true;
                state->alarm_button_repeat_ticks = 0;
            }
            break;
        case EVENT_LIGHT_LONG_PRESS:
            state->alarm_button_repeat_active = false;
            state->alarm_button_repeat_ticks = 0;
            if (state->setting_mode == ALARM_FACE_SETTING_MODE_NONE) {
                state->setting_mode = ALARM_FACE_SETTING_MODE_SETTING_HOUR;
                movement_request_tick_frequency(4);
                button_beep();
            }
            break;
        case EVENT_BACKGROUND_TASK:
            movement_play_alarm();
            _alarm_face_schedule_next_alarm(state);
                // 2022-07-23: Thx @joeycastillo for the dedicated “alarm” signal
            break;
        case EVENT_TIMEOUT:
            movement_move_to_face(0);
            break;
        case EVENT_LOW_ENERGY_UPDATE:
            break;
        default:
            movement_default_loop_handler(event);
            break;
    }

    return true;
}

movement_watch_face_advisory_t alarm_face_advise(void *context) {
    alarm_face_state_t *state = (alarm_face_state_t *)context;
    movement_watch_face_advisory_t retval = { 0 };

    if ( state->alarm_is_on ) {
        _alarm_face_schedule_next_alarm(state);
    }

    return retval;
}
