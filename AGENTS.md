# Agent Instructions

Unless explicitly specified otherwise, all instructions and requirements refer to the Direct Gossamer App (`app.c`).

## Environment Setup

Before building or running firmware checks, verify that the GNU Arm Embedded
Toolchain is installed and that all git submodules are initialized. If the
toolchain is missing on Debian or Ubuntu, install it with:

```sh
apt install gcc-arm-none-eabi
```

If submodules are missing or uninitialized, fetch them with:

```sh
git submodule update --init --recursive
```

## Project Aim

This project aims to reproduce the original Casio F-91W firmware, including
the same clock faces. In addition to AM/PM, it provides two additional time
formats: diurnal and semidiurnal (collectively referred to as dozenal time).

The official Casio F-91W user manual (Module 593) is included in this repository as [`MANUAL.md`](MANUAL.md), which serves as the authoritative source on how the gossamer app should behave.

Unless specified otherwise, scope changes only to the four active clock faces that are enabled and part of the standard Casio F-91W firmware rotation (`clock_face`, `alarm_face`, `fast_stopwatch_face`, and `set_time_face`).

## Original Firmware Parity

Match the original F-91W's capabilities as well as its appearance. For
example, the original alarm can be set by hour and minute, but not at a
sub-minute resolution. Therefore, the dozenal alarm face must not invent a
digit-4 alarm setting or display digit 4 as an alarm value merely because the
main clock face displays it. Preserve the original alarm's supported precision
when adapting its controls to other time modes.

The set-time face is another firmware-parity surface. In diurnal and semidiurnal modes (dozenal time), group the
editable digits the same way as the alarm face: digit 1 as the first group (including the
semidiurnal leading half-day digit), digits 2 and 3 together as the second group, and digit 4 as
the third group. The selected group must blink and advance together; do not expose the decimal
hour, minute, and second fields as independent controls in these modes. The RTC stores whole
seconds, so digit-4 setting uses the nearest whole-second increment rather than inventing
fractional persistence.

## Diurnal Time Reference

