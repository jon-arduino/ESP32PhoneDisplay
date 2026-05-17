# BLE_Telemetry

Live sensor data display over BLE. Shows four sensor readings updating
every second using an efficient partial-redraw pattern.

## What this example demonstrates

- **Partial redraw** — static labels drawn once on connect; only the value
  column redrawn each second. Keeps per-update BLE traffic minimal.
- **`onDisplayAvailable`** — primary signal for display ready/offline;
  stops value updates when display is unavailable.
- **`_displayReset` flag** — `begin()` and `setTitle()` only re-sent on
  a new BLE connection, not on every redraw.
- **Conditional text colour** — temperature and battery change colour
  based on threshold values.
- **`flush()` batching** — all four value updates go out in one flush.
- **Volatile flag pattern** — safe cross-core signalling between BLE
  callbacks (core 0) and drawing/loop (core 1).

## Hardware required

- Any ESP32 board
- Replace the four `readXxx()` stubs with real sensor reads

## How to run

1. Replace `readTemperature()`, `readHumidity()`, `readPressure()`,
   `readBattery()` with your actual sensor code
2. Flash to your ESP32
3. Open RemoteGraphics → **Bluetooth** → connect
4. Values update every second automatically
5. Switch apps and return — layout redraws, updates resume
6. Disconnect and reconnect — full session rebuilt, updates resume

## Partial redraw pattern

```
┌─────────────────────────────┐
│  ESP32 Telemetry            │  <- drawn once per session (drawLabelFrame)
│  "live" sensor data         │
├─────────────────────────────┤
│  Temp:  [ 22.6 C          ] │  <- label once / value every second
│  Humi:  [ 55.2 %          ] │
│  Pres:  [ 1013.5 hPa      ] │
│  Batt:  [ 85 %            ] │
└─────────────────────────────┘
```

`drawLabelFrame()` sends the title bar and labels once. `updateValues()`
sends only `fillRect + print` for each value — about 20 BLE commands per
second, well within a single BLE packet.

## Session reset vs display redraw

The `_displayReset` flag separates two distinct cases:

**BLE reconnect** — session state is lost. `begin()` and `setTitle()` must
be re-sent before drawing. `_displayReset` is set by `onDisconnected` and
cleared inside `drawLabelFrame()`.

**App switch/return** — BLE stays connected, session intact. `begin()` and
`setTitle()` are skipped — `drawLabelFrame()` just redraws the content.
Value updates resume immediately after.

## Porting from Adafruit_GFX

All drawing calls are identical. Lines that differ are marked `// #Ported:`
in the source. See [docs/porting.md](../../docs/porting.md) for the full
porting guide.

## See also

- `BLE_HelloWorld` — connection handling and callbacks explained
- `BLE_TouchButtons` — toolbar buttons and touch input
- [docs/transport.md](../../docs/transport.md) — transport architecture,
  `onDisplayAvailable`, auto-flush
- [docs/porting.md](../../docs/porting.md) — porting from Adafruit_GFX