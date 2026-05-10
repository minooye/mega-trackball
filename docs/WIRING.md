# Wiring

## Mega Drive DB-9 pinout (view from the female connector side, the one from the Pad)

```
        ___________
       \ 1 2 3 4 5 /
        \ 6 7 8 9 /
         ---------
```

| DB-9 pin | Signal | Direction (MD side) |
|---|---|---|
| 1 | D0 | input |
| 2 | D1 | input |
| 3 | D2 | input |
| 4 | D3 | input |
| 5 | +5V | output (controller power) |
| 6 | TL | input |
| 7 | TH | output |
| 8 | GND | — |
| 9 | TR | output |

> On a classic controller (PAD), TH and TR are MD-side outputs used to select which half of the buttons to read. On the mouse, they are used for the handshake.

## DB-9 → Arduino Nano wiring

Sacrifice an official Mega Drive 3 or 6-button controller to recover the DB-9 cable. Colors below are from my controller; **verify the pin → color correspondence on your own controller** with a multimeter, as colors vary.

| DB-9 pin | Signal | Color (example) | Arduino Nano pin |
|---|---|---|---|
| 1 | D0 | Brown | D5 |
| 2 | D1 | Red | D6 |
| 3 | D2 | Orange | D7 |
| 4 | D3 | Yellow | D8 |
| 5 | +5V | Green | 5V|
| 6 | TL | Blue | D4 |
| 7 | TH | Grey | **D2** (INT0) |
| 8 | GND | Black | GND |
| 9 | TR | White | **D3** (INT1) |


## Trackball → Arduino Nano wiring

Typical 5V JAMMA-style arcade trackball (3-inch, 4 axis wires + 2 power wires). Colors vary by manufacturer.

| Trackball wire | Role | Arduino Nano pin |
|---|---|---|
| Red | +5V | 5V |
| Black | GND | GND |
| Yellow | Axis 1 phase A | D9 (PB1) |
| Green | Axis 1 phase B | D10 (PB2) |
| Blue | Axis 2 phase A | D11 (PB3) |
| Purple | Axis 2 phase B | D12 (PB4) |

The D9-D12 pins map to **PORTB bits 1-4**, which allows reading them in a single `PINB` access and using PCINT0 to decode transitions without polling.

## Full diagram (logical view)

```
            ┌─────────────────┐
            │   MD Console    │
            │   (port 2)      │
            └────┬─DB-9 Pin───┘
                 │
                 │ 1 (D0)─Brown───┐
                 │ 2 (D1)─Red─────┤
                 │ 3 (D2)─Orange──┤
                 │ 4 (D3)─Yellow──┤
                 │ 5 (+5V)──── (NC)
                 │ 6 (TL)─Blue────┤
                 │ 7 (TH)─Grey────┤
                 │ 8 (GND)─Black──┤
                 │ 9 (TR)─White───┤
                 │                │
                 │     ┌──────────┴────────┐
                 │     │   Arduino Nano    │
                 │     │                   │
                 │     │  D5 ◄─────────────┘ (D0 brown)
                 │     │  D6 ◄────────── (D1 red)
                 │     │  D7 ◄────────── (D2 orange)
                 │     │  D8 ◄────────── (D3 yellow)
                 │     │  D4 ◄────────── (TL blue)
                 │     │  D2 ◄────────── (TH grey)  ──→ INT0
                 │     │  D3 ◄────────── (TR white) ──→ INT1
                 │     │  GND─────────── (GND black)
                 │     │
                 │     │           Trackball
                 │     │  5V ─────► Red
                 │     │  GND ────► Black
                 │     │  D9 ◄────► Yellow   (axis 1 A)  ──→ PCINT1
                 │     │  D10 ◄───► Green    (axis 1 B)  ──→ PCINT2
                 │     │  D11 ◄───► Blue     (axis 2 A)  ──→ PCINT3
                 │     │  D12 ◄───► Purple   (axis 2 B)  ──→ PCINT4
                 │     │
                 │     │  USB ─────► PC or USB charger power
                 │     └──────────────────┘
```

## Things to watch out for

### Pin direction on the Arduino side

During the Mega Mouse sequence, the console (= protocol master) drives TH and TR. The Arduino must therefore have D2 and D3 **as inputs**. Conversely, TL and D0-D3 are driven by the Arduino, so D4-D8 must be **outputs**.

This is exactly what the sketch does:
```c
DDRD = (DDRD & 0b00001111) | 0b11110000;  // D2,D3 in / D4-D7 out
DDRB |= (1 << 0);                          // D8 out
```

### Initialize idle pins right at startup

The sketch sets TL=1 and D=0 **before** enabling interrupts. That's important: if the console runs an ID test right after the Arduino boots, the pins must already be at valid levels, otherwise SGDK classification will fail.

### Pull-up on quadrature pins

D9-D12 are configured as `INPUT_PULLUP` to have a stable state even if the trackball is unplugged or broken. Without a pull-up, the pins float and PCINT fires endlessly on noise.

## Build photo (optional)

To be added in `docs/images/` once the final case is ready.
