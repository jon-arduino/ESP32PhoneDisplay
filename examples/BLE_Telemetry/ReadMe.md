# BLE_Telemetry

Live sensor data display over BLE. Shows four sensor readings that update
every second using an efficient partial-redraw pattern.

## What this example demonstrates

- **Partial redraw** — static labels drawn once; only the value column
  redrawn each second. Keeps per-update BLE traffic minimal.
- **Volatile flag pattern** — safe cross-core signalling between BLE
  callbacks (core 0) and display/loop (core 1).
- **`onRedrawRequest`** — rebuilds full layout on reconnect or app foreground.
- **Conditional text colour** — temperature and battery change colour on threshold.
- **`flush()` batching** — all four value updates go out in one flush.

## Hardware required

- Any ESP32 board
- Replace the four `readXxx()` stubs with real sensor code

## How to run

1. Replace `readTemperature()`, `readHumidity()`, `readPressure()`,
   `readBattery()` with real sensor reads
2. Flash to your ESP32
3. Open RemoteGraphics → **Bluetooth** → connect
4. Values update every second. Disconnect and reconnect — layout redraws cleanly.

## Partial redraw pattern

```
┌─────────────────────────────┐
│  ESP32 Telemetry            │  ← drawn once (drawLabelFrame)
│  Live sensor data — 1s upd  │
├─────────────────────────────┤
│  Temp:  [ 22.6 C          ] │  ← label once / value every second
│  Humi:  [ 55.1 %          ] │
│  Pres:  [ 1013.3 hPa      ] │
│  Batt:  [ 85 %            ] │
└─────────────────────────────┘
```

Each second: `fillRect` clears old value, `print()` draws new value.
About 20 BLE commands per update — fits in a single BLE packet.

## Porting from Adafruit_GFX

All drawing calls are identical. Lines that differ are marked `// #Ported:`
in the source. See [docs/porting.md](../../docs/porting.md) for full details.

## onRedrawRequest

The iPhone's framebuffer persists across BLE disconnects. On reconnect or
app foreground, display state is unknown — `onRedrawRequest` triggers a full
`drawLabelFrame()` to resync from scratch before value updates resume.

See [docs/transport.md](../../docs/transport.md) for the full explanation.

## See also

- `BLE_HelloWorld` — connection handling and callbacks explained
- `BLE_TouchButtons` — toolbar and touch input
- [docs/transport.md](../../docs/transport.md) — transport architecture
- [docs/porting.md](../../docs/porting.md) — porting from Adafruit_GFX