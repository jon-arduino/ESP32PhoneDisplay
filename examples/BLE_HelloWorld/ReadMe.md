# BLE_HelloWorld

A minimal ESP32PhoneDisplay sketch — good starting point for new projects.
Draws a screen on the iPhone over BLE, handles connect/reconnect cleanly,
and handles reconnect cleanly.

For a detailed walkthrough of all connection callbacks and protocol
diagnostics, see **BLE_HelloWorld_Debug**.

## What this example demonstrates

- **`onDisplayAvailable`** — primary signal for display ready/offline
- **`onConnected` / `onDisconnected`** — BLE transport connect/disconnect
- **`onRedrawRequest`** — display needs rebuilding after app backgrounding
- **`_displayReset` flag** — `begin()` and `setTitle()` only re-sent when
  the BLE session is new, not on every redraw
- **Volatile flag pattern** — safe cross-core signalling (callbacks on
  core 0, drawing on core 1)
- **Session API** — `setTitle()` sets the iPhone nav bar title
- **`flush()`** — explicit frame delivery

## Hardware required

- Any ESP32 board (ESP32, ESP32-S3, ESP32-C3, etc.)
- No additional hardware — the iPhone is the display

## How to run

1. Flash to your ESP32
2. Open the RemoteGraphics app → **Bluetooth** → connect
3. Screen draws automatically. Switch apps and return — screen redraws cleanly.
4. Disconnect and reconnect — screen redraws cleanly.

## Connection callbacks

Three callbacks work together to keep the display in sync:

**`onDisplayAvailable`** is the primary signal. The iPhone sends this when
the display becomes ready (~100ms after connect, and when returning from
background) or unavailable (app backgrounded, phone locked, disconnect).
Use it to start/stop drawing.

**`onRedrawRequest`** fires when the app returns to the foreground — the
screen may be stale from commands missed while backgrounded. Always fires
alongside `onDisplayAvailable(true)` in that case, but not on fresh connect.

**`onConnected` / `onDisconnected`** are transport-level signals. `onConnected`
serves as a fallback draw trigger. `onDisconnected` sets `_displayOffline`
immediately — on a BT link loss the display-unavailable signal won't arrive
from the app.

```
BLE connect:
  onConnected           → _redrawPending = true  (fallback)
  onDisplayAvailable    → _redrawPending = true  (primary)
  → drawScreen() called, begin()/setTitle() sent (_displayReset=true)

App switch + return (BLE stays up):
  onDisplayAvailable(false) → _displayOffline = true  (stop drawing)
  onDisplayAvailable(true)  → _redrawPending = true   (resume)
  onRedrawRequest           → _redrawPending = true
  → drawScreen() called, begin()/setTitle() skipped (_displayReset=false)

BLE disconnect:
  onDisconnected        → _displayOffline = true, _displayReset = true

BLE reconnect:
  onConnected           → _redrawPending = true
  onDisplayAvailable    → _redrawPending = true
  → drawScreen() called, begin()/setTitle() re-sent (_displayReset=true)
```

For full details see [docs/transport.md](../../docs/transport.md).

## Porting from Adafruit_GFX

| Adafruit_GFX (local display)              | ESP32PhoneDisplay (iPhone)               |
|-------------------------------------------|------------------------------------------|
| `#include <Adafruit_ST7735.h>`            | `#include <ESP32PhoneDisplay.h>`         |
| `Adafruit_ST7735 display(CS, DC, RST);`   | `BleTransport transport;`                |
|                                           | `ESP32PhoneDisplay display(transport);`  |
| `display.initR(INITR_BLACKTAB);`          | `transport.begin();`                     |
| `display.fillScreen(BLACK);`              | `display.fillScreen(BLACK);` — identical |
| *(always ready)*                          | `onDisplayAvailable` / `onConnected`     |
| *(no equivalent)*                         | `display.flush();`                       |
| `fillRect / drawCircle / print / ...`     | **identical — no changes needed**        |

Lines that differ are marked `// #Ported:` in the source. See
[docs/porting.md](../../docs/porting.md) for the full porting guide.

## Driving a local display alongside the iPhone

The drawing API matches Adafruit_GFX — drive both simultaneously:

```cpp
Adafruit_ST7735   tft(TFT_CS, TFT_DC, TFT_RST);
BleTransport      transport;
ESP32PhoneDisplay phone(transport);

void drawScreen() {
    tft.fillScreen(ST77XX_BLACK);   // local display
    phone.fillScreen(BLACK);        // iPhone — identical call
    // ... all other drawing calls identical, different object
    phone.flush();
}
```

## See also

- `BLE_HelloWorld_Debug` — full callback diagnostics and protocol testing
- `BLE_Telemetry` — live updating data, partial redraw pattern
- `BLE_TouchButtons` — toolbar buttons and touch input
- [docs/transport.md](../../docs/transport.md) — transport architecture and callbacks
- [docs/porting.md](../../docs/porting.md) — porting guide from Adafruit_GFX