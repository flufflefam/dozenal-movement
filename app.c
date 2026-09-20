#include "app.h"
#include "watch.h"
#include "watch_private.h"
#include "watch_common_display.h"
#include "watch_utility.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Declarations for buzzer functions from watch_common_buzzer.c
void watch_buzzer_play_sequence(int8_t *note_sequence, watch_cb_t callback);

// --- Definitions & Constants ---
#define LONG_PRESS_TICKS 8  // ~0.5s at 16Hz RTC tick
#define HOLD_REPEAT_DELAY_TICKS 8
#define HOLD_REPEAT_RATE_TICKS 2

typedef enum {
    WATCH_MODE_CLOCK = 0,
    WATCH_MODE_ALARM,
    WATCH_MODE_STOPWATCH,
    WATCH_MODE_SET_TIME,
    WATCH_MODE_NUM
} watch_app_mode_t;

typedef enum {
    TIME_MODE_12H = 0,
    TIME_MODE_24H,
    TIME_MODE_DIURNAL,
    TIME_MODE_SEMIDIURNAL,
    TIME_MODE_NUM
} time_display_mode_t;

static const char dozenal_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '2', 'E' };

static const uint32_t dig1_sec = 2 * 60 * 60;
static const uint32_t dig2_sec = 10 * 60;
static const uint32_t dig3_sec = 50;
static const double dig4_sec = 4.0 + 1.0 / 6.0;
static const double dig5_sec = 25.0 / 72.0;

// --- Application Global State ---
typedef struct {
    watch_app_mode_t app_mode;
    time_display_mode_t time_mode;

    // Button state
    bool mode_btn_down;
    bool light_btn_down;
    bool alarm_btn_down;

    uint32_t alarm_btn_down_ticks;

    // F-91W direct mode return state
    bool quick_return_to_clock;

    // Alarm state
    bool alarm_enabled;
    bool chime_enabled;
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    bool alarm_setting_active;
    uint8_t alarm_setting_field; // 0: hour, 1: minute
    int8_t last_alarm_triggered_minute;
    int8_t last_chime_triggered_hour;

    // Stopwatch state
    bool sw_running;
    uint32_t sw_elapsed_ticks;
    bool sw_lap_active;
    uint32_t sw_lap_ticks;

    // Set Time state
    uint8_t set_time_field; // Standard: 0:sec, 1:hour, 2:min, 3:year, 4:month, 5:day. Dozenal/Semidiurnal: 0:Group 1 (dig1), 1:Group 2 (dig2&3), 2:Group 3 (dig4)
    watch_date_time_t setting_dt;

    // Dynamic tick rate
    uint8_t current_tick_freq;
    uint32_t rtc_tick_counter;
    bool blink_state;
} app_state_t;

static app_state_t g_state;

// --- Tune Definitions ---
static int8_t button_beep_tune[] = { BUZZER_NOTE_C7, 2, 0 };
static int8_t mode_return_tune[] = { BUZZER_NOTE_C8, 3, 0 };
static int8_t hourly_chime_tune[] = {
    BUZZER_NOTE_C8, 2,
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_C8, 2,
    0
};
static int8_t alarm_tune[] = {
    BUZZER_NOTE_C8, 3, BUZZER_NOTE_REST, 4,
    BUZZER_NOTE_C8, 3, BUZZER_NOTE_REST, 4,
    BUZZER_NOTE_C8, 3, BUZZER_NOTE_REST, 4,
    BUZZER_NOTE_C8, 5, BUZZER_NOTE_REST, 38,
    -8, 9, 0
};

// --- Prototypes ---
static void clock_display_dozenal_digit(uint8_t digit, uint8_t position);
static void clock_display_dozenal_value(uint32_t value, uint8_t digits, uint8_t position);
static void clock_display_dozenal_duration(uint32_t seconds, uint8_t subsecond, uint8_t tick_freq, time_display_mode_t mode, bool show_extra_digit);
static void clock_display_dozenal_day(watch_date_time_t date_time);

