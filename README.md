# Mega Trackball

**Sega Mega Mouse emulator** running on an **Arduino Nano**, allowing a quadrature arcade trackball to be used as a pointing device on the Sega Mega Drive / Genesis.

To my knowledge, this is the first open-source project documenting full Mega Mouse emulation from a 16 MHz AVR microcontroller, including the workaround for SGDK's port classification.

## Overview

| Component | Role |
|---|---|
| Arcade trackball (3-inch, ~25€) | Motion source, quadrature output |
| Arduino Nano (ATmega168/328P) | Quadrature decoding + Mega Mouse emulation |
| Sacrificed official MD pad | DB-9 cable to the console |
| Custom SGDK ROM | Manual 3-line handshake reading |

## Hardware required

- Arduino Nano (CH340 clone OK, ATmega168PA-AU or ATmega328P, 16 MHz)
- JAMMA-style arcade trackball with 5V quadrature output (4 axis wires + power)
- Official Sega Mega Drive 3 or 6 button controller (sacrificed for the DB-9 cable)
- Soldering iron, wires, prototyping board

## Software required

- **Arduino IDE** (1.8+) or compatible
- **SGDK** 1.80+ for the ROM
- **Java** (for SGDK's `rescomp`)

## Documentation

- [`docs/WIRING.md`](docs/WIRING.md) — wiring diagrams Arduino ↔ DB-9 ↔ trackball
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — technical notes on the Mega Mouse protocol and pitfalls

## Repository contents

```
mega-trackball/
├── README.md                  This file
├── arduino/
│   └── trackball_mouse.ino    Arduino sketch: Mega Mouse emulation + trackball reading
├── sgdk/
│   ├── src/
│   │   └── main.c             SGDK ROM: manual bypass + sprite cursor
│   └── res/
│       └── resources.res      Sprite resources (cursor.png to be provided)
└── docs/
    ├── WIRING.md
    └── PROTOCOL.md
```

## Quick start

### 1. Wiring

See [`docs/WIRING.md`](docs/WIRING.md) for full details. Summary:

- DB-9 → Arduino: TH→D2, TR→D3, TL→D4, D0–D3→D5–D8, GND→GND
- Trackball → Arduino: axis 1 on D9/D10, axis 2 on D11/D12, +5V and GND

### 2. Arduino upload

Open `arduino/trackball_mouse.ino` in the Arduino IDE, select your board (Nano, ATmega168 or ATmega328P), and upload.

### 3. SGDK ROM build

```bash
cd sgdk
make
```

The ROM will be at `out/rom.bin`. Load it on your Everdrive or flashcart.

### 4. Console testing

1. Plug the Arduino (via DB-9) into **port 2** of the console
2. Run the ROM
3. Move the trackball, the cursor should follow

> **Important note**: due to the Arduino bootloader's startup time (~500 ms to 1 s), you need to either
> - Cold reboot the console **after** the Arduino has finished booting, or
> - Flash the Arduino without bootloader using an ISP programmer (USBasp ~5€), or
> - Add an RC capacitor on the Nano's RESET pin to force a reset at power-up.

## Implementation choices

### Why bypass SGDK?

`JOY_init()` classifies the port via the SEGA 4-bit identification test. Due to tight timing (12 µs bursts), an imperfect read can result in `PORT_TYPE_TEAMPLAYER` (0x07) instead of `PORT_TYPE_MOUSE` (0x03). Once that classification is set, SGDK refuses mouse-mode reads even with `JOY_setSupport(JOY_SUPPORT_MOUSE)`.

The ROM contains a `readMousePacket()` function that directly accesses the I/O registers (`0xA10005`, `0xA1000B`) and runs the handshake sequence itself, independently of the SGDK classification.

### Why port 2?

Port 1 is often used by linkers / Everdrive carts for menu navigation. The code can be trivially adapted to port 1 by changing the I/O addresses (`0xA10003` / `0xA10009`).

### Anti-glitch

The console's identification bursts and the AVR's 16 MHz timing occasionally cause a corrupted mouse read, resulting in a sudden cursor jump. The ROM filters out deltas exceeding `MAX_DELTA = 30` pixels per frame.

## Known limitations

- **Arduino boot delay**: if the Arduino is not ready when the console runs its first ID test, SGDK classification fails. The bypass works around this for reading, but requires the custom ROM.
- **Precision**: a typical arcade trackball has 200–300 pulses per turn — enough for a smooth 60 Hz cursor but not comparable to a modern optical mouse.
- **No multi-port support**: one trackball at a time.

## Credits & resources

- [Plutiedev](https://www.plutiedev.com/) — Mega Mouse protocol documentation (the reference!)
- [Eke-Eke](http://gendev.spritesmind.net/) — Lightgun docs and SEGA identification formula
- [SGDK](https://github.com/Stephane-D/SGDK) — Stephane Dallongeville
- [HotPixelChannel/Mouse-To-SEGA-MD](https://github.com/HotPixelChannel/Mouse-To-SEGA-MD) — neighboring project (gamepad emulation instead of mouse)

## License

MIT — see `LICENSE`.
