# BLE_TouchPaint2

Low-latency finger painting over BLE. Paint strokes stay right under
your finger regardless of drawing speed.

Compare to `BLE_TouchPaint` — the difference is dramatic and immediately
obvious. This example exists specifically to show why.

## What this example demonstrates

- **Queued touch points** — touch events accumulate in a buffer so all
  points since the last loop() pass can be drawn in a single batch
- **Batched drawing** — app performance is not limited by drawing speed,
  it is limited by BLE throughput. Throughput is maximised by putting more
  points in a single packet. One flush() per batch, not one per circle
- **Duplicate filtering** — skips identical positions, fewer commands,
  less BLE congestion, cleaner strokes
- **Minimal connection handling** — lightweight pattern for apps that
  don't need complex reconnect management

## Hardware required

- Any ESP32 board
- No additional hardware — the iPhone is the display

## How to run

1. Flash to your ESP32
2. Open RemoteGraphics → **Bluetooth** → connect
3. Draw on the screen — strokes follow your finger instantly
4. Tap a colour swatch at the bottom to change colour
5. Tap **CLR** to erase the canvas

## Why it feels instant

BLE delivers data in connection intervals — typically 15ms. Each interval
is one opportunity to send a batch of commands. The question is how many
strokes fit in each batch.

**BLE_TouchPaint — one stroke per interval:**
```
loop():  getPoint() → fillCircle() → flush()   ← one point, one flush
loop():  getPoint() → fillCircle() → flush()   ← one point, one flush
loop():  getPoint() → fillCircle() → flush()   ← one point, one flush
```
At 15ms per interval, max throughput = ~67 strokes/sec. Fast finger
movement generates ~120 points/sec. Lag accumulates and grows the
faster you draw.

**BLE_TouchPaint2 — all strokes per interval:**
```
loop():  while(available) { getQueuedPoint() → fillCircle() }   ← N points
         flush()                                                 ← one flush
```
All points accumulated during one interval go out in the next, regardless
of how many. Throughput scales with finger speed. Lag cannot accumulate.

**Queue draining and batched flush are inseparable:**
- Queue draining without batched flush: still one notification per point
- Batched flush without queue draining: still one point processed per loop
- Together: N points → 1-2 BLE notifications, always one interval of lag

Duplicate filtering reduces command count and keeps strokes clean but is
not the reason for the dramatic latency improvement.

## Connection handling

Reconnect restarts the session and clears the canvas — simple and
predictable. This is the right pattern for apps where display state does
not need to survive a disconnect.

Brief BT interruptions within NimBLE's supervision timeout (~20s) recover
transparently — the canvas survives because the iPhone framebuffer persists
across short range gaps.

## Layout

```
┌─────────────────────────────┐
│  Touch Paint | select below │  ← header (24px)
│                             │
│       drawing area          │
│                             │
├──┬──┬──┬──┬──┬──┬──┬────────┤
│R │Y │G │C │B │M │W │  CLR  │  ← swatches + clear (30px)
└──┴──┴──┴──┴──┴──┴──┴────────┘
```

Swatches at the bottom keep them out of the way while drawing.
Selection indicator: a 4px coloured bar above the active swatch.

## See also

- `BLE_TouchPaint` — simpler version, shows the latency problem
- `DualTransport_TouchPaint` — same technique over BLE or WiFi
- `BLE_TouchButtons` — toolbar and on-screen touch buttons
- [docs/transport.md](../../docs/transport.md) — BLE transport architecture