// --- Segment Glyph Helper ---
static void clock_display_dozenal_digit(uint8_t digit, uint8_t position) {
    if (digit != 10) {
        watch_display_character(dozenal_digits[digit], position);
        return;
    }

    digit_mapping_t segmap = (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM) ? Custom_LCD_Display_Mapping[position] : Classic_LCD_Display_Mapping[position];
    uint8_t segdata = 0b01011010; // '2' glyph without top segment

    for (uint8_t i = 0; i < 8; i++) {
        if (segmap.segment[i].value != segment_does_not_exist) {
            uint8_t com = segmap.segment[i].address.com;
            uint8_t seg = segmap.segment[i].address.seg;

            if (segdata & 1) {
                watch_set_pixel(com, seg);
            } else {
                watch_clear_pixel(com, seg);
            }
        }
        segdata >>= 1;
    }
}

static void clock_display_dozenal_value(uint32_t value, uint8_t digits, uint8_t position) {
    uint8_t digit_values[4];
    for (uint8_t i = 0; i < digits; i++) {
        digit_values[digits - i - 1] = value % 12;
        value /= 12;
    }
    for (uint8_t i = 0; i < digits; i++) {
        clock_display_dozenal_digit(digit_values[i], position + i);
    }
}

static void clock_display_dozenal_duration(uint32_t seconds, uint8_t subsecond, uint8_t tick_freq, time_display_mode_t mode, bool show_extra_digit) {
    uint32_t tsec = seconds % (24 * 3600);
    uint8_t semidiurnal_adj = (mode == TIME_MODE_SEMIDIURNAL) ? 2 : 1;

    uint8_t dig0 = 0;
    uint8_t dig1 = tsec / (dig1_sec / semidiurnal_adj);
    tsec %= (dig1_sec / semidiurnal_adj);
    if (dig1 > 11) {
        dig0 = 1;
        dig1 %= 12;
    }
    uint8_t dig2 = tsec / (dig2_sec / semidiurnal_adj);
    tsec %= (dig2_sec / semidiurnal_adj);
    uint8_t dig3 = tsec / (dig3_sec / semidiurnal_adj);
    tsec %= (dig3_sec / semidiurnal_adj);

    double tsub = (double)tsec + (double)subsecond / (double)(tick_freq ? tick_freq : 1);
    uint8_t dig4 = tsub / (dig4_sec / semidiurnal_adj);
    tsub -= dig4 * (dig4_sec / semidiurnal_adj);
    uint8_t dig5 = tsub / (dig5_sec / semidiurnal_adj);

    if (mode == TIME_MODE_DIURNAL) {
        watch_display_character(' ', 4);
        if (!(g_state.app_mode == WATCH_MODE_SET_TIME && g_state.set_time_field == 0 && g_state.blink_state)) {
            clock_display_dozenal_digit(dig1, 5);
        } else {
            watch_display_character(' ', 5);
        }
        if (!(g_state.app_mode == WATCH_MODE_SET_TIME && g_state.set_time_field == 1 && g_state.blink_state)) {
            clock_display_dozenal_digit(dig2, 6);
            clock_display_dozenal_digit(dig3, 7);
        } else {
            watch_display_character(' ', 6);
            watch_display_character(' ', 7);
        }
        if (!(g_state.app_mode == WATCH_MODE_SET_TIME && g_state.set_time_field == 2 && g_state.blink_state)) {
            clock_display_dozenal_digit(dig4, 8);
        } else {
            watch_display_character(' ', 8);
        }
        if (show_extra_digit) {
            clock_display_dozenal_digit(dig5, 9);
        } else {
            watch_display_character(' ', 9);
        }
    } else if (mode == TIME_MODE_SEMIDIURNAL) {
        if (!(g_state.app_mode == WATCH_MODE_SET_TIME && g_state.set_time_field == 0 && g_state.blink_state)) {
            clock_display_dozenal_digit(dig0, 4);
            clock_display_dozenal_digit(dig1, 5);
        } else {
            watch_display_character(' ', 4);
            watch_display_character(' ', 5);
        }
        if (!(g_state.app_mode == WATCH_MODE_SET_TIME && g_state.set_time_field == 1 && g_state.blink_state)) {
            clock_display_dozenal_digit(dig2, 6);
            clock_display_dozenal_digit(dig3, 7);
        } else {
            watch_display_character(' ', 6);
            watch_display_character(' ', 7);
        }
        if (!(g_state.app_mode == WATCH_MODE_SET_TIME && g_state.set_time_field == 2 && g_state.blink_state)) {
            clock_display_dozenal_digit(dig4, 8);
        } else {
            watch_display_character(' ', 8);
        }
        if (show_extra_digit) {
            clock_display_dozenal_digit(dig5, 9);
        } else {
            watch_display_character(' ', 9);
        }
    }
}

