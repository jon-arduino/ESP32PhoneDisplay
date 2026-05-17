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
`ILI9341_RED`, etc.). These aren't available once you remove the TFT include.

The simplest fix — include `Colors.h` from this library:

```cpp
#include <Colors.h>
```

This defines the full set of standard RGB565 colours plus drop-in aliases
for all common Adafruit driver constants (`ST77XX_*`, `ILI9341_*`, `HX8357_*`,
`SSD1351_*`). Sketches using those constants need no further changes.

Each constant is `#ifndef` guarded so `Colors.h` can coexist with an existing
display driver header without redefinition errors.

Alternatively, define your own RGB565 values:

```cpp
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define ORANGE  0xFD20
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
    display.fillScreen(0x0000);
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

### Display size and scaling

Physical TFTs have fixed resolutions (128×160, 240×320, etc.).
With ESP32PhoneDisplay you choose the virtual canvas size in `begin(w, h)`.
The iPhone app always scales this canvas to fill the screen — the virtual
size controls how many drawing coordinates you have, not how large the
result appears physically. The display is always full screen.

**Resolution — virtual pixel density:**

The iPhone scales the canvas by approximately `screen_size / virtual_size`.
At low virtual resolution each virtual pixel maps to many physical pixels —
drawings look blocky. At high resolution each virtual pixel is small —
drawings are sharp but lines and text may be very fine.

| Virtual size | Effect on iPhone |
|---|---|
| 128×160 | ~3× physical pixels per virtual pixel — very blocky. 1px lines look thick. Text at textSize(1) appears large. |
| 240×320 | ~1.6× — slightly soft. Good baseline. Text readable at textSize(1). |
| 480×854 | ~0.8× — sharp. 1px lines may be barely visible. Use textSize(2)+ for readability. |

There is no performance cost to choosing a larger virtual canvas — the ESP32
sends coordinates, not pixels. Choose size based on the visual result you want.

**Aspect ratio — filling the screen:**

The app preserves the virtual canvas aspect ratio and letterboxes
(black bars) if it doesn't match the iPhone screen. Modern iPhones are
approximately 9:19.5 portrait (~1:2.16 ratio). Common mismatches:

| Virtual size | Ratio | Result on iPhone 14 (390×844) |
|---|---|---|
| 240×320 | 1:1.33 | Significant pillarboxing — wide black bars on sides |
| 240×480 | 1:2.00 | Close — small letterboxing top/bottom |
| 240×520 | 1:2.17 | Near edge-to-edge on most iPhones |
| 320×693 | 1:2.17 | Higher resolution, same fill |

If your original sketch was for a 240×320 TFT, consider increasing the
height to 480 or 520 to better fill the iPhone screen.

**Line widths:**

All Adafruit_GFX drawing functions use 1-virtual-pixel wide lines —
there is no line width setting. At low virtual resolution these appear
thick on screen; at high resolution they appear hairline thin. Choose
your virtual resolution to get the line weight you want.

**Rotation:**

`setRotation(r)` rotates the virtual canvas (0 = portrait, 1 = landscape
90° clockwise, 2 = portrait flipped, 3 = landscape 90° counter-clockwise).
Swap width and height in `begin()` when using landscape:

```cpp
display.begin(320, 240);   // landscape — wider than tall
display.setRotation(1);
```

### flush()

`flush()` marks an explicit frame boundary and sends a sync marker to the
iPhone. Internally it also wakes the BLE drain task immediately rather than
waiting for the 5ms idle timeout.

Every `flush()` call results in a BLE packet being sent. How quickly that
packet reaches the iPhone depends on the **BLE connection interval** — the
rate at which the ESP32 and iPhone exchange data. At the iOS default interval
(30–100ms), each flush can take up to 100ms to deliver. At 15ms, delivery
is within 15ms. For games and animations this difference is dramatic —
see the Performance section below.

`flush()` is not strictly required — commands auto-flush within 5ms of going
idle — but calling it at the end of each logical frame is best practice. It
makes rendering deterministic and prevents partial frames appearing during
complex draws.

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

## Performance

### Connection interval — first-order fix for games and animations

**This is the single most impactful change for any ported game or animation.**

BLE data exchange happens in connection intervals — time slots negotiated
between the ESP32 and iPhone. The iOS default is typically 30–100ms. Every
`flush()` call sends a BLE packet, but that packet cannot leave until the
next connection interval. At 100ms intervals, a game running at 60fps still
only updates the display 10 times per second — regardless of how fast the
ESP32 is drawing.

Set the connection interval before calling `transport.begin()`:

```cpp
// Request 15ms interval — essential for games and animations.
// iOS minimum is 15ms. Must be called before transport.begin().
transport.setConnectionInterval(15, 15);

