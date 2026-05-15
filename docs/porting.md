# Porting to ESP32PhoneDisplay

This guide covers migrating an existing Arduino sketch from a physical TFT
display and touchscreen to ESP32PhoneDisplay.

---

## Display porting

### Quick path — drop-in replacement (5 minutes)

If your sketch uses a concrete TFT type directly (most sketches do), the
migration is a single type change using `ESP32PhoneDisplay_Compat`.

**Before (Adafruit_ST7735):**
```cpp
#include <Adafruit_ST7735.h>
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

void setup() {
    tft.initR(INITR_BLACKTAB);
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(ST77XX_WHITE);
    tft.print("Hello!");
}
```

**After:**
```cpp
#include <ESP32PhoneDisplay_Compat.h>
#include <transport/BleTransport.h>

BleTransport             transport;
ESP32PhoneDisplay_Compat tft(transport);   // remove pin definitions

void setup() {
    transport.begin();
    while (!tft.isConnected()) { delay(100); }
    tft.begin();                // replaces tft.initR(...)
    tft.fillScreen(0x0000);     // BLACK in RGB565
    tft.setCursor(0, 0);
    tft.setTextColor(0xFFFF);   // WHITE in RGB565
    tft.print("Hello!");
}
```

Everything else — `drawCircle()`, `fillRect()`, `print()`, `setFont()` — is
identical.

### Colour constants

Adafruit defines colour constants in their display headers (`ST77XX_BLACK`,
`ILI9341_RED`, etc.). These aren't available when you remove the TFT include.
Replace them with RGB565 hex values or define your own:

```cpp
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define ORANGE  0xFC00
```

### Sketches that use Adafruit_GFX* pointers

If your sketch or a library it uses accepts `Adafruit_GFX*` or `Adafruit_GFX&`,
`ESP32PhoneDisplay_Compat` subclasses `Adafruit_GFX` and works everywhere a
base class pointer is expected:

```cpp
void drawUI(Adafruit_GFX &gfx) {    // existing function — unchanged
    gfx.fillRect(0, 0, 100, 50, 0xF800);
    gfx.setCursor(5, 15);
    gfx.print("Hello");
}

BleTransport             transport;
ESP32PhoneDisplay_Compat tft(transport);

void setup() {
    transport.begin();
    while (!tft.isConnected()) { delay(100); }
    tft.begin();
    drawUI(tft);   // works — tft IS an Adafruit_GFX
}
```

### Native driver — best performance

Once your sketch is working with `ESP32PhoneDisplay_Compat`, consider migrating
to the native `ESP32PhoneDisplay` class. Every GFX operation — `drawCircle()`,
`drawChar()`, rounded rects — sends a single compact command. With the compat
layer, shapes decompose to `drawPixel`/`drawFastHLine` calls via Adafruit_GFX's
non-virtual base (a C++ limitation, not ours).

```cpp
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>

BleTransport      transport;
ESP32PhoneDisplay display(transport);

void setup() {
    transport.begin();
    while (!transport.canSend()) { delay(100); }
    display.begin(240, 320);
    display.clear(0x0000);
    display.setCursor(0, 0);
    display.setTextColor(0xFFFF);
    display.print("Hello!");
    display.flush();
}
```

The API is identical to Adafruit_GFX except `Adafruit_GFX*` pointer
compatibility is lost. If you need that, stay on `ESP32PhoneDisplay_Compat`.

### When compat mode overhead matters

The performance impact of compat mode depends on how many commands a frame
generates, and how those commands fill BLE packets.

**BLE packet capacity:** Each BLE notification carries up to 252 bytes
(MTU 255 − 3 bytes ATT header). Every GFX command has 4 bytes of framing
overhead plus its payload. At a 15ms connection interval, one packet drains
per interval — so packet count directly determines frame time.

**Native mode — compact commands:**
A `fillRoundRect` or `drawRoundRect` is always one command regardless of
size. `print()` sends one `GFX_CMD_WRITE_CHAR` per character; the iPhone
renders each glyph. A typical button (fill + outline + 5-char label) generates
~10 commands and fits in a single packet — drains in one interval (~15ms).

**Compat mode — decomposed commands:**
`fillRoundRect`, `drawRoundRect`, and text rendering decompose to
`drawFastHLine` and `drawPixel` calls via Adafruit_GFX's non-virtual base.
A 180×50 rounded button with a 3-character label at textSize 2 generates
roughly 200 commands — 8–10 packets. At 15ms per interval that's 120–150ms
per button draw.

**The batching effect:**
Commands from all drawing calls accumulate in the BLE stream buffer and drain
together before `flush()`. When the total frame command count fits in 1–2
packets (≤504 bytes), compat overhead is negligible — the slow commands are
carried along in the same batch as fast ones and the frame still drains in
one interval. As compat command count grows, each additional packet adds one
full connection interval to frame time.

| Frame total   | Drain time (15ms interval) | Perceived         |
|---------------|----------------------------|-------------------|
| 1–2 packets   | 15–30ms                    | Imperceptible     |
| 3–5 packets   | 45–75ms                    | Slightly sluggish |
| 10+ packets   | 150ms+                     | Visibly slow      |

### Mixing compat and native in one sketch

If you need `Adafruit_GFX*` compatibility for one thing (e.g. a third-party
button library) but want native performance for everything else, hold both
objects against the same transport simultaneously:

```cpp
BleTransport             transport;
ESP32PhoneDisplay_Compat tft(transport, 240, 320);  // for Adafruit_GFX_Button
ESP32PhoneDisplay        display(transport);         // for fast drawing

void setup() {
    transport.begin();
    while (!transport.canSend()) { delay(100); }

    tft.begin();          // establishes phone session — call once only
    // display.begin() intentionally omitted — a second GFX_CMD_BEGIN would
    // reset the phone session. display shares the session tft.begin() created.

    // Compat path — slow, needed for Adafruit_GFX_Button
    someButton.initButtonUL(&tft, ...);
    someButton.drawButton(false);

    // Native path — fast, for everything else
    display.fillRoundRect(20, 140, 180, 50, 8, 0x03E0);
    display.setCursor(50, 158);
    display.setTextColor(0xFFFF);
    display.print("Native text");

    tft.flush();          // flush via either object — both wake the drain task
}
```

Commands from both objects flow into the same BLE stream in call order.
`getTextBounds()` for text centering math should be called on `tft` — it is
pure local Adafruit_GFX arithmetic and sends no BLE commands. `ESP32PhoneDisplay`
has no equivalent since it does not subclass Adafruit_GFX.

### Display size

Physical TFTs have fixed resolutions (128x160, 240x320, etc.).
With ESP32PhoneDisplay you choose the virtual display size in `begin(w, h)`.
The iPhone app scales to fill the screen regardless of resolution.

Start with the same size as your original display for minimal code changes.
Or go larger — 480x640 or 720x1280 cost nothing on the ESP32 and give far
more screen real estate on a modern iPhone.

### flush()

`flush()` marks an explicit frame boundary. The iPhone auto-flushes when the
command stream goes idle, so `flush()` is not strictly required — but calling
it at the end of each logical frame is best practice. It makes rendering
deterministic and prevents partial frames from appearing during complex draws.

---

## Touch porting

### From Adafruit_TouchScreen (resistive)

**Before:**
```cpp
#include <Adafruit_TouchScreen.h>
#define XP 8
#define YP A3
#define XM A2
#define YM 9
#define TS_MINX 120  // calibration constants
#define TS_MAXX 900
#define TS_MINY 70
#define TS_MAXY 920

TouchScreen ts(XP, YP, XM, YM, 300);

void loop() {
    TSPoint p = ts.getPoint();
    if (p.z > ts.pressureThreshhold) {
        // raw ADC values — must map to screen pixels
        int x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
        int y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());
        tft.drawPixel(x, y, WHITE);
    }
}
```

**After:**
```cpp
#include <touch/RemoteTouchScreen.h>

RemoteTouchScreen ts(transport);   // same transport as display

void setup() {
    // ... transport and display setup ...
    ts.begin();   // starts touch reporting, wires callbacks automatically
}

void loop() {
    TSPoint p = ts.getPoint();
    if (p.z > RemoteTouchScreen::MINPRESSURE) {
        // coordinates already mapped to virtual display pixels — no map() needed
        tft.drawPixel(p.x, p.y, WHITE);
    }
}
```

**Key difference:** `Adafruit_TouchScreen` returns raw ADC values (0–1023)
that require `map()` to convert to screen pixels. `RemoteTouchScreen` returns
coordinates already in virtual display pixel space (0 to width-1, 0 to
height-1). **Remove your `map()` calls and calibration constants** — they
are not needed and will produce wrong results if left in.

`p.z` is always `128` when touching (constant — no analog pressure on
capacitive iPhone screen). This is above any `pressureThreshhold` value used
in typical sketches (10–100), so the `p.z > threshold` check works unchanged.

### From Adafruit_FT6206 (capacitive)

**Before:**
```cpp
#include <Adafruit_FT6206.h>
Adafruit_FT6206 ts;

void setup() {
    ts.begin();
}

void loop() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        // p.x, p.y already in screen pixels — no mapping needed
        tft.drawPixel(p.x, p.y, WHITE);
    }
}
```

**After:**
```cpp
#include <touch/RemoteTouchScreen.h>
RemoteTouchScreen ts(transport);

void setup() {
    ts.begin();
}

void loop() {
    TSPoint p = ts.getPoint();
    if (p.z > RemoteTouchScreen::MINPRESSURE) {
        // p.x, p.y in virtual display pixels — same as FT6206
        tft.drawPixel(p.x, p.y, WHITE);
    }
}
```

FT6206 sketches are the easiest to port — both libraries return pre-mapped
pixel coordinates, so only the class name and the `touched()`→`p.z` check
pattern changes.

**Note:** FT6206 uses `TS_Point` (with underscore) while Adafruit_TouchScreen
uses `TSPoint`. Both have `.x`, `.y`, `.z` fields. `RemoteTouchScreen` uses
`TSPoint` (no underscore) matching the more common resistive library.

### Touch throttle

By default `RemoteTouchScreen` throttles TOUCH_MOVE events to 50ms (20Hz).
This matches the typical polling rate of Arduino sketches using physical
touchscreens. If your sketch needs faster updates:

```cpp
ts.begin(TOUCH_MODE_SINGLE, 16);   // 16ms = ~60Hz
```

Or if bandwidth is constrained:
```cpp
ts.begin(TOUCH_MODE_SINGLE, 100);  // 100ms = 10Hz
```

---

## Common issues

**Display is blank after porting:**
Make sure you call `transport.begin()` and wait for `isConnected()` before
calling `display.begin()` or `tft.begin()`.

**Touch coordinates are wrong:**
Check that you removed any `map()` calls and calibration constants from your
original resistive touch code. `RemoteTouchScreen` returns virtual display
pixel coordinates directly.

**Colours look wrong:**
Check that you're using RGB565 values. Some Adafruit display headers define
colour constants that aren't available once you remove the include.

**Sketch was passing the display to a third-party library:**
Use `ESP32PhoneDisplay_Compat` — it's an `Adafruit_GFX` subclass so existing
libraries accept it.

**Text or shapes are too small:**
Increase the virtual display size in `begin(w, h)`. A 128x160 virtual display
on an iPhone will be very small — try 320x480 or larger.