static void clock_display_dozenal_day(watch_date_time_t date_time) {
    clock_display_dozenal_value(date_time.unit.day, 2, 2);
    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, watch_utility_get_long_weekday(date_time), watch_utility_get_weekday(date_time));
}

static void play_beep(int8_t *tune) {
    watch_buzzer_play_sequence(tune, NULL);
}

static void update_indicators(void) {
    if (g_state.alarm_enabled) {
        watch_set_indicator(WATCH_INDICATOR_SIGNAL);
    } else {
        watch_clear_indicator(WATCH_INDICATOR_SIGNAL);
    }

    if (g_state.chime_enabled) {
        watch_set_indicator(WATCH_INDICATOR_BELL);
    } else {
        watch_clear_indicator(WATCH_INDICATOR_BELL);
    }

    if (g_state.time_mode == TIME_MODE_24H) {
        watch_set_indicator(WATCH_INDICATOR_24H);
        watch_clear_indicator(WATCH_INDICATOR_PM);
    } else if (g_state.time_mode == TIME_MODE_12H) {
        watch_clear_indicator(WATCH_INDICATOR_24H);
        watch_date_time_t dt = watch_rtc_get_date_time();
        if (dt.unit.hour >= 12) {
            watch_set_indicator(WATCH_INDICATOR_PM);
        } else {
            watch_clear_indicator(WATCH_INDICATOR_PM);
        }
    } else {
        watch_clear_indicator(WATCH_INDICATOR_24H);
        watch_clear_indicator(WATCH_INDICATOR_PM);
    }
}

static void render_clock_face(void) {
    if (g_state.alarm_btn_down) {
        uint32_t held_ticks = g_state.rtc_tick_counter - g_state.alarm_btn_down_ticks;
        if (held_ticks >= LONG_PRESS_TICKS) {
            watch_clear_colon();
            watch_display_text(WATCH_POSITION_TOP_LEFT, "  ");
            watch_display_text(WATCH_POSITION_TOP_RIGHT, "  ");

            static const char scroll_text[] = "      Dozenal Watch      ";
            uint32_t anim_ticks = held_ticks - LONG_PRESS_TICKS;
            uint32_t frame = (anim_ticks / 4) % 20;

            char buf[7];
            memcpy(buf, &scroll_text[frame], 6);
            buf[6] = '\0';

            watch_display_text(WATCH_POSITION_BOTTOM, buf);
            return;
        }
    }

    watch_date_time_t dt = watch_rtc_get_date_time();

    if (g_state.time_mode == TIME_MODE_DIURNAL || g_state.time_mode == TIME_MODE_SEMIDIURNAL) {
        watch_clear_colon();
        uint32_t seconds = (((uint32_t)dt.unit.hour * 60) + dt.unit.minute) * 60 + dt.unit.second;
        clock_display_dozenal_duration(seconds, 0, g_state.current_tick_freq, g_state.time_mode, false);
        clock_display_dozenal_day(dt);
    } else {
        watch_set_colon();
        char buf[9];
        uint8_t display_hour = dt.unit.hour;
        if (g_state.time_mode == TIME_MODE_12H) {
            display_hour %= 12;
            if (display_hour == 0) display_hour = 12;
            snprintf(buf, sizeof(buf), "%2d%02d%02d", display_hour, dt.unit.minute, dt.unit.second);
        } else {
            snprintf(buf, sizeof(buf), "%02d%02d%02d", display_hour, dt.unit.minute, dt.unit.second);
        }

        watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, watch_utility_get_long_weekday(dt), watch_utility_get_weekday(dt));
        char day_str[3];
        snprintf(day_str, sizeof(day_str), "%02d", dt.unit.day);
        watch_display_text(WATCH_POSITION_TOP_RIGHT, day_str);
        watch_display_text(WATCH_POSITION_BOTTOM, buf);
    }
}

