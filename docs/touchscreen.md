# Touch Input with RemoteTouchScreen

ESP32PhoneDisplay can receive touch input from the iPhone over the same BLE
or WiFi connection used for graphics. The iPhone screen becomes a touchscreen
for your ESP32 sketch — no extra hardware, no wiring, no ADC pins needed.

Touch is **opt-in**. The iPhone sends nothing until your sketch calls
`ts.begin()`. When done, call `ts.end()` to stop.

---

## Quick start

```cpp
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>

BleTransport      transport;
ESP32PhoneDisplay display(transport);
RemoteTouchScreen ts(transport);      // same transport as display

void setup() {
    transport.begin();
    while (!transport.canSend()) { delay(100); }

    display.begin(240, 320);
    display.clear(0x0000);

    ts.begin();    // tell iPhone to start sending touch events
}

void loop() {
    TSPoint p = ts.getPoint();
    if (p.z > RemoteTouchScreen::MINPRESSURE) {
        // finger is down at virtual display coordinates p.x, p.y
        display.fillCircle(p.x, p.y, 4, 0xFFFF);
        display.flush();
    }
}
```

---

## API reference

### Constructor

```cpp
RemoteTouchScreen ts(transport);
```

Pass the same `GraphicsTransport` instance used for the display. Works with
`BleTransport`, `WiFiTransport`, or any custom transport that implements
`onTouch()`.

### begin()

```cpp
ts.begin();                                    // all defaults
ts.begin(TOUCH_MODE_RESISTIVE);                // explicit mode, default throttle
ts.begin(TOUCH_MODE_RESISTIVE, 16);            // ~60Hz move events
ts.begin(TOUCH_MODE_RESISTIVE, 100);           // 10Hz move events
```

Sends `TOUCH_BEGIN` to the iPhone and registers the back-channel callback
automatically. Call after the transport is connected.

**Parameters:**

| Parameter    | Type     | Default                          | Description                    |
|--------------|----------|----------------------------------|--------------------------------|
| mode         | uint8_t  | `TOUCH_MODE_RESISTIVE` (0x00)    | Touch emulation mode           |
| interval_ms  | uint16_t | 50                               | TOUCH_MOVE throttle in ms      |

**Touch modes:**

| Constant              | Value | Emulates                                      |
|-----------------------|-------|-----------------------------------------------|
| `TOUCH_MODE_RESISTIVE`| 0x00  | `Adafruit_TouchScreen` + `Adafruit_FT6206`    |

Future modes (not yet implemented):
- `TOUCH_MODE_GESTURES` (0x01) — single touch with swipe, double-tap, long-press
- `TOUCH_MODE_MULTI` (0x02) — up to 5 simultaneous touch points

**Move throttle:**

`interval_ms` controls how often `TOUCH_MOVE` events are sent while a finger
is held down and moving.

| interval_ms | Rate  | Good for                          |
|-------------|-------|-----------------------------------|
| 0           | max   | Smooth drawing, games             |
| 16          | ~60Hz | Responsive UI                     |
| 50          | 20Hz  | Default — standard UI interaction |
| 100         | 10Hz  | Low bandwidth, simple buttons     |

`TOUCH_DOWN` and `TOUCH_UP` are always sent immediately, unthrottled.

### end()

```cpp
ts.end();
```

Sends `TOUCH_END` to the iPhone and clears the current touch state. The iPhone
stops sending touch events. Call if you want to disable touch while keeping
the display active.

### setDelay()

```cpp
ts.setDelay(16);    // increase to ~60Hz while in a drawing mode
ts.setDelay(100);   // reduce when not needed
```

Update the TOUCH_MOVE throttle while touch is active. Sends `TOUCH_DELAY` to
the iPhone immediately — no need to call `end()` / `begin()`.

### getPoint()

```cpp
TSPoint p = ts.getPoint();
```

Returns the most recent touch state as a `TSPoint`. Compatible with
`Adafruit_TouchScreen::getPoint()`.

| Field | Type    | Value when touching | Value when not touching |
|-------|---------|---------------------|-------------------------|
| p.x   | int16_t | 0 to display width-1 | 0                      |
| p.y   | int16_t | 0 to display height-1 | 0                     |
| p.z   | int16_t | 128 (BC_TOUCH_Z_CONTACT) | 0                  |

Coordinates are in virtual display pixels as declared by `display.begin(w, h)`.
Origin (0,0) is top-left. Touches outside the display area (letterbox/pillarbox
border) are dropped by the iPhone and never sent.

Safe to call from `loop()` while `handleTouch()` fires on the BLE/WiFi
receive task — the ESP32's aligned 16-bit writes are atomic for single-touch.

### touched()

```cpp
if (ts.touched()) { ... }
```

Returns true if a finger is currently down. Compatible with
`Adafruit_FT6206::touched()`. Equivalent to `ts.getPoint().z > MINPRESSURE`.

