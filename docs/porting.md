# Porting from Adafruit_GFX

This guide covers migrating an existing Arduino sketch from a physical TFT display to ESP32PhoneDisplay.

## Quick path (5 minutes)

If your sketch uses a concrete TFT type directly (most sketches do), the migration is a single type change.

**Before:**
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
ESP32PhoneDisplay_Compat tft;   // remove pin definitions

void setup() {
    tft.begin();                // replaces tft.initR(...)
    tft.fillScreen(0x0000);     // BLACK in RGB565
    tft.setCursor(0, 0);
    tft.setTextColor(0xFFFF);   // WHITE in RGB565
    tft.print("Hello!");
}
```

That's it. Everything else — `drawCircle()`, `fillRect()`, `print()`, `setFont()` — is identical.

## Colour constants

Adafruit defines colour constants in their display headers (`ST77XX_BLACK`, `ILI9341_RED`, etc.). These aren't available when you remove the TFT include. Replace them with RGB565 hex values or define your own:

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

## Sketches that use Adafruit_GFX* pointers

If your sketch or a library it uses accepts `Adafruit_GFX*` or `Adafruit_GFX&`, use `ESP32PhoneDisplay_Compat` — it subclasses `Adafruit_GFX` and works anywhere a base class pointer is expected:

```cpp
#include <ESP32PhoneDisplay_Compat.h>

ESP32PhoneDisplay_Compat tft;

void drawSomething(Adafruit_GFX &gfx) {     // existing function signature
    gfx.fillRect(0, 0, 100, 100, 0xF800);
}

void setup() {
    tft.begin();
    drawSomething(tft);   // works — tft IS an Adafruit_GFX
}
```

## Going further — native driver

Once your sketch is working with `ESP32PhoneDisplay_Compat`, consider migrating to the native `ESP32PhoneDisplay` class. The native driver sends every GFX operation as a single compact command — no pixel decomposition for `drawCircle()`, `drawChar()`, or rounded rects.

The API is identical except:
- Include `ESP32PhoneDisplay.h` instead of `ESP32PhoneDisplay_Compat.h`
- Construct with an explicit transport: `ESP32PhoneDisplay display(transport)`
- `Adafruit_GFX*` pointer compatibility is lost (use native type directly)

```cpp
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>

BleTransport        transport;
ESP32PhoneDisplay   display(transport);

void setup() {
    transport.begin();
    while (!transport.canSend()) { delay(100); }
    display.begin(240, 320);
    display.clear(0x0000);
    display.print("Hello!");
    display.flush();
}
```

## Display size

Physical TFTs have fixed pixel counts (128x160, 240x320, etc.). With ESP32PhoneDisplay you choose the virtual display size in `begin(w, h)`. The iPhone app scales the output to fill the screen.

Choose a size that matches your original display for minimal sketch changes, or go larger — 480x640 or 720x1280 cost nothing on the ESP32 side and give you much more display real estate on a modern iPhone.

## Touch input

If your sketch uses `Adafruit_TouchScreen`, replace it with `RemoteTouchScreen`:

```cpp
// Before:
#include <Adafruit_TouchScreen.h>
TouchScreen ts(XP, YP, XM, YM, 300);

// After:
#include <touch/RemoteTouchScreen.h>
RemoteTouchScreen ts;

// Register with your transport (in setup):
transport.onTouch([](uint8_t cmd, int16_t x, int16_t y) {
    ts.handleTouch(cmd, x, y);
});

// In loop() — identical:
TSPoint p = ts.getPoint();
if (p.z > RemoteTouchScreen::MINPRESSURE) {
    // finger down at p.x, p.y
}
```

## Common issues

**Sketch was passing the display to a third-party library:**
Use `ESP32PhoneDisplay_Compat` — it's an `Adafruit_GFX` subclass so existing libraries accept it.

**Colours look wrong:**
Check that you're using RGB565 values. Some Adafruit constants use a different format.

**Display is blank:**
Call `display.flush()` after drawing. The library batches commands and the iPhone renders on flush.

**Text is too small:**
Increase the virtual display size in `begin()` or call `setTextSize()` with a larger value.
