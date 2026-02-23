# Arduino Bird Light Controller — v2

A personal home automation project — a programmable LED light that simulates natural daylight cycles for a pet bird, rebuilt with a cleaner interface and powered by the [LightScheduler](https://github.com/IAMTorres/LightScheduler) library.

> **Origin:** A family member's bird needed consistent light exposure to stay healthy. Commercial timers didn't have a gradual sunset effect. So I built one.
> See the original project: [arduino-bird-light](https://github.com/IAMTorres/arduino-bird-light)

## What's new in v2

| | v1 (original) | v2 (this) |
|--|---------------|-----------|
| Idle screen | Menu text only | Always shows ON/OFF schedule + current time + status |
| Time config | Increment minute by minute (slow) | Hour and minute set separately |
| Button feel | Hold = nothing | Hold BTN1 = fast increment (accelerates) |
| Menu structure | Nested while loops | Clean state machine |
| Scheduling core | Custom inline code | [LightScheduler](https://github.com/IAMTorres/LightScheduler) library |

## Idle Screen

No button press needed — the screen always shows everything:

```
08:00->22:00   🐦
14:35:22   ON
```

Line 1: configured ON time → OFF time + bird icon
Line 2: current time + light status (`ON` / `OFF` / `47%` during dimming)

## Button Flow

```
IDLE
  ├── BTN1 short ──→ Set clock
  │                    BTN1 = increment  │  BTN2 = confirm → next field
  │                    [hour] → [minute] → back to idle
  │
  └── BTN2 short ──→ Set schedule
                       BTN1 = increment  │  BTN2 = confirm → next field
                       [ON hour] → [ON min] → [OFF hour] → [OFF min] → save → idle

Hold BTN1       → fast increment (accelerates after 800 ms)
No input 8 s    → auto-return to idle, unsaved changes discarded
```

## Light Behaviour

```
ON time              OFF time         OFF time + 60 min
  │                     │                    │
  ├── PWM 255 ───────────┤── dim 255 → 0 ─────┤── OFF (until next day)
```

At the OFF time, [LightScheduler](https://github.com/IAMTorres/LightScheduler) begins a smooth 60-minute linear fade, simulating a natural sunset. The light only dims if it was actually on during that cycle — no false triggers on boot.

## Hardware

| Component | Purpose |
|-----------|---------|
| Arduino Uno | Main controller |
| DS1302 RTC module | Real-time clock (battery-backed) |
| 16x2 LCD (I2C) | Status display and menu |
| 2x push buttons | Navigation and value input |
| L298N motor driver | PWM brightness control |
| External 12V supply | Powers the LED light |
| LED lamp | The actual bird light |

## Wiring

```
  ┌──────────┐       ┌─────────────────────────┐       ┌─────────────┐
  │  DS1302  │       │       Arduino Uno        │       │  LCD 16x2   │
  │  (RTC)   ├─D5───→ RST               SDA ───┼──────→│   (I2C)     │
  │          ├─D6───→ DAT               SCL ───┼──────→│             │
  │          ├─D7───→ CLK                      │       └─────────────┘
  └──────────┘       │                         │
                     │                D11 ─────┼──→ [Button 1]
                     │                D12 ─────┼──→ [Button 2]
                     │                         │
                     │          D2 (IN1) ──────┼──┐
                     │          D3 (IN2) ──────┼──┤    ┌──────────────────┐
                     │          D9 (ENA) ──────┼──┴───→│     L298N        │
                     └─────────────────────────┘       │                  │
                                               12V ───→│ VS          OUT+ ├──→ LED (+)
                                               GND ───→│ GND         OUT- ├──→ LED (-)
                                                        └──────────────────┘
```

## Libraries Required

Install all via Arduino IDE → `Sketch → Include Library → Manage Libraries`:

| Library | Install as |
|---------|-----------|
| [LightScheduler](https://github.com/IAMTorres/LightScheduler) | `LightScheduler` |
| [DS1302 by Makuna](https://github.com/Makuna/Rtc) | `Rtc by Makuna` |
| [LiquidCrystal_I2C](https://github.com/johnrickman/LiquidCrystal_I2C) | `LiquidCrystal I2C` |

`Wire.h` and `EEPROM.h` are built-in (no install needed).

## How to Use

1. Install the libraries above
2. Open `bird_light_v2/bird_light_v2.ino` in Arduino IDE
3. Upload to Arduino Uno
4. On first boot: press **BTN1** to set the clock, then **BTN2** to set the ON/OFF schedule
5. Settings are saved to EEPROM — no need to reconfigure after power cuts

## Author

**Gonçalo Torres** — [github.com/IAMTorres](https://github.com/IAMTorres)

## License

MIT
