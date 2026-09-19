Dozenal Watch (Direct Gossamer App)
==================================

[![Latest Simulator](https://img.shields.io/badge/Simulator-App%20Rewrite%20Firmware-2ea44f)](https://flufflefam.github.io/dozenal-movement/simulator/firmware.html)

A direct, low-power standalone application implementation on top of the [Gossamer](https://github.com/joeycastillo/gossamer) hardware abstraction layer for the [Sensor Watch extension board](https://www.sensorwatch.net/), matching the exact functional behavior and navigation of a Casio F-91W with [dozenal and semidiurnal timekeeping modes](https://clocks.dozenal.ca/).

Firmware build:
```sh
make BOARD=sensorwatch_pro DISPLAY=custom
```

Emulator build:
```sh
emmake make BOARD=sensorwatch_red DISPLAY=classic
python3 -m http.server -d build-sim
```

Power & Battery Consumption Analysis
------------------------------------
By completely discarding the heavy Movement framework, filesystem drivers, shell tasks, and multi-face overhead, this direct Gossamer app minimizes CPU wake cycles and active background power consumption.

### Microcontroller & Peripheral Operating Parameters (SAML22)
* **Supply Voltage**: 3.0 V (CR2016 Lithium Coin Cell, Nominal Capacity: **90 mAh** / 90,000 µAh)
* **STANDBY Sleep Mode Current ($I_{sleep}$)**: ~3.5 µA (SAML22 RTC running @ 32.768 kHz + SLCD driver active)
* **CPU Active Mode Current ($I_{active}$)**: ~120 µA/MHz (CPU running at 4 MHz $\approx 480$ µA during active interrupt processing)
* **Active Wake Duration per Tick**: $\approx 0.5$ ms per interrupt tick

### Power Analysis by Mode

1. **Standard 12H / 24H Clock Mode (1 Hz Tick Rate)**:
   - **Sleep current**: $3.5\text{ }\mu\text{A}$
   - **Active current contribution**: $480\text{ }\mu\text{A} \times \frac{0.5\text{ ms}}{1000\text{ ms}} = 0.24\text{ }\mu\text{A}$
   - **Total Average Current ($I_{avg}$)**: $\approx 3.74\text{ }\mu\text{A}$
   - **Estimated Battery Life**:
     $$\text{Battery Life} = \frac{90,000\text{ }\mu\text{Ah}}{3.74\text{ }\mu\text{A}} \approx 24,064\text{ hours} \approx \mathbf{2.74\text{ years}}$$

2. **Dozenal / Semidiurnal Active Display Mode (16 Hz Tick Rate)**:
   - **Sleep current**: $3.5\text{ }\mu\text{A}$
   - **Active current contribution**: $16 \times \left(480\text{ }\mu\text{A} \times \frac{0.5\text{ ms}}{1000\text{ ms}}\right) = 3.84\text{ }\mu\text{A}$
   - **Total Average Current ($I_{avg}$)**: $\approx 7.34\text{ }\mu\text{A}$
   - **Estimated Battery Life**:
     $$\text{Battery Life} = \frac{90,000\text{ }\mu\text{Ah}}{7.34\text{ }\mu\text{A}} \approx 12,261\text{ hours} \approx \mathbf{1.40\text{ years}}$$

3. **Stopwatch Active Mode / Fast Tick (100 Hz Tick Rate)**:
   - **Active current contribution**: $100 \times \left(480\text{ }\mu\text{A} \times \frac{0.5\text{ ms}}{1000\text{ ms}}\right) = 24\text{ }\mu\text{A}$
   - **Total Average Current ($I_{avg}$)**: $\approx 27.5\text{ }\mu\text{A}$