Use the following diurnal timekeeping definitions when working on time displays
or time calculations in this repository. They are taken from [The Dozenal
Watch](https://clocks.dozenal.ca/pdf/watch.pdf).

In the source document, a subscript `z` indicates the dozenal base, and `d`
indicates the decimal base.

The time is diurnal. The day is divided by successive powers of a dozen. The
first digit changes every 2 hours. The entire set of digits is the following:

| Digit | Frequency of change |
| --- | --- |
| 1 | 2 hours |
| 2 | 10d minutes |
| 3 | 50d seconds |
| 4 | 4 1/6 seconds |
| 5 | 25d/72d second |

The day begins at midnight, time `000.0(0)`.

On the SensorWatch display, dozenal digit ten is represented by the glyph `2`
with its top horizontal segment removed. It is rendered directly through the
LCD segment map rather than as ordinary text. dozenal digit eleven is displayed
as `E`.

## Semidiurnal Time

In this codebase, semidiurnal time is the diurnal time display compressed into
two equal half-day cycles. Every diurnal digit changes twice as often: each
diurnal interval is divided by 2. A leading digit, `0` or `1`, identifies which
half of the day is being displayed, followed by the regular diurnal digits.

This definition is inferred from the standard clock implementation: the
semidiurnal mode applies a factor of 2 to the diurnal digit frequencies and
displays the leading half-day digit, while the ordinary AM/PM mode remains a
separate 12-hour display mode.

## Time Mode Consistency

The time mode selected on the main clock face is authoritative on every screen.
Any screen that shows or sets the current time or an alarm must use the same
mode and its native increments:

- `12H`: 12-hour time with an AM/PM indicator.
- `24H`: 24-hour time.
- `DIURNAL`: the diurnal digit frequencies defined above.
- `SEMIDIURNAL`: the diurnal digit frequencies divided by 2, with the leading
	half-day digit defined above.

Do not display or advance time or alarm fields using fixed decimal hour and
minute increments when the selected mode is dozenal (diurnal or semidiurnal). Preserve
the selected mode while entering, editing, displaying, and confirming values.
Setting or repeating a value always advances it by one unit in the current
display mode. In `12H` and `24H`, one hour is 3600 seconds and one minute is
60 seconds. In `DIURNAL`, one displayed hour is two decimal hours and the
alarm's minute group advances by 50 decimal seconds. In `SEMIDIURNAL`, those
diurnal intervals are divided by two. The two-hour diurnal hour interval is
therefore the conversion for one displayed unit, not a request to skip two
displayed units.

Date components shown by active faces use the same display mode. In dozenal modes
(diurnal and semidiurnal), convert numeric date components independently to base
12 using the existing time glyphs: ten is the `2` glyph with its top horizontal
segment removed, and eleven is displayed as `E`. For example, decimal
`2026-01-10` is displayed with the equivalent glyphs for `120` plus dozenal
ten, followed by `-01-0` plus dozenal ten. Weekday names remain unchanged
across all time modes. Faces without a date component do not need to add one
solely for this conversion.

## Original Button Navigation

On the alarm and stopwatch faces, pressing either the upper-left LIGHT button or
the lower-right ALARM button, followed by the lower-left MODE button, returns to
the main clock face. Preserve this original Casio navigation behavior when
changing those faces.

On all four screens (clock, alarm and stopwatch set time), pressing down the 
upper-left LIGHT button turns the green backlight on (`watch_set_led_green()`), and releasing (de-pressing) it
turns the backlight off immediately (`watch_set_led_off()`) without delay, matching original
Casio F-91W hardware behavior.

On the alarm face, the LIGHT button enters and advances alarm-time setting,
while the lower-right ALARM button adjusts the selected value during setting
and cycles through alarm off, hourly chime, alarm, and both in normal mode.
Holding the ALARM button while setting the alarm starts automatically advancing
the selected value after the normal long-press delay, and releasing it stops
the repeat.
Alarm-time hold cycling must match the set-time face exactly: use the same
repeat frequency, advance once per repeat tick while held, and stop repeating
only when the button is released.
The main clock face must not use an ALARM long press to toggle the hourly chime.
On the main clock face, pressing or releasing ALARM cycles through all available
time modes, and long-pressing ALARM does not display the year or date.

## Stopwatch Face

Dozenal movement uses `fast_stopwatch_face` for the stopwatch unless the
configuration explicitly selects a different stopwatch face. Apply stopwatch
behavior changes to the active face accordingly.

## User Interface Discrepancies (Original Casio F-91W vs. Current Firmware)

The following list documents discrepancies between an unmodified Casio F-91W user interface and the current firmware implementation:

### 1. Main Timekeeping Face & Time Modes
- **Time Modes:** Unmodified F-91W only supports standard 12-hour and 24-hour time modes. Current firmware adds `DIURNAL` (diurnal base-12 time) and `SEMIDIURNAL` time modes (collectively dozenal time).
- **ALARM Button Function on Main Clock:**
  - *Original F-91W:* Pressing ALARM toggles between 12-hour and 24-hour display modes (showing a "24H" indicator in 24H mode). Holding ALARM sounds the alarm buzzer for testing.
  - *Current UI:* Pressing or releasing ALARM cycles through all available time modes (`12H` -> `24H` -> `DIURNAL` -> `SEMIDIURNAL`). Holding ALARM does not display the year or date.
- **Date Display:** Unmodified F-91W displays day-of-week abbreviation and day-of-month (e.g. `SU  25`) at top-right. Current UI displays full date (including 4-digit year, month, day, or dozenal equivalents) depending on active mode and interactions.

### 2. Time Setting Face (`set_time_face`)
- **Access & Mode Structure:**
  - *Original F-91W:* Time setting is entered by pressing MODE 3 times from main clock (seconds flash immediately upon entry).
  - *Current UI:* Time setting is a standalone watch face (`set_time_face`) in face rotation/secondary menu with titled pages ("Year", "Month", "Day", "Z" Time Zone, "Hour", "Minut", "Secnd").
- **Editable Fields:**
  - *Original F-91W:* Editable fields sequence: Seconds -> Hours -> Minutes -> Month -> Date -> Day of Week.
  - *Current UI:* Includes Year setting and Time Zone ("Z") selection. Does not have a separate Day of Week edit step (calculated automatically).
- **Seconds Reset Behavior:**
  - *Original F-91W:* Pressing ALARM while seconds are blinking resets seconds to 00. If seconds were 30–59, minutes increment by 1.
  - *Current UI:* Pressing ALARM in decimal modes resets seconds to 0 without incrementing minutes, or advances by digit-4 increments in dozenal modes (diurnal/semidiurnal).

### 3. Alarm Face (`alarm_face`)
- **Alarm Sound Test vs Hold Repeat:**
  - *Original F-91W:* Holding ALARM while viewing alarm time plays the alarm buzzer as a test.
  - *Current UI:* Holding ALARM in normal view does not sound a test buzzer. In setting mode, holding ALARM continuously advances the active digit group after a long-press delay.
- **Setting Mode Entry:**
  - *Original F-91W:* Pressing LIGHT in Alarm view immediately enters hour setting (hours flash).
  - *Current UI:* Pressing LIGHT or long-pressing LIGHT enters setting mode.

### 4. Stopwatch Face (`fast_stopwatch_face` / `stopwatch_face`)
- **Rollover & Range:**
  - *Original F-91W:* Stopwatch counts up to 59:59.99 (59 minutes, 59.99 seconds) and rolls over to 00:00.00.
  - *Current UI:* Tracks elapsed time up to 24 hours, displaying hours in the top right corner when hours > 0.
- **LIGHT Button Long Press:**
  - *Original F-91W:* LIGHT button only illuminates LCD backlight (or handles lap/reset).
  - *Current UI:* Long pressing LIGHT button toggles slow refresh rate mode and LED behavior on button press.

### 5. Navigation & Secondary Menus
- **Mode Cycle & Navigation:**
  - *Original F-91W:* Fixed mode sequence (Timekeeping -> Alarm -> Stopwatch -> Time Setting -> Timekeeping).
  - *Current UI:* Configurable list of watch faces via `watch_faces[]` in `movement_config.h`, with optional secondary face menu on long MODE press (`MOVEMENT_SECONDARY_FACE_INDEX`). Retains original Casio shortcut (pressing LIGHT/ALARM then MODE returns to main clock) on Alarm and Stopwatch faces.

## Original Firmware Documentation

Whenever a patch matches or restores behavior from the original Casio firmware,
document that behavior in `AGENTS.md` as part of the same change.

## Reference Material: Casio F-91W User Manual (Module 593)

**Operation Guide 10B+7L (Ver.C)**

---

### Button Identification

* **Light button (L)**: Upper-left button (Light / Set / Select)
* **Mode button (C)**: Lower-left button (Change Mode)
* **Alarm / 12/24-hour button (A)**: Lower-right button (Start / Stop / Adjust / Toggle 12/24-hour)

---

### Reading the Display

#### Day of Week Abbreviations
`SU`: Sunday, `MO`: Monday, `TU`: Tuesday, `WE`: Wednesday, `TH`: Thursday, `FR`: Friday, `SA`: Saturday

#### Regular Timekeeping Mode
* Displays: Day of week, Date, Hour, Minute, Second, PM indicator (in 12-hour format).
* **Changeover between 12-hour and 24-hour formats**: In regular timekeeping mode, with every push of **A**, the display is switched between 12-hour and 24-hour formats.
* **Easter Egg (Authenticity Test / ALARM Long-Press)**: In regular timekeeping mode, pressing and holding the **A** (Alarm / 12/24-hour) button for approximately 3 seconds displays "CASIo" across the LCD screen. Releasing the **A** button returns to the regular timekeeping display.

#### Mode Cycling
From Regular timekeeping mode:
* Press **C**: Daily alarm mode
* Press **C** again: Stopwatch mode
* Press **C** again: Time/calendar setting mode
* Press **C** again: Return to Regular timekeeping mode

---

### Setting Daily Alarm

If the daily alarm is set, the buzzer sounds for 20 seconds at the preset time every day until cleared. To stop the buzzer, press the **L** button. If the time signal is set, the watch sounds every hour on the hour.

* **Sound demonstration**: Press and hold the **A** button in alarm mode to sound the buzzer.
* *If the **C** button is pressed, the watch reverts to the regular timekeeping mode.*
* **Quick digit advance**: When the **A** button is pressed for more than 2 seconds, the digit advances quickly. When released, the digit advance will stop.

#### Alarm Time Setting
1. In daily alarm mode, press **L** to set hour digits. One hour advances with every push of **A**.
2. Press **L** to set minute digits. One minute advances with every push of **A**.
3. Press **L** to complete.*

#### On or Off Setting of Daily Alarm and Time Signal
In daily alarm mode, press **A** to cycle through the alarm and time signal states:
1. **Both Alarm-on-mark and Time-signal-on-mark appear**: The daily alarm and time signal sound.
2. **Both Alarm-on-mark and Time-signal-on-mark disappear**: The daily alarm and time signal do not sound.
3. **Alarm-on-mark only appears**: The daily alarm only sounds.
4. **Time-signal-on-mark only appears**: The time signal only sounds.

---

### Stopwatch Operation

A signal confirms start/stop operation.

* **Working range**: The stopwatch display is limited to 59 minutes 59.99 seconds. For longer times, reset and start again.

#### (a) Net Time Measurement
* **A** (Start) $\rightarrow$ **A** (Stop) $\rightarrow$ **A** (Re-start) $\rightarrow$ **A** (Stop) $\rightarrow$ **L** (Reset)

#### (b) Split Time Measurement
* **A** (Start) $\rightarrow$ **L** (Split) $\rightarrow$ **L** (Split release) $\rightarrow$ **A** (Stop) $\rightarrow$ **L** (Reset)

#### (c) 1st-2nd Place Times
* **A** (Start) $\rightarrow$ **L** (Split - The first runner finishes. Record the time of the first runner.) $\rightarrow$ **A** (Stop - The second runner finishes.) $\rightarrow$ **L** (Split release - Record the time of the second runner.) $\rightarrow$ **L** (Reset)

---

### Setting Time and Calendar

**IMPORTANT**: Setting sequence MUST BE FOLLOWED when making any new setting.

1. From regular timekeeping mode, press **C** three times to change to time/calendar setting mode (seconds start flashing).
2. **Second adjusting**: Press **A** to adjust seconds. (Precise time can be maintained by correcting the seconds once a month on a time signal from a radio, TV, telephone, etc.)
3. **Hour setting**: Press **L** to move to the hour setting. Press **A** to increase the number.
4. **Minute setting**: Press **L** to move to the minute setting. Press **A** to increase the number.
5. **Month setting**: Press **L** to move to the month setting. Press **A** to increase the number.
6. **Date setting**: Press **L** to move to the date setting. Press **A** to increase the number.
7. **Day setting**: Press **L** to move to the day setting. Pressing **A** advances to the next day.
8. Press **C** to complete.

#### Additional Notes
* **Correction skipping**: Correction of any digit, if not required, can be skipped by pressing the **L** button repeatedly.
* **Auto-return**: If you do not operate any button for a few minutes while a selection is flashing, the flashing stops and the watch goes back to the regular timekeeping mode automatically.
* **Quick digit advance**: When the **A** button is pressed for more than 2 seconds, the digit advances quickly. When released, the digit advance will stop.

---

### Specifications

* **Accuracy at normal temperature**: ±30 seconds a month
* **Display capacity**:
  * **Regular timekeeping mode**: Hour, minute, second, am/pm, date and day
  * **Calendar system**: Auto-calendar set at 28 days for February
  * **Daily alarm mode**
  * **Hourly time signal function**
  * **Stopwatch mode**:
    * **Measuring capacity**: 59 minutes 59.99 seconds
    * **Measuring unit**: 1/100th of a second
    * **Measuring modes**: Normal time, net time, split time and 1st-2nd place times
* **Battery**:
  * One lithium battery (type: CR-2016 –– except Module 1271/BR-2020 –– Module 1271 only)
  * Approx. 7 years operation on type CR-2016 (except Module 1271) / 10 years on type BR-2020 (Module 1271 only). [assuming alarm operation for 20 sec./day and one light operation for 1 sec./day]

*NOTE: THERE IS NO WAY unit components can be damaged or malfunction, due to misoperation of buttons. If confusing information appears on the display it means entry sequence was incorrect. Please read the manual and try again.*
