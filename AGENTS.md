# Agent Instructions

## Project Aim

This project aims to reproduce the original Casio F-91W firmware, including
the same clock faces. In addition to AM/PM, it provides two additional time
formats: Dozenal and Semidiurnal.

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
