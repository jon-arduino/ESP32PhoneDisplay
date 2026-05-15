# BLE_HelloWorld

Minimal ESP32PhoneDisplay example. Draws a simple screen on the iPhone over
BLE and responds to toolbar button presses.

## What this example demonstrates

- Minimal sketch structure for ESP32PhoneDisplay
- Safe cross-core signalling — BLE callbacks fire on core 0, display calls
  must happen on the Arduino loop task (core 1)
- `onRedrawRequest` and `onSubscribed` — when to use each
- Session API — `setTitle()`, `setButton1()`, `setButton2()`
- `flush()` — explicit vs auto-flush

## Hardware required

- Any ESP32 board (ESP32, ESP32-S3, ESP32-C3, etc.)
- No additional hardware — the iPhone is the display

## How to run

1. Flash this sketch to your ESP32
2. Open the RemoteGraphics iPhone app
3. Tap **Bluetooth** → connect to **ESP32-Display**
4. The screen draws automatically. Press **T1** or **T2** in the toolbar
   to see a banner. Disconnect and reconnect — the screen redraws cleanly.

## onRedrawRequest and display state

The iPhone maintains a rendered framebuffer that persists across BLE
disconnects — the display shows stale content on reconnect, not a blank
screen. When BLE reconnects, or when the user switches away from the
RemoteGraphics app and back, the iPhone sends `BC_CMD_REDRAW_REQUEST` to ask
the ESP32 to rebuild the full display from current state.

`onSubscribed(true)` alone cannot cover the app-switching case (BLE was
never dropped). Register `onRedrawRequest` and set your draw flag there if
you want the display to stay correct across reconnects and app switching.

For the full explanation including background rendering behavior see
[docs/transport.md](../../docs/transport.md).

```
iPhone app                    ESP32
    |                           |
    |-- BLE connect ----------->|  onSubscribed(true) → _paused = false
    |-- BC_CMD_REDRAW_REQUEST ->|  onRedrawRequest()  → _drawPending = true
    |<-- full screen redraw ----|
    |                           |
    |-- T1/T2 press ----------->|  onKey()            → _keyPending = key
    |<-- banner update ----------|
    |                           |
    |-- BLE disconnect -------->|  onSubscribed(false)→ _paused = true
    |-- BLE reconnect --------->|  onSubscribed(true) → _paused = false
    |-- BC_CMD_REDRAW_REQUEST ->|  onRedrawRequest()  → _drawPending = true
    |<-- full screen redraw ----|
    |                           |
    |-- app backgrounded        |  (BLE stays active)
    |-- app foregrounded        |
    |-- BC_CMD_REDRAW_REQUEST ->|  onRedrawRequest()  → _drawPending = true
    |<-- full screen redraw ----|
```

## Porting from Adafruit_GFX

| Adafruit_GFX (local display)              | ESP32PhoneDisplay (iPhone)               |
|-------------------------------------------|------------------------------------------|
| `#include <Adafruit_ST7735.h>`            | `#include <ESP32PhoneDisplay.h>`         |
| `Adafruit_ST7735 display(CS, DC, RST);`   | `BleTransport transport;`                |
|                                           | `ESP32PhoneDisplay display(transport);`  |
| `display.initR(INITR_BLACKTAB);`          | `transport.begin();` + wait for connect  |
| `display.fillScreen(BLACK);`              | `display.fillScreen(BLACK);` — identical |
| *(always ready)*                          | `onRedrawRequest` / `onSubscribed`       |
| *(no equivalent)*                         | `display.flush();`                       |
| `fillRect / drawCircle / print / ...`     | **identical — no changes needed**        |

Lines that differ are marked `// #Ported:` in the source with the original
shown as a comment. See [docs/porting.md](../../docs/porting.md) for the
full porting guide.

## Driving a local display alongside the iPhone

The drawing API matches Adafruit_GFX so both can be driven simultaneously:

```cpp
Adafruit_ST7735   tft(TFT_CS, TFT_DC, TFT_RST);
BleTransport      transport;
ESP32PhoneDisplay phone(transport);

void drawScreen() {
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 240, 50, ST77XX_BLUE);
    tft.setCursor(20, 15);
    tft.setTextColor(ST77XX_WHITE);
    tft.print("Hello!");

    phone.fillScreen(BLACK);        // identical calls, different object
    phone.fillRect(0, 0, 240, 50, BLUE);
    phone.setCursor(20, 15);
    phone.setTextColor(WHITE);
    phone.print("Hello!");
    phone.flush();
}
```

## See also

- [docs/transport.md](../../docs/transport.md) — BLE transport architecture,
  callbacks, `onRedrawRequest` full explanation, connection interval, auto-flush
- [docs/porting.md](../../docs/porting.md) — porting guide from Adafruit_GFX
- `BLE_TouchButtons` — toolbar buttons and on-screen touch input
- `BLE_Telemetry` — live data partial-redraw pattern
- `WiFi_HelloWorld` — same example over WiFi