# BLE_TouchPaint

Finger painting over BLE. Draw on the iPhone screen with your finger,
tap colour swatches to change colour, tap CLEAR to erase.

This is the simpler paint example — one touch point processed per loop pass,
one flush per stroke. For a low-latency version that stays right under your
finger at any drawing speed, see **BLE_TouchPaint2**.

## What this example demonstrates

- **Simple connection model** — minimal handling for apps where display state
  is self-managing. No redraw needed on reconnect — the iPhone framebuffer
  persists and drawing resumes automatically.
- **`onRedrawRequest`** — chrome (swatches, CLEAR button) redrawn when app
  returns from background, where iOS may have cleared the framebuffer.
- **`onDisplayAvailable`** — drawing pauses when display goes offline and
  resumes when it comes back.
- **Single-point touch** — `ts.getPoint()` returns newest position, suitable
  for simple drawing where stroke continuity matters less than simplicity.

## Hardware required

- Any ESP32 board
- No additional hardware — the iPhone is the display and touchscreen

## How to run

1. Flash to your ESP32
2. Open RemoteGraphics → **Bluetooth** → connect
3. Draw on the screen with your finger
4. Tap a colour swatch at the top to change colour
5. Tap **CLEAR** to erase the canvas

## Canvas persistence

The iPhone framebuffer persists independently of the BLE connection state.
This means:

**Brief BT interruption** — NimBLE's supervision timeout (~20s) keeps the
connection alive through short range gaps. Drawing resumes with canvas intact,
no reconnect needed.

**Full BT disconnect** — the framebuffer still persists on the iPhone. When
BLE reconnects, drawing resumes from where it left off without sending any
display commands.

**App backgrounded** — iOS may clear the framebuffer when the app is not
in the foreground. `onRedrawRequest` fires on app return and redraws the
chrome. The canvas is lost — this is unavoidable without storing stroke
history on the ESP32.

This makes `BLE_TouchPaint` an example of the **simple connection model**:
apps where the display is self-managing need very little connection handling.
See [docs/transport.md](../../docs/transport.md) for the full explanation.

## Why strokes lag at high drawing speed

`BLE_TouchPaint` calls `ts.getPoint()` once per loop pass and flushes once
per stroke. At a 15ms BLE connection interval this limits throughput to ~67
strokes/sec. Fast finger movement generates more than that — lag accumulates.

`BLE_TouchPaint2` solves this by draining the full touch queue each loop pass
and flushing once after all points are drawn. See that example and
[docs/porting.md](../../docs/porting.md) for a full explanation of the
technique and why it makes such a dramatic difference.

## Porting from Adafruit_GFX

All drawing calls are identical. Lines that differ are marked `// #Ported:`
in the source. See [docs/porting.md](../../docs/porting.md) for the full
porting guide.

## See also

- `BLE_TouchPaint2` — low-latency version using touch queue draining
- `BLE_TouchButtons` — on-screen buttons with touch hit detection
- [docs/transport.md](../../docs/transport.md) — connection callbacks,
  canvas persistence, display availability explained
- [docs/porting.md](../../docs/porting.md) — touch queue draining,
  connection interval, flush() explained