static void render_alarm_face(void) {
    watch_display_text(WATCH_POSITION_TOP_LEFT, "AL");
    watch_display_text(WATCH_POSITION_TOP_RIGHT, "  ");

    if (g_state.time_mode == TIME_MODE_DIURNAL || g_state.time_mode == TIME_MODE_SEMIDIURNAL) {
        watch_clear_colon();
        uint32_t seconds = (((uint32_t)g_state.alarm_hour * 60) + g_state.alarm_minute) * 60;
        clock_display_dozenal_duration(seconds, 0, 1, g_state.time_mode, false);
    } else {
        watch_set_colon();
        char buf[10];
        uint8_t h = g_state.alarm_hour;
        if (g_state.time_mode == TIME_MODE_12H) {
            h %= 12;
            if (h == 0) h = 12;
            snprintf(buf, sizeof(buf), "%2d%02d  ", h, g_state.alarm_minute);
        } else {
            snprintf(buf, sizeof(buf), "%02d%02d  ", h, g_state.alarm_minute);
        }

        if (g_state.alarm_setting_active && g_state.blink_state) {
            if (g_state.alarm_setting_field == 0) {
                buf[0] = ' '; buf[1] = ' ';
            } else {
                buf[2] = ' '; buf[3] = ' ';
            }
        }
        watch_display_text(WATCH_POSITION_BOTTOM, buf);
    }
}

static void render_stopwatch_face(void) {
    watch_display_text(WATCH_POSITION_TOP_LEFT, "ST");
    watch_display_text(WATCH_POSITION_TOP_RIGHT, "  ");
    watch_set_colon();

    uint32_t ticks = g_state.sw_lap_active ? g_state.sw_lap_ticks : g_state.sw_elapsed_ticks;
    // 16 ticks per second -> scale ticks to hundredths (100 / 16 = 6.25)
    uint32_t total_hundredths = (ticks * 100) / 16;
    uint32_t mins = (total_hundredths / 6000) % 60;
    uint32_t secs = (total_hundredths / 100) % 60;
    uint32_t hundredths = total_hundredths % 100;

    char buf[7];
    snprintf(buf, sizeof(buf), "%02d%02d%02d", (uint8_t)mins, (uint8_t)secs, (uint8_t)hundredths);
    watch_display_text(WATCH_POSITION_BOTTOM, buf);
}

static void render_set_time_face(void) {
    watch_date_time_t dt = g_state.setting_dt;

    if (g_state.time_mode == TIME_MODE_DIURNAL || g_state.time_mode == TIME_MODE_SEMIDIURNAL) {
        watch_clear_colon();
        uint32_t seconds = (((uint32_t)dt.unit.hour * 60) + dt.unit.minute) * 60 + dt.unit.second;
        clock_display_dozenal_duration(seconds, 0, 1, g_state.time_mode, false);
        clock_display_dozenal_day(dt);
    } else {
        watch_set_colon();
        char buf[7];
        uint8_t h = dt.unit.hour;
        if (g_state.time_mode == TIME_MODE_12H) {
            h %= 12;
            if (h == 0) h = 12;
            snprintf(buf, sizeof(buf), "%2d%02d%02d", h, dt.unit.minute, dt.unit.second);
        } else {
            snprintf(buf, sizeof(buf), "%02d%02d%02d", h, dt.unit.minute, dt.unit.second);
        }

        char day_str[3];
        snprintf(day_str, sizeof(day_str), "%02d", dt.unit.day);

        // Blinking active field
        if (g_state.blink_state) {
            switch (g_state.set_time_field) {
                case 0: buf[4] = ' '; buf[5] = ' '; break; // sec
                case 1: buf[0] = ' '; buf[1] = ' '; break; // hour
                case 2: buf[2] = ' '; buf[3] = ' '; break; // min
                case 3: // year
                case 4: // month
                case 5: // day
                    break;
            }
        }

        if (g_state.set_time_field >= 3) {
            // Render Year / Month / Day setting on top/bottom display
            char date_str[7];
            snprintf(date_str, sizeof(date_str), "%04d%02d", dt.unit.year + WATCH_RTC_REFERENCE_YEAR, dt.unit.month);
            if (g_state.blink_state) {
                if (g_state.set_time_field == 3) { // year
                    date_str[0] = ' '; date_str[1] = ' '; date_str[2] = ' '; date_str[3] = ' ';
                } else if (g_state.set_time_field == 4) { // month
                    date_str[4] = ' '; date_str[5] = ' ';
                } else if (g_state.set_time_field == 5) { // day
                    day_str[0] = ' '; day_str[1] = ' ';
                }
            }
            watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, watch_utility_get_long_weekday(dt), watch_utility_get_weekday(dt));
            watch_display_text(WATCH_POSITION_TOP_RIGHT, day_str);
            watch_display_text(WATCH_POSITION_BOTTOM, date_str);
        } else {
            watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, watch_utility_get_long_weekday(dt), watch_utility_get_weekday(dt));
            watch_display_text(WATCH_POSITION_TOP_RIGHT, day_str);
            watch_display_text(WATCH_POSITION_BOTTOM, buf);
        }
    }
}

