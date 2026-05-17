# BLE_TouchButtons

Three colour buttons on the iPhone display with toolbar button support.
Tap a button to activate it, press T1 or T2 in the toolbar for status updates.

This example has two purposes: demonstrating the simple connection model for
button-driven apps, and showing the performance difference between compat and
native drawing modes side by side.

## What this example demonstrates

- **Simple connection model** — buttons are always redrawn from current state
  when touched, so no complex reconnect handling is needed
- **Compat vs native performance** — RED uses `Adafruit_GFX_Button` (slow),
  GREEN and BLUE use native `ESP32PhoneDisplay` (fast). The difference is
  visible and measurable
- **Dual display objects** — `tft` (compat) and `display` (native) sharing
  the same transport, each used where it makes sense
- **Toolbar key callbacks** — T1 and T2 press events from the iPhone toolbar
- **`onRedrawRequest`** — buttons rebuilt from current state when app returns
  from background

## Hardware required

- Any ESP32 board
- No additional hardware — the iPhone is the display and touchscreen

## How to run

1. Flash to your ESP32
2. Open RemoteGraphics → **Bluetooth** → connect
3. Tap RED, GREEN, or BLUE to activate a button
4. Press **T1** or **T2** in the app toolbar — status bar updates
5. Tap repeatedly and watch RED redraw slower than GREEN and BLUE

## Compat vs native performance

The three buttons demonstrate three different rendering costs for the same
visual result — an activated/deactivated coloured button with a label.

**RED — `Adafruit_GFX_Button::drawButton()` (compat mode):**
Text rendering routes through Adafruit_GFX's pixel-level font engine.
Each character at textSize 2 decomposes into ~35 `drawFastHLine` and
`drawPixel` commands. "RED" alone = ~105 commands just for the label.
`fillRoundRect` and `drawRoundRect` also decompose to pixel calls.
Total: ~200+ BLE commands per button draw — 8–10 BLE packets.

**GREEN and BLUE — `ESP32PhoneDisplay` (native mode):**
Every operation sends one compact command regardless of size:
- `fillRoundRect` → 1 command
- `drawRoundRect` → 1 command
- `print()` → 1 `GFX_CMD_WRITE_CHAR` per character (iPhone renders glyph)
Total: ~10 commands per button draw — fits in a single BLE packet.

**Why RED doesn't feel dramatically slower in practice:**
All three buttons flush together in `drawButtons()`. The ~200 RED commands
and ~20 GREEN+BLUE commands drain together in one batch before `flush()`.
At a 15ms connection interval, the whole batch drains in 1–2 intervals —
still fast enough to feel instant. The overhead of compat mode is hidden in
the batch. See [docs/porting.md](../../docs/porting.md) for the full packet
analysis and when compat overhead becomes visible.

## Dual display objects

`ESP32PhoneDisplay_Compat` is required to pass an `Adafruit_GFX*` to
`Adafruit_GFX_Button::initButtonUL()`. Rather than using compat for
everything (slow) or dropping `Adafruit_GFX_Button` entirely (more code),
this example holds both objects against the same transport:

```cpp
BleTransport             transport;
ESP32PhoneDisplay_Compat tft(transport, DISP_W, DISP_H);  // for Adafruit_GFX_Button
ESP32PhoneDisplay        display(transport);               // for fast drawing
```

`tft.begin()` establishes the session for both — `display.begin()` is
intentionally omitted since a second `GFX_CMD_BEGIN` would reset the session.
All commands from both objects flow into the same BLE stream in call order.

`getTextBounds()` for text centering is called on `tft` — it is pure local
Adafruit_GFX arithmetic, no BLE commands sent. `ESP32PhoneDisplay` uses its
own `getTextBounds()` implementation that works identically.

## Connection model

Buttons are always drawn from current state on touch — there is nothing to
reconstruct on reconnect beyond rebuilding what the display currently shows.
This makes TouchButtons an example of the **simple connection model** where
minimal connection handling is needed. See
[docs/transport.md](../../docs/transport.md) for the full explanation.

## Porting from Adafruit_GFX

Drawing calls are identical. Lines that differ are marked `// #Ported:` in
the source. See [docs/porting.md](../../docs/porting.md) for the full guide
including when to use compat mode and when to go native.

## See also

- `BLE_TouchPaint` — touch drawing with simple connection model
- `BLE_TouchPaint2` — low-latency touch queue draining
- `BLE_HelloWorld` — connection callbacks explained
- [docs/porting.md](../../docs/porting.md) — compat vs native packet analysis,
  when compat overhead matters, dual-object pattern
- [docs/transport.md](../../docs/transport.md) — connection model, display
  availability, `onRedrawRequest`