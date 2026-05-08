# Mega Mouse protocol — technical notes

This document gathers the technical details of the protocol and the pitfalls we hit during development, to help anyone who wants to reproduce or extend this project.

## Protocol overview

The Mega Mouse uses the same 7 signal lines as the classic controller on the DB-9, but with different semantics. The protocol is a **3-line handshake** where the console drives TH and TR to clock the read, and the mouse responds by presenting a nibble (4 bits = D0-D3) on each transition, with TL as a synchronization bit.

Reference source: [Plutiedev — Mega Mouse protocol](https://www.plutiedev.com/mouse).

## Boot identification (SEGA 4-bit ID test)

At startup, SGDK calls `JOY_init()` which runs a fast identification test on each port. The test consists of:

1. Set TH=1, TR=1, read bits D2 and D3
2. Set TH=1, TR=0, read bits D0 and D1
3. Set TH=0, TR=1, read bits D2 and D3
4. Set TH=0, TR=0, read bits D0 and D1
5. Combine the 4 bits into a 4-bit identifier

The bursts between steps are spaced about **12 µs** apart. That's very tight for a 16 MHz AVR that has to react to TH/TR changes via INT0/INT1.

### SEGA identifier codes

| Code | Type | SGDK constant |
|---|---|---|
| 0x0 | Menacer | `PORT_TYPE_MENACER` |
| 0x1 | Justifier | `PORT_TYPE_JUSTIFIER` |
| 0x3 | Mouse | `PORT_TYPE_MOUSE` |
| 0x7 | Multitap (Teamplayer) | `PORT_TYPE_TEAMPLAYER` |
| 0xD | Classic pad | `PORT_TYPE_PAD` |
| 0xF | Unknown (nothing) | `PORT_TYPE_UNKNOWN` |

To respond with 0x3 (mouse), our Arduino must present:
- TH=1, TR=1 → D2D3 = 0x0 (bits 2-3 of 0x3 = 00)
- TH=1, TR=0 → D0D1 = 0x3 (bits 0-1 of 0x3 = 11)
- TH=0, TR=1 → D2D3 = 0x0
- TH=0, TR=0 → D0D1 = 0x3

(See Eke-Eke / SpritesMind for the exact combination formula.)

## Read sequence (9 nibbles)

Once the mouse is identified, the console initiates a read by writing this sequence to the DATA port:

```
$60 (idle, TH=1, TR=1)
$20 (TH=0, TR=1)  → reads nibble 0
$00 (TH=0, TR=0)  → reads nibble 1
$20 (TH=0, TR=1)  → reads nibble 2
$00 (TH=0, TR=0)  → reads nibble 3
$20 (TH=0, TR=1)  → reads nibble 4
$00 (TH=0, TR=0)  → reads nibble 5
$20 (TH=0, TR=1)  → reads nibble 6
$00 (TH=0, TR=0)  → reads nibble 7
$20 (TH=0, TR=1)  → reads nibble 8
$60 (back to idle)
```

On each console write, the mouse responds by presenting a nibble on D0-D3 and setting TL to the proper level (1 for even-indexed nibbles, 0 for odd ones after the 0).

### 9-nibble contents

| Index | Value | Meaning |
|---|---|---|
| 0 | `0xB` | Mouse signature |
| 1 | `0xF` | Header (ignored) |
| 2 | `0xF` | Header (ignored) |
| 3 | `YO XO YS XS` | Overflow and signs |
| 4 | `S M R L` | Buttons (Start, Middle, Right, Left) |
| 5 | X[7:4] | High nibble of \|ΔX\| |
| 6 | X[3:0] | Low nibble of \|ΔX\| |
| 7 | Y[7:4] | High nibble of \|ΔY\| |
| 8 | Y[3:0] | Low nibble of \|ΔY\| |

Bits in nibble 3:
- bit 0 (XS): 1 if X is negative
- bit 1 (YS): 1 if Y is negative
- bit 2 (XO): 1 if X overflow
- bit 3 (YO): 1 if Y overflow

## Arduino-side implementation

### Pre-computed tables

The 16 MHz AVR doesn't have time to compute PORTD/PORTB values inside the INT1 ISR. So we pre-compute the values to apply for each nibble in two tables `portd_tab[9]` and `portb_tab[9]`. The INT1 ISR just does a lookup and an OR on PORTD.

### Lookup tables vs on-the-fly computation

A first version tried to compute the PORTD values inside the ISR. It missed timing windows. With pre-computed tables and the ISR reduced to a direct assignment, timing holds at 16 MHz.

### INT0 / INT1 race condition

During the sequence, the console makes TH **and** TR transitions. The INT0 and INT1 ISRs can fire in overlap. The protection:
- INT0 always resets `nibbleIndex = 0` when TH=1 (idle or boot)
- INT1 starts with `if (PIND & (1 << 2)) return;` — if TH=1, we ignore (the TR transition may have arrived ahead of the TH transition)

## ROM-side implementation

### Why bypass SGDK?

`JOY_init()` classifies the port via the 4-bit test described above. If the read is imperfect (tight timing), the console may classify the port as TEAMPLAYER (0x07) instead of MOUSE (0x03). Once that classification is set, **`JOY_setSupport(JOY_SUPPORT_MOUSE)` is not enough to force SGDK to read in mouse mode** — polling uses the frozen `port_type`.

Solution: `JOY_setSupport(PORT_2, JOY_SUPPORT_OFF)` fully disables SGDK polling for that port, then we directly access the I/O registers to run the sequence by hand.

### I/O addresses

| Port | DATA | CTRL |
|---|---|---|
| 1 | `0xA10003` | `0xA10009` |
| 2 | `0xA10005` | `0xA1000B` |

### CTRL configuration

```c
*ctrl = 0x60;   // bits 6 (TH) and 5 (TR) as outputs; others as inputs
```

With this configuration:
- TH and TR can be driven by the console
- TL and D0-D3 are driven by the mouse, read by the console

### Raw read vs waiting for TL

The "official" sequence waits for TL to flip to the expected level after each write, as synchronization. In our case (Arduino emulating the mouse), I use a **fixed delay** (~30 µs) instead of waiting for TL:

```c
*data = cmds[i];
for (volatile u16 j = 0; j < 200; j++);  // ~30 µs
mouseBuffer[i] = *data & 0x0F;
```

Advantages:
- More tolerant to AVR timing variations
- No risk of infinite timeout if the Arduino crashes or misresponds
- Simpler code

The `200` delay is calibrated to give the Arduino time to react to INT1 and update PORTD. At the 68000's 7.67 MHz, 200 iterations of an empty loop take ~30 µs.

### Anti-glitch

The SGDK identification bursts and the tight timing occasionally cause a corrupted read, which manifests as a sudden cursor jump. The ROM filters out deltas exceeding `MAX_DELTA = 30` pixels per frame:

```c
if (dX > MAX_DELTA || dX < -MAX_DELTA ||
    dY > MAX_DELTA || dY < -MAX_DELTA) {
    glitchCount++;
    dX = 0;
    dY = 0;
}
```

At 60 Hz, a human can't move a cursor more than 30 pixels per frame in a useful way. Values above that are always noise.

## Pitfalls encountered (lessons learned)

### 1. Off-by-one indexing vs SpritesMind

The SpritesMind table describing the nibbles uses 1-based numbering that **starts at 0**, which can be misleading. The Plutiedev doc (1-based starting from 1) is clearer and was our final reference.

### 2. JOY_setSupport(MOUSE) is not enough

Even called after `JOY_init()`, `JOY_setSupport(PORT_2, JOY_SUPPORT_MOUSE)` does not force a mouse-mode read if the port was classified as TEAMPLAYER. You need `JOY_SUPPORT_OFF` + manual reading.

### 3. SYS_hardReset() crashes

Trying to force a re-classification via `SYS_hardReset()` doesn't work — the function crashes in some configurations. The bypass is more reliable.

### 4. The Arduino bootloader delays startup

The bootloader on a CH340 Nano clone takes ~500 ms to 1 s to hand over to the sketch after reset. If the console boots before that, the Arduino misses the ID test and classification fails.

Solutions:
- Cold reboot the console **after** the Arduino is ready (LED L on)
- Flash the Arduino without bootloader using an ISP programmer (USBasp ~5€)
- Add an RC capacitor on the Nano's RESET pin to force a reset at power-up

### 5. Timer0 and parasitic IRQs

On the Arduino, Timer0 powers `millis()` but generates a 1024 µs IRQ that can interrupt our critical ISRs. We keep Timer0 active (need millis() in the loop) but disable Timer1, Timer2, USART (unless Serial debug is needed) and ADC.

### 6. The disconnected wire

During debugging, several hours lost on "software" issues that turned out to be a poorly crimped wire in the DB-9. Always check continuity with a multimeter **before** diving into code.

## References

- [Plutiedev — Mega Mouse protocol](https://www.plutiedev.com/mouse) — main protocol reference
- [SpritesMind forum](http://gendev.spritesmind.net/forum/) — old but rich technical discussions
- [SGDK source code](https://github.com/Stephane-D/SGDK/blob/master/src/joy.c) — to understand `JOY_init()` and classification
- [Eke-Eke notes](http://gendev.spritesmind.net/forum/viewtopic.php?t=1185) — SEGA ID formula
- [HotPixelChannel/Mouse-To-SEGA-MD](https://github.com/HotPixelChannel/Mouse-To-SEGA-MD) — neighboring project