static void render_app(void) {
    update_indicators();

    switch (g_state.app_mode) {
        case WATCH_MODE_CLOCK:
            render_clock_face();
            break;
        case WATCH_MODE_ALARM:
            render_alarm_face();
            break;
        case WATCH_MODE_STOPWATCH:
            render_stopwatch_face();
            break;
        case WATCH_MODE_SET_TIME:
            render_set_time_face();
            break;
        default:
            break;
    }
}

// --- Button Handling & Mode Transitions ---

static void handle_mode_button_press(void) {
    if (g_state.quick_return_to_clock && (g_state.app_mode == WATCH_MODE_ALARM || g_state.app_mode == WATCH_MODE_STOPWATCH)) {
        g_state.app_mode = WATCH_MODE_CLOCK;
        g_state.quick_return_to_clock = false;
        g_state.alarm_setting_active = false;
        g_state.alarm_setting_field = 0;
        g_state.set_time_field = 0;
        g_state.blink_state = false;
        play_beep(mode_return_tune);
        return;
    }

    g_state.quick_return_to_clock = false;
    watch_app_mode_t prev_mode = g_state.app_mode;
    g_state.app_mode = (g_state.app_mode + 1) % WATCH_MODE_NUM;

    g_state.alarm_setting_active = false;
    g_state.alarm_setting_field = 0;
    g_state.set_time_field = 0;
    g_state.blink_state = false;

    if (g_state.app_mode == WATCH_MODE_SET_TIME) {
        g_state.setting_dt = watch_rtc_get_date_time();
    } else if (prev_mode == WATCH_MODE_SET_TIME) {
        watch_rtc_set_date_time(g_state.setting_dt);
    }

    if (g_state.app_mode == WATCH_MODE_CLOCK) {
        play_beep(mode_return_tune);
    } else {
        play_beep(button_beep_tune);
    }
}

static void handle_light_button_press(void) {
    if (g_state.app_mode == WATCH_MODE_CLOCK) {
        return;
    }

    g_state.quick_return_to_clock = true;

    if (g_state.app_mode == WATCH_MODE_ALARM) {
        if (!g_state.alarm_setting_active) {
            g_state.alarm_setting_active = true;
            g_state.alarm_setting_field = 0;
        } else {
            g_state.alarm_setting_field = (g_state.alarm_setting_field + 1) % 2;
        }
        play_beep(button_beep_tune);
    } else if (g_state.app_mode == WATCH_MODE_STOPWATCH) {
        // Lap / Split / Reset
        if (!g_state.sw_running && g_state.sw_elapsed_ticks > 0) {
            g_state.sw_elapsed_ticks = 0;
            g_state.sw_lap_active = false;
        } else if (g_state.sw_running) {
            g_state.sw_lap_active = !g_state.sw_lap_active;
            if (g_state.sw_lap_active) {
                g_state.sw_lap_ticks = g_state.sw_elapsed_ticks;
            }
        }
        play_beep(button_beep_tune);
    } else if (g_state.app_mode == WATCH_MODE_SET_TIME) {
        if (g_state.time_mode == TIME_MODE_DIURNAL || g_state.time_mode == TIME_MODE_SEMIDIURNAL) {
            g_state.set_time_field = (g_state.set_time_field + 1) % 3; // Group 1, Group 2, Group 3
        } else {
            g_state.set_time_field = (g_state.set_time_field + 1) % 6;
        }
        play_beep(button_beep_tune);
    }
}