### Constants

```cpp
RemoteTouchScreen::MINPRESSURE    // = 1  — use in p.z > MINPRESSURE checks
ts.pressureThreshhold             // = 1  — public member, matches Adafruit_TouchScreen
```

---

## Coordinate space

All touch coordinates are in **virtual display pixel space** — the same space
your drawing commands use. `p.x` and `p.y` are ready to pass directly to
`drawPixel()`, `fillCircle()`, `btn.contains()`, etc.

**This is different from physical resistive touchscreens**, which return raw
ADC values (0–1023) requiring `map()` and calibration constants. If you are
porting from `Adafruit_TouchScreen`, remove your `map()` calls and calibration
defines — they will produce wrong results with `RemoteTouchScreen`.

See [porting.md](porting.md) for detailed porting examples from both resistive
and capacitive Adafruit libraries.

---

## Porting from Adafruit_TouchScreen (resistive)

Minimal change — remove pins, calibration, and `map()` calls:

```cpp
// BEFORE
#include <Adafruit_TouchScreen.h>
#define XP 8
#define YP A3
#define XM A2
#define YM 9
#define TS_MINX 120
#define TS_MAXX 900
#define TS_MINY 70
#define TS_MAXY 920
TouchScreen ts(XP, YP, XM, YM, 300);

void loop() {
    TSPoint p = ts.getPoint();
    if (p.z > ts.pressureThreshhold) {
        int x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
        int y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());
        tft.drawPixel(x, y, WHITE);
    }
}

// AFTER
#include <touch/RemoteTouchScreen.h>
RemoteTouchScreen ts(transport);

void setup() { ts.begin(); }

void loop() {
    TSPoint p = ts.getPoint();
    if (p.z > ts.pressureThreshhold) {       // unchanged
        tft.drawPixel(p.x, p.y, WHITE);       // no map() needed
    }
}
```

## Porting from Adafruit_FT6206 (capacitive)

Change class name and replace `touched()` check pattern:

```cpp
// BEFORE
#include <Adafruit_FT6206.h>
Adafruit_FT6206 ts;

void setup() { ts.begin(); }

void loop() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();           // TS_Point with underscore
        tft.drawPixel(p.x, p.y, WHITE);
    }
}

// AFTER
#include <touch/RemoteTouchScreen.h>
RemoteTouchScreen ts(transport);

void setup() { ts.begin(); }

void loop() {
    if (ts.touched()) {                       // touched() still works
        TSPoint p = ts.getPoint();            // TSPoint without underscore
        tft.drawPixel(p.x, p.y, WHITE);      // coordinates identical
    }
}
```

---

## Using touch with Adafruit_GFX buttons

`Adafruit_GFX_Button` works unchanged. The `btn.contains(p.x, p.y)` call
receives virtual display pixel coordinates directly:

```cpp
#include <Adafruit_GFX.h>
#include <touch/RemoteTouchScreen.h>

RemoteTouchScreen ts(transport);
Adafruit_GFX_Button btn;

void setup() {
    ts.begin();
    btn.initButton(&display, 120, 200, 100, 40, WHITE, BLUE, WHITE, "GO", 2);
    btn.drawButton();
    display.flush();
}

void loop() {
    TSPoint p = ts.getPoint();
    bool touching = (p.z > RemoteTouchScreen::MINPRESSURE);
    btn.press(touching && btn.contains(p.x, p.y));

    if (btn.justPressed()) {
        // button was tapped
    }
    if (btn.justReleased()) {
        // finger lifted
    }
}
```

---

## Touch with both BLE and WiFi

If your sketch supports both transports, create one `RemoteTouchScreen` per
transport and call `begin()` on whichever is active:

```cpp
RemoteTouchScreen bleTouchScreen(bleTransport);
RemoteTouchScreen wifiTouchScreen(wifiTransport);

// When BLE connects:
bleTouchScreen.begin();

// When WiFi connects:
wifiTouchScreen.begin();

// In loop() — read from whichever is active
RemoteTouchScreen &ts = bleConnected ? bleTouchScreen : wifiTouchScreen;
TSPoint p = ts.getPoint();
```

---

## Notes

**Only one `begin()` per connection.** Calling `begin()` multiple times sends
multiple `TOUCH_BEGIN` frames to the iPhone. Call `end()` before `begin()` if
you need to change the mode or throttle, or use `setDelay()` to update the
throttle without restarting.

**Touch stops on disconnect.** When BLE or WiFi disconnects, the iPhone stops
sending touch events automatically. The last `_point` value is held until
`begin()` is called again on the new connection, or until `end()` clears it.
Best practice: call `ts.end()` in your disconnect handler, or at minimum
check `transport.canSend()` before acting on `p.z > MINPRESSURE`.

**No touch before display init.** Call `display.begin(w, h)` before
`ts.begin()`. The iPhone needs to know the display dimensions to map touch
coordinates correctly.