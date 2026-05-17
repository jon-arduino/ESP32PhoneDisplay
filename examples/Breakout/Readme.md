# Breakout

Classic Breakout game — a minimal port from a physical TFT display to
ESP32PhoneDisplay. The game logic is unchanged from the original. Only
setup and connection handling changed.

This is the **baseline port** — straightforward, no optimisations. See
**Breakout2** for what can be achieved with fixed frame rate, touch queue
draining, and connection-aware pause/resume.

## What this example demonstrates

- **Minimal porting** — how little needs to change to move from a physical
  TFT to the iPhone display
- **Connection interval** — the single most impactful change for any ported
  game. Without it, the iOS default (30–100ms) caps display updates at
  10–30fps regardless of game loop speed
- **`onConnected` / `onDisconnected`** — simple reconnect handling that
  restarts the game on reconnect
- **T1 / T2 toolbar** — autoplay and player mode via iPhone toolbar buttons
- **`#Ported:` comments** — every line that changed from the original is
  marked with the original code shown alongside

## Hardware required

- Any ESP32 board
- No additional hardware — the iPhone is the display and touchscreen

## How to run

1. Flash to your ESP32
2. Open RemoteGraphics → **Bluetooth** → connect
3. Tap the screen to start. Drag your finger to move the paddle.
4. Press **T1** in the toolbar for autoplay, **T2** for player control.

## Porting changes

The complete list of what changed from the original hardware sketch:

| Original                              | Ported                                        |
|---------------------------------------|-----------------------------------------------|
| `#include <Adafruit_TFTLCD.h>`        | `#include <ESP32PhoneDisplay.h>`              |
| `Adafruit_TFTLCD tft(CS,CD,WR,RD,RST)`| `BleTransport transport;`                    |
|                                       | `ESP32PhoneDisplay display(transport);`       |
| `tft.reset(); tft.begin(id);`         | `display.begin(DISP_W, DISP_H);`             |
| `#include <Adafruit_TouchScreen.h>`   | `#include <touch/RemoteTouchScreen.h>`        |
| `TouchScreen ts(XP,YP,XM,YM,300);`   | `RemoteTouchScreen ts(transport);`            |
| `map(p.x, TS_MINX, TS_MAXX, 0, w)`   | *(removed — iPhone sends pixel coords)*       |
| `tft.width() / tft.height()`         | `DISP_W / DISP_H`                            |
| `onSubscribed`                        | `onConnected / onDisconnected`               |
| *(no equivalent)*                     | `transport.setConnectionInterval(15, 15)`     |

## Connection interval — the first-order fix for games

Without `transport.setConnectionInterval(15, 15)`, the iOS default connection
interval (30–100ms) limits display updates to 10–30fps regardless of how fast
the game loop runs. Every `flush()` sends a BLE packet, but that packet waits
until the next connection interval to be delivered. At 100ms intervals, even
a 200fps game loop only updates the display 10 times per second.

Setting 15ms cuts this to ~67fps potential — the difference between unplayable
and smooth. **This is the first thing to add when porting any game.**

See [docs/porting.md](../../docs/porting.md) for the full explanation.

## What Breakout2 improves

This baseline port is deliberately simple. Breakout2 demonstrates:

- **Fixed frame rate** — `millis()`-based frame budget for consistent speed
  regardless of BLE jitter
- **Touch queue draining** — newest paddle position used each frame,
  eliminating queue buildup lag
- **Brick flash state machine** — no blocking delays in the game loop
- **Pause on display offline** — game pauses gracefully on app switch or
  phone lock via `onDisplayAvailable`
- **Session API** — `setTitle()` and toolbar buttons integrated

## See also

- `Breakout2` — optimised version showing game-quality improvements
- [docs/porting.md](../../docs/porting.md) — full porting guide including
  connection interval, touch queue draining, and flush() explained
- `BLE_TouchPaint2` — touch queue draining demonstrated for drawing apps