transport.begin();
```

**Effect:** At 15ms intervals, each `flush()` delivers within 15ms —
effectively 60+ fps potential. At the iOS default, the same code may deliver
at 10–30fps regardless of game loop speed. This single change transformed the
Breakout example from barely playable to smooth.

**Rule of thumb:** Set connection interval to 15ms for any sketch that calls
`flush()` more than once per second. For static displays or slow telemetry
updates, the default is fine.

### flush() and connection interval

Every `flush()` is a BLE packet, and the connection interval controls how
quickly packets are delivered:

| Connection interval | Max flush rate | Suitable for                    |
|---------------------|----------------|---------------------------------|
| 15ms (set manually) | ~67/sec        | Games, animation, touch drawing |
| 30ms (iOS default)  | ~33/sec        | UI, moderate update rates       |
| 100ms (iOS default) | ~10/sec        | Telemetry, slow updates         |

Don't flush more often than the connection interval allows — excess flushes
queue up in the stream buffer and add latency rather than reducing it.

### Touch queue draining for drawing apps

`RemoteTouchScreen` maintains a 16-point FIFO queue of touch events. Two
usage patterns suit different app types:

**Games — current position only (`ts.getPoint()`):**
Returns the newest touch position and discards queued history. Equivalent
to a live ADC read on physical hardware. Use for paddle control, button
presses, any case where only the current finger position matters.

```cpp
TSPoint p = ts.getPoint();
if (p.z > RemoteTouchScreen::MINPRESSURE) {
    paddle_x = p.x;
}
```

**Drawing apps — full path (`ts.available()` + `ts.getQueuedPoint()`):**
Drains all queued points since the last loop pass and draws each one.
Combined with a single `flush()` after the drain, this batches all stroke
commands into one or two BLE packets — the key to low-latency drawing.
Without queue draining, strokes lag visibly as the queue fills faster than
loop() processes it.

```cpp
bool drew = false;
while (ts.available()) {
    TSPoint p = ts.getQueuedPoint();
    if (p.z > RemoteTouchScreen::MINPRESSURE) {
        display.fillCircle(p.x, p.y, radius, color);
        drew = true;
    }
}
if (drew) display.flush();   // one packet for all points in this pass
```

See `BLE_TouchPaint2` for a complete working example and explanation of
why queue draining and batched flush are inseparable for low-latency drawing.

---


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

**Graphics look blocky or pixelated:**
Your virtual canvas resolution is too low. The iPhone scales the canvas to
fill the screen — at low resolution each virtual pixel maps to many physical
pixels. Increase `begin(w, h)` to a larger size. 240×480 or 320×693 are
good starting points for a portrait iPhone. There is no ESP32 performance
cost to a larger virtual canvas.

**1-pixel lines are barely visible:**
Your virtual canvas resolution is too high relative to the iPhone screen.
Each virtual pixel is smaller than one physical pixel, making 1px lines
nearly invisible. Either reduce the virtual canvas size, or use
`fillRect(x, y, w, 2, color)` to draw 2-virtual-pixel wide lines.

**Black bars on sides or top/bottom:**
Your virtual canvas aspect ratio doesn't match the iPhone screen. Modern
iPhones are approximately 9:19.5 portrait. A 240×320 canvas (3:4 ratio)
will have wide black bars on the sides. Try 240×520 or 320×693 for
near edge-to-edge fill on most iPhones.

**Text is too small to read:**
Increase `setTextSize()`. At high virtual resolution, textSize(1) characters
are small. textSize(2) or textSize(3) is more readable on a large iPhone screen.