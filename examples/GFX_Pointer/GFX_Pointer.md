# GFX_Pointer

Shows how to handle libraries and functions that require an `Adafruit_GFX*`
pointer when using ESP32PhoneDisplay.

This pattern comes up whenever you use a third-party library like
`Adafruit_GFX_Button`, or have existing sketch functions written to accept
`Adafruit_GFX*` or `Adafruit_GFX&`. The three approaches below are listed
in order of preference.

## Option 1 — Modify the library (best)

If you own the code or can copy it, change the parameter type from
`Adafruit_GFX*` to `ESP32PhoneDisplay*`. All method names are identical
so the function body does not change. Full native performance, no compat
layer needed.

```cpp
// Before
void drawPanel(Adafruit_GFX* gfx) {
    gfx->fillRect(10, 10, 100, 50, 0xF800);
    gfx->setCursor(15, 20);
    gfx->print("Hello");
}

// After — one line changed, body identical
void drawPanel(ESP32PhoneDisplay* gfx) {
    gfx->fillRect(10, 10, 100, 50, 0xF800);
    gfx->setCursor(15, 20);
    gfx->print("Hello");
}
```

For `Adafruit_GFX_Button` specifically: copy the source into your project,
change `Adafruit_GFX*` to `ESP32PhoneDisplay*` in the constructor and
`initButton()`/`initButtonUL()`. The drawing code inside is identical. You
get a fully native button class with no compat overhead.

## Option 2 — Dual-object pattern (good)

When you cannot modify the library (third-party, closed source, or too
complex to fork), hold a compat object alongside the native object on the
same transport:

```cpp
BleTransport             transport;
ESP32PhoneDisplay_Compat tft(transport, 240, 320);  // satisfies Adafruit_GFX*
ESP32PhoneDisplay        display(transport);         // for all your own drawing

void setup() {
    transport.begin();
    // ...
    tft.begin();        // establishes session for BOTH objects
    // display.begin() intentionally omitted — would reset the session

    thirdPartyLib.begin(&tft);   // pass compat where Adafruit_GFX* needed
}

void loop() {
    thirdPartyLib.draw();        // draws via compat (slower)

    display.fillRect(...);       // your drawing via native (fast)
    display.flush();
}
```

Both objects write to the same BLE stream in call order. `tft.begin()`
establishes the session for both — `display.begin()` must not be called
as it would send a second `GFX_CMD_BEGIN` and reset the session.

See `BLE_TouchButtons` for a complete working example of this pattern,
including the performance difference between compat and native drawing.

## Option 3 — Compat for everything (avoid)

Using `ESP32PhoneDisplay_Compat` for all drawing works but is the slowest
option. Text and shapes decompose to pixel-level BLE commands via
Adafruit_GFX's non-virtual base. A single button with a label can generate
200+ BLE commands where native would send ~10.

Only use compat where the `Adafruit_GFX*` pointer is actually required.
Use native everywhere else. See [docs/porting.md](../../docs/porting.md)
for the full performance analysis.

## What this example shows

The sketch calls `thirdPartyDrawPanel(&tft, ...)` — a function that requires
`Adafruit_GFX*` and cannot be modified. Everything else draws via native
`display`. Both objects share the transport and their commands interleave
in the BLE stream naturally.

## Hardware required

- Any ESP32 board
- No additional hardware — the iPhone is the display

## See also

- `BLE_TouchButtons` — full working example of the dual-object pattern
  with `Adafruit_GFX_Button`, including performance comparison
- [docs/porting.md](../../docs/porting.md) — when compat overhead matters,
  packet analysis, dual-object pattern explained