# Agent Instructions

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
formats: Dozenal and Semidiurnal.

## Original Firmware Parity

Match the original F-91W's capabilities as well as its appearance. For
example, the original alarm can be set by hour and minute, but not at a
sub-minute resolution. Therefore, the Dozenal alarm face must not invent a
digit-4 alarm setting or display digit 4 as an alarm value merely because the
main clock face displays it. Preserve the original alarm's supported precision
when adapting its controls to other time modes.

The set-time face is another firmware-parity surface. In Dozenal and Semidiurnal modes, group the
editable digits the same way as the alarm face: digit 1 as the first group (including the
Semidiurnal leading half-day digit), digits 2 and 3 together as the second group, and digit 4 as
the third group. The selected group must blink and advance together; do not expose the decimal
hour, minute, and second fields as independent controls in these modes. The RTC stores whole
seconds, so digit-4 setting uses the nearest whole-second increment rather than inventing
fractional persistence.

## Dozenal Time Reference

Use the following dozenal timekeeping definitions when working on time displays
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

On the SensorWatch display, Dozenal digit ten is represented by the glyph `2`
with its top horizontal segment removed. It is rendered directly through the
LCD segment map rather than as ordinary text. Dozenal digit eleven is displayed
as `E`.

## Semidiurnal Time

In this codebase, Semidiurnal time is the Dozenal time display compressed into
two equal half-day cycles. Every Dozenal digit changes twice as often: each
Dozenal interval is divided by 2. A leading digit, `0` or `1`, identifies which
half of the day is being displayed, followed by the regular Dozenal digits.

This definition is inferred from the standard clock implementation: the
Semidiurnal mode applies a factor of 2 to the Dozenal digit frequencies and
displays the leading half-day digit, while the ordinary AM/PM mode remains a
separate 12-hour display mode.

## Time Mode Consistency

The time mode selected on the main clock face is authoritative on every screen.
Any screen that shows or sets the current time or an alarm must use the same
mode and its native increments:

- `12H`: 12-hour time with an AM/PM indicator.
- `24H`: 24-hour time.
- `DIURNAL`: the Dozenal digit frequencies defined above.
- `SEMIDIURNAL`: the Dozenal digit frequencies divided by 2, with the leading
	half-day digit defined above.

Do not display or advance time or alarm fields using fixed decimal hour and
minute increments when the selected mode is Dozenal or Semidiurnal. Preserve
the selected mode while entering, editing, displaying, and confirming values.
Setting or repeating a value always advances it by one unit in the current
display mode. In `12H` and `24H`, one hour is 3600 seconds and one minute is
60 seconds. In `DIURNAL`, one displayed hour is two decimal hours and the
alarm's minute group advances by 50 decimal seconds. In `SEMIDIURNAL`, those
Dozenal intervals are divided by two. The two-hour Dozenal hour interval is
therefore the conversion for one displayed unit, not a request to skip two
displayed units.

Date components shown by active faces use the same display mode. In Dozenal
and Semidiurnal modes, convert numeric date components independently to base
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

On the alarm and stopwatch screens, pressing the upper-left LIGHT button turns
on the backlight when pressed, in addition to performing its existing mode function.

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
On the main clock face, an ALARM long press temporarily shows the current
year-month-date in the active mode, then returns to the time display after a
few seconds. A short ALARM press continues to cycle time modes.

## Stopwatch Face

Dozenal movement uses `fast_stopwatch_face` for the stopwatch unless the
configuration explicitly selects a different stopwatch face. Apply stopwatch
behavior changes to the active face accordingly.

## Original Firmware Documentation

Whenever a patch matches or restores behavior from the original Casio firmware,
document that behavior in `AGENTS.md` as part of the same change.
