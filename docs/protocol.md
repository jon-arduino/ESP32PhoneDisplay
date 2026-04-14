# ESP32PhoneDisplay Wire Protocol

This document describes the binary protocol between the ESP32 and the iPhone
app. It is intended for anyone building an alternative receiver (Android,
desktop, web, etc.) or a custom transport implementation.

## Overview

Two independent byte streams share the same physical link (BLE or WiFi):

```
ESP32 → iPhone    GFX stream      Drawing commands + touch control
iPhone → ESP32    Back-channel    Touch events, keys, pong
```

Both streams use the same frame format. They never collide — the ESP32 only
sends GFX frames, the iPhone only sends back-channel frames (except PONG,
which is a back-channel reply to a GFX PING).

## Frame format (both directions)

```
[0xA5]       magic byte — always 0xA5
[lenLo]      payload length, uint16 little-endian, low byte
[lenHi]      payload length, uint16 little-endian, high byte
             length = 1 (cmd byte) + sizeof(payload)
[cmd]        command opcode
[payload...] 0 or more bytes, command-specific
```

Total frame size = 4 + payload_length bytes.

All multi-byte integers are **little-endian** throughout — frame headers,
payload fields, coordinates, colors, everything.

Colors are **RGB565** uint16 little-endian.

---

## GFX stream (ESP32 → iPhone)

### System commands

| Cmd  | Name            | Payload                        | Notes                          |
|------|-----------------|--------------------------------|--------------------------------|
| 0x01 | BEGIN           | w(2) h(2)                      | Set virtual display size       |
| 0x02 | FILL_SCREEN     | color(2)                       | Clear to color                 |
| 0x03 | FLUSH           | none                           | Explicit frame boundary (see below) |
| 0x05 | SET_ROTATION    | r(1)                           | 0-3, same as Adafruit_GFX     |
| 0x06 | INVERT_DISPLAY  | invert(1)                      | 0=normal, 1=inverted           |

### FLUSH behavior

`FLUSH (0x03)` marks an explicit frame boundary. It is **not required for
rendering** — the iPhone app auto-flushes when it detects a gap in the
command stream. However, sending FLUSH at the end of each logical frame is
strongly recommended:

- Makes frame boundaries explicit and deterministic
- Prevents partial frames from rendering mid-draw during complex scenes
- Ensures the display updates promptly rather than waiting for the auto-flush
  timeout
- Makes your code easier to reason about

**Best practice:**
```cpp
display.fillScreen(BLACK);
display.drawCircle(120, 160, 50, GREEN);
display.setCursor(10, 10);
display.print("Hello");
display.flush();   // render everything above as one atomic frame
```

Without `flush()`, the app will still render — but timing is less
predictable, especially over BLE where packet pacing varies.

### Drawing commands

| Cmd  | Name            | Payload                                       |
|------|-----------------|-----------------------------------------------|
| 0x10 | DRAW_PIXEL      | x(2) y(2) color(2)                            |
| 0x11 | DRAW_LINE       | x0(2) y0(2) x1(2) y1(2) color(2)             |
| 0x12 | DRAW_FAST_HLINE | x(2) y(2) w(2) color(2)                       |
| 0x13 | DRAW_FAST_VLINE | x(2) y(2) h(2) color(2)                       |
| 0x20 | DRAW_RECT       | x(2) y(2) w(2) h(2) color(2)                  |
| 0x21 | FILL_RECT       | x(2) y(2) w(2) h(2) color(2)                  |
| 0x22 | DRAW_ROUNDRECT  | x(2) y(2) w(2) h(2) r(2) color(2)             |
| 0x23 | FILL_ROUNDRECT  | x(2) y(2) w(2) h(2) r(2) color(2)             |
| 0x30 | DRAW_CIRCLE     | x(2) y(2) r(2) color(2)                       |
| 0x31 | FILL_CIRCLE     | x(2) y(2) r(2) color(2)                       |
| 0x32 | DRAW_TRIANGLE   | x0(2) y0(2) x1(2) y1(2) x2(2) y2(2) color(2) |
| 0x33 | FILL_TRIANGLE   | x0(2) y0(2) x1(2) y1(2) x2(2) y2(2) color(2) |

### Bitmap commands

| Cmd  | Name           | Payload                                  |
|------|----------------|------------------------------------------|
| 0x40 | DRAW_BITMAP    | x(2) y(2) w(2) h(2) + bitmap bytes      |
| 0x41 | DRAW_BITMAP_BG | x(2) y(2) w(2) h(2) fg(2) bg(2) + bitmap bytes |

Bitmap data is 1-bit monochrome, row-major, MSB first — same format as
Adafruit_GFX `drawBitmap()`. Byte count = ceil(w × h / 8).

### Text commands