static void advance_alarm_value(void) {
    if (g_state.alarm_setting_field == 0) {
        g_state.alarm_hour = (g_state.alarm_hour + 1) % 24;
    } else {
        g_state.alarm_minute = (g_state.alarm_minute + 1) % 60;
    }
}

static void advance_set_time_value(void) {
    if (g_state.time_mode == TIME_MODE_DIURNAL || g_state.time_mode == TIME_MODE_SEMIDIURNAL) {
        uint32_t semidiurnal_adj = (g_state.time_mode == TIME_MODE_SEMIDIURNAL) ? 2 : 1;
        uint32_t step = 0;
        if (g_state.set_time_field == 0) step = dig1_sec / semidiurnal_adj;       // Group 1: 2 hours (or 1 hour in semidiurnal)
        else if (g_state.set_time_field == 1) step = dig2_sec / semidiurnal_adj;  // Group 2: 10 mins (or 5 mins in semidiurnal)
        else if (g_state.set_time_field == 2) step = (uint32_t)(dig4_sec / semidiurnal_adj + 0.5); // Group 3: 4 sec (or 2 sec)

        uint32_t cur_sec = (((uint32_t)g_state.setting_dt.unit.hour * 60) + g_state.setting_dt.unit.minute) * 60 + g_state.setting_dt.unit.second;
        cur_sec = (cur_sec + step) % (24 * 3600);
        g_state.setting_dt.unit.hour = cur_sec / 3600;
        g_state.setting_dt.unit.minute = (cur_sec % 3600) / 60;
        g_state.setting_dt.unit.second = cur_sec % 60;
        return;
    }

    switch (g_state.set_time_field) {
        case 0: g_state.setting_dt.unit.second = 0; break;
        case 1: g_state.setting_dt.unit.hour = (g_state.setting_dt.unit.hour + 1) % 24; break;
        case 2: g_state.setting_dt.unit.minute = (g_state.setting_dt.unit.minute + 1) % 60; break;
        case 3: g_state.setting_dt.unit.year = (g_state.setting_dt.unit.year + 1) % 64; break;
        case 4: g_state.setting_dt.unit.month = (g_state.setting_dt.unit.month % 12) + 1; break;
        case 5: g_state.setting_dt.unit.day = (g_state.setting_dt.unit.day % watch_utility_days_in_month(g_state.setting_dt.unit.month, g_state.setting_dt.unit.year + WATCH_RTC_REFERENCE_YEAR)) + 1; break;
        default: break;
    }
}

static void handle_alarm_button_press(void) {
    if (g_state.app_mode == WATCH_MODE_CLOCK) {
        // Do not immediately mutate time_mode on press down; time_mode toggles on short release
        return;
    }

    g_state.quick_return_to_clock = true;

    if (g_state.app_mode == WATCH_MODE_ALARM) {
        if (g_state.alarm_setting_active) {
            advance_alarm_value();
        } else {
            // Toggle Alarm / Chime states
            if (!g_state.alarm_enabled && !g_state.chime_enabled) {
                g_state.chime_enabled = true;
            } else if (!g_state.alarm_enabled && g_state.chime_enabled) {
                g_state.alarm_enabled = true;
                g_state.chime_enabled = false;
            } else if (g_state.alarm_enabled && !g_state.chime_enabled) {
                g_state.chime_enabled = true;
            } else {
                g_state.alarm_enabled = false;
                g_state.chime_enabled = false;
            }
            play_beep(button_beep_tune);
        }
    } else if (g_state.app_mode == WATCH_MODE_STOPWATCH) {
        g_state.sw_running = !g_state.sw_running;
        play_beep(button_beep_tune);
    } else if (g_state.app_mode == WATCH_MODE_SET_TIME) {
        advance_set_time_value();
    }
}

// --- Interrupt Handlers & App Lifecycle ---

static void cb_mode_pin(void) {
    if (HAL_GPIO_BTN_MODE_read()) {
        handle_mode_button_press();
    }
}

