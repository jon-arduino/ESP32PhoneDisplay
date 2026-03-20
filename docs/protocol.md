# ESP32PhoneDisplay Wire Protocol

This document describes the binary protocol between the ESP32 and the iPhone app. It is intended for anyone building an alternative receiver (Android, desktop, web, etc.).

## Overview

Two independent byte streams share the same physical link (BLE or WiFi):

```
ESP32 → iPhone    GFX stream       Drawing commands, unframed, length-prefixed
iPhone → ESP32    Back-channel     Control frames (touch, keys, pong)
```

They never mix. The ESP32 only sends GFX stream bytes. The iPhone only sends back-channel frames (except pong, which is a back-channel reply to a GFX ping).

## GFX stream (ESP32 → iPhone)

Every command is a self-contained packet:

```
[0x??]  magic byte  (currently 0xA5)
[lo]    length low  (little-endian uint16 — number of bytes after the 3-byte header)
[hi]    length high
[cmd]   command byte
[...]   payload     (0 or more bytes, command-specific)
```

Total packet size = 4 + payload_length.

### Command table

| Cmd  | Name                | Payload (bytes)                          |
|------|---------------------|------------------------------------------|
| 0x00 | BEGIN               | w(2) h(2)                                |
| 0x01 | FLUSH               | none                                     |
| 0x02 | FILL_SCREEN         | color(2)                                 |
| 0x03 | SET_ROTATION        | r(1)                                     |
| 0x04 | INVERT_DISPLAY      | invert(1)                                |
| 0x10 | DRAW_PIXEL          | x(2) y(2) color(2)                       |
| 0x11 | DRAW_LINE           | x0(2) y0(2) x1(2) y1(2) color(2)        |
| 0x12 | DRAW_FAST_HLINE     | x(2) y(2) w(2) color(2)                  |
| 0x13 | DRAW_FAST_VLINE     | x(2) y(2) h(2) color(2)                  |
| 0x20 | DRAW_RECT           | x(2) y(2) w(2) h(2) color(2)             |
| 0x21 | FILL_RECT           | x(2) y(2) w(2) h(2) color(2)             |
| 0x22 | DRAW_ROUNDRECT      | x(2) y(2) w(2) h(2) r(2) color(2)        |
| 0x23 | FILL_ROUNDRECT      | x(2) y(2) w(2) h(2) r(2) color(2)        |
| 0x30 | DRAW_CIRCLE         | x(2) y(2) r(2) color(2)                  |
| 0x31 | FILL_CIRCLE         | x(2) y(2) r(2) color(2)                  |
| 0x32 | DRAW_TRIANGLE       | x0(2) y0(2) x1(2) y1(2) x2(2) y2(2) color(2) |
| 0x33 | FILL_TRIANGLE       | x0(2) y0(2) x1(2) y1(2) x2(2) y2(2) color(2) |
| 0x40 | DRAW_BITMAP         | x(2) y(2) w(2) h(2) color(2) + bitmap bytes |
| 0x41 | DRAW_BITMAP_BG      | x(2) y(2) w(2) h(2) fg(2) bg(2) + bitmap bytes |
| 0x50 | SET_CURSOR          | x(2) y(2)                                |
| 0x51 | SET_TEXT_COLOR      | color(2)                                 |
| 0x52 | SET_TEXT_COLOR_BG   | fg(2) bg(2)                              |
| 0x53 | SET_TEXT_SIZE       | size(1)                                  |
| 0x54 | SET_TEXT_WRAP       | wrap(1)                                  |
| 0x55 | CP437               | enable(1)                                |
| 0x56 | SET_FONT            | id(2)                                    |
| 0x57 | WRITE_CHAR          | c(1)                                     |
| 0xF0 | PING                | none  (iPhone replies with BC pong)      |

All multi-byte integers are **little-endian**. Colors are **RGB565** (uint16).

### Coordinate system

Origin (0,0) is top-left. Positive x goes right, positive y goes down. Coordinates are in virtual display pixels as set by `BEGIN`.

### Bitmap format

1-bit monochrome, row-major, MSB first (same as Adafruit_GFX `drawBitmap`). Byte count = ceil(w * h / 8).

## Back-channel (iPhone → ESP32)

All back-channel frames use the same framing:

```
[0xA5]  BC_MAGIC
[lo]    length low  (little-endian uint16 — bytes after the 3-byte header)
[hi]    length high
[cmd]   command byte
[...]   payload
```

### Command table

| Cmd  | Name        | Payload                          | Direction      |
|------|-------------|----------------------------------|----------------|
| 0xF0 | PING        | none (reserved, not yet used)    | iPhone → ESP32 |
| 0xF1 | PONG        | none                             | iPhone → ESP32 |
| 0xF2 | KEY1        | none                             | iPhone → ESP32 |
| 0xF3 | KEY2        | none                             | iPhone → ESP32 |
| 0x10 | TOUCH_DOWN  | x(2) y(2) z(1)                   | iPhone → ESP32 |
| 0x11 | TOUCH_MOVE  | x(2) y(2) z(1)                   | iPhone → ESP32 |
| 0x12 | TOUCH_UP    | none                             | iPhone → ESP32 |

Touch coordinates are big-endian uint16, mapped to virtual display space. `z` is 0xFF on contact, 0 on release.

## Heartbeat (WiFi only)

Over WiFi the ESP32 sends a PING (0xF0) every 3 seconds. The iPhone must reply with a PONG back-channel frame within 9 seconds or the ESP32 drops the connection. BLE does not use the heartbeat — CoreBluetooth handles link health natively.

## BLE transport details

- Service: Nordic UART Service (NUS) `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- TX characteristic (ESP32 → iPhone, notify): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- RX characteristic (iPhone → ESP32, write): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- MTU: negotiated, typically 255 bytes (252 byte payload)

## WiFi transport details

- Discovery: mDNS/Bonjour, service type `_uart._tcp`
- Port: 9000 (default, configurable)
- Protocol: raw TCP, no framing beyond the GFX stream packets above