| Cmd  | Name              | Payload       |
|------|-------------------|---------------|
| 0x50 | SET_CURSOR        | x(2) y(2)    |
| 0x51 | SET_TEXT_COLOR    | color(2)      |
| 0x52 | SET_TEXT_COLOR_BG | fg(2) bg(2)  |
| 0x53 | SET_TEXT_SIZE     | size(1)       |
| 0x54 | SET_TEXT_WRAP     | wrap(1)       |
| 0x55 | CP437             | enable(1)     |
| 0x56 | SET_FONT          | fontId(2)     |
| 0x57 | WRITE_CHAR        | c(1)          |

### Touch control commands

These tell the iPhone to start, stop, or reconfigure touch reporting. The
iPhone sends nothing until it receives TOUCH_BEGIN.

| Cmd  | Name         | Payload                  | Notes                          |
|------|--------------|--------------------------|--------------------------------|
| 0x70 | TOUCH_BEGIN  | mode(1) interval_ms(2)  | Start touch, set mode+throttle |
| 0x71 | TOUCH_END    | none                     | Stop touch reporting           |
| 0x72 | TOUCH_DELAY  | interval_ms(2)           | Update move throttle           |

**TOUCH_BEGIN payload:**
- `mode` (1 byte):
  - `0x00` — single touch: emulates both resistive (`Adafruit_TouchScreen`)
    and capacitive single-touch displays (`Adafruit_FT6206`, `Adafruit_CST816`).
    Sketch code using `getPoint()` and `p.z > MINPRESSURE` works unchanged.
  - `0x01` — single touch with gestures: swipe, double-tap, long-press *(future)*
  - `0x02` — multi-touch: up to 5 simultaneous points, different API *(future)*
- `interval_ms` (2 bytes, little-endian): milliseconds between TOUCH_MOVE
  events. `0` = every event, `50` = 50ms (20Hz default), `100` = 10Hz.

**TOUCH_DELAY payload:**
- `interval_ms` (2 bytes, little-endian): new throttle while touch is active.

### Transport commands

| Cmd  | Name | Payload | Notes                              |
|------|------|---------|------------------------------------|
| 0xF0 | PING | none    | WiFi heartbeat — iPhone replies with PONG |

---

## Back-channel (iPhone → ESP32)

### Transport

| Cmd  | Name | Payload | Notes                    |
|------|------|---------|--------------------------|
| 0xF1 | PONG | none    | Reply to GFX PING (WiFi) |

### Key commands

| Cmd  | Name | Payload | Notes           |
|------|------|---------|-----------------|
| 0xF2 | KEY1 | none    | T1 button press |
| 0xF3 | KEY2 | none    | T2 button press |

### Touch events

Sent by the iPhone when touch is active (after TOUCH_BEGIN received).

| Cmd  | Name        | Payload          | Notes                      |
|------|-------------|------------------|----------------------------|
| 0x10 | TOUCH_DOWN  | x(2) y(2) z(1)  | Finger placed on screen    |
| 0x11 | TOUCH_MOVE  | x(2) y(2) z(1)  | Finger moved (throttled)   |
| 0x12 | TOUCH_UP    | none             | Finger lifted              |

**Touch payload:**
- `x`, `y` (2 bytes each, little-endian): virtual display coordinates,
  clamped to `[0, width-1]` × `[0, height-1]`. Origin (0,0) is top-left.
  Touches in the letterbox/pillarbox border are dropped and never sent.
- `z` (1 byte): contact pressure. Always `128` on DOWN/MOVE, `0` on UP.
  iPhone capacitive screens have no real pressure — 128 is chosen to be
  above any `MINPRESSURE` threshold used in Adafruit_TouchScreen sketches
  (typically 10–100).

---

## Coordinate system

- Origin (0,0) is **top-left**
- Positive x goes **right**, positive y goes **down**
- All coordinates are in virtual display pixels as declared by `BEGIN (0x01)`
- The iPhone scales the virtual display to fill the screen — the ESP32 sketch
  always works in virtual pixel space regardless of iPhone screen resolution

---

## Heartbeat (WiFi only)

The ESP32 sends `PING (0xF0)` every 3 seconds. The iPhone must reply with
`PONG (0xF1)` within 9 seconds or the ESP32 drops the TCP connection.

BLE does not use the heartbeat — CoreBluetooth detects link loss natively via
the connection supervision timeout.

---

## BLE transport

- Service: Nordic UART Service (NUS) `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- TX characteristic (ESP32→iPhone, notify): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- RX characteristic (iPhone→ESP32, write): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- MTU: negotiated, typically 255 bytes (252 byte payload per packet)
- Both GFX and back-channel share the same characteristics

## WiFi transport

- Discovery: mDNS/Bonjour, service type `_uart._tcp`, domain `local.`
- Default port: 9000 (configurable in WiFiTransport constructor)
- Protocol: raw TCP byte stream — frames are self-delimiting via the 3-byte header
- Both GFX stream and back-channel share the same TCP connection