static void cb_light_pin(void) {
    if (HAL_GPIO_BTN_LIGHT_read()) {
        watch_set_led_green();
        handle_light_button_press();
    } else {
        watch_set_led_off();
    }
}

static void cb_alarm_pin(void) {
    if (HAL_GPIO_BTN_ALARM_read()) {
        g_state.alarm_btn_down = true;
        g_state.alarm_btn_down_ticks = g_state.rtc_tick_counter;
        handle_alarm_button_press();
    } else {
        bool was_long_press = (g_state.rtc_tick_counter - g_state.alarm_btn_down_ticks) >= LONG_PRESS_TICKS;
        g_state.alarm_btn_down = false;
        if (g_state.app_mode == WATCH_MODE_CLOCK) {
            // Pressing ALARM button in Clock mode cycles time display mode on short release
            if (!was_long_press) {
                g_state.time_mode = (g_state.time_mode + 1) % TIME_MODE_NUM;
                play_beep(button_beep_tune);
            }
        }
    }
}

static void cb_tick(void) {
    g_state.rtc_tick_counter++;

    if (g_state.rtc_tick_counter % 8 == 0) {
        g_state.blink_state = !g_state.blink_state;
    }

    if (g_state.sw_running) {
        g_state.sw_elapsed_ticks += 1;
    }

    // Check ALARM button hold
    if (g_state.alarm_btn_down) {
        uint32_t held_ticks = g_state.rtc_tick_counter - g_state.alarm_btn_down_ticks;
        if ((g_state.app_mode == WATCH_MODE_ALARM && g_state.alarm_setting_active) || g_state.app_mode == WATCH_MODE_SET_TIME) {
            if (held_ticks >= HOLD_REPEAT_DELAY_TICKS && (held_ticks % HOLD_REPEAT_RATE_TICKS == 0)) {
                if (g_state.app_mode == WATCH_MODE_ALARM) advance_alarm_value();
                else advance_set_time_value();
            }
        }
    }

    // Check Alarm & Hourly Chime Triggers (ensure single trigger per second 0)
    watch_date_time_t dt = watch_rtc_get_date_time();

    if (dt.unit.second == 0 && (g_state.rtc_tick_counter % g_state.current_tick_freq == 0)) {
        if (g_state.alarm_enabled && dt.unit.minute != g_state.last_alarm_triggered_minute && dt.unit.hour == g_state.alarm_hour && dt.unit.minute == g_state.alarm_minute) {
            g_state.last_alarm_triggered_minute = dt.unit.minute;
            play_beep(alarm_tune);
        }

        if (g_state.chime_enabled && dt.unit.minute == 0 && dt.unit.hour != g_state.last_chime_triggered_hour) {
            g_state.last_chime_triggered_hour = dt.unit.hour;
            play_beep(hourly_chime_tune);
        }
    }
}

void app_init(void) {
    _watch_init();
}

void app_setup(void) {
    watch_enable_display();

    g_state.app_mode = WATCH_MODE_CLOCK;
    g_state.time_mode = TIME_MODE_12H;
    g_state.current_tick_freq = 16;
    g_state.last_alarm_triggered_minute = -1;
    g_state.last_chime_triggered_hour = -1;
    g_state.alarm_setting_active = false;
    g_state.alarm_setting_field = 0;
    g_state.set_time_field = 0;
    g_state.blink_state = false;

    watch_enable_external_interrupts();
    watch_register_interrupt_callback(HAL_GPIO_BTN_MODE_pin(), cb_mode_pin, INTERRUPT_TRIGGER_BOTH);
    watch_register_interrupt_callback(HAL_GPIO_BTN_LIGHT_pin(), cb_light_pin, INTERRUPT_TRIGGER_BOTH);
    watch_register_interrupt_callback(HAL_GPIO_BTN_ALARM_pin(), cb_alarm_pin, INTERRUPT_TRIGGER_BOTH);

    watch_rtc_register_periodic_callback(cb_tick, g_state.current_tick_freq);
}

bool app_loop(void) {
    render_app();

    // SAML22 CPU enters STANDBY low-power mode until next RTC/EIC interrupt
    return true;
}
