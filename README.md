# ESP32PhoneDisplay

Use an iPhone as a wireless display for your ESP32 project. Draw graphics, receive touch input, and build interactive UIs — all over BLE or WiFi, with no hardware display required.

The library speaks directly to the **RemoteGraphics iOS app** (available on the App Store). The ESP32 sends compact binary drawing commands; the iPhone renders them full-screen in real time.

---

## Features

- **Adafruit GFX–compatible API** — pixels, lines, rectangles, circles, triangles, text, bitmaps
- **BLE and WiFi transports** — BLE for zero-config simplicity, WiFi for high throughput
- **DualTransport** — advertise BLE and WiFi simultaneously, switch seamlessly
- **RemoteTouchScreen** — Adafruit_TouchScreen–compatible touch input via back-channel
- **Compat layer** — drop-in replacement for Adafruit_TFTLCD sketches with minimal changes
- **Connection interval control** — tune BLE latency for games and real-time applications
- **Diagnostic counters** — back-channel parser stats for debugging

---

## Requirements

### Hardware
- ESP32 or ESP32-S3 (tested on Heltec WiFi LoRa 32 V3)
- iPhone running the RemoteGraphics iOS app

### Arduino Libraries
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — for `BleTransport` and `DualTransport`
- [AsyncTCP](https://github.com/me-no-dev/AsyncTCP) — for `WiFiTransport` and `DualTransport`
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) — for font types and compat layer

Install all three via the Arduino Library Manager or PlatformIO before using this library.

---

## Quick Start

### BLE — Minimal Example

```cpp
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>

BleTransport      transport;
ESP32PhoneDisplay display(transport);

void setup() {
    transport.onSubscribed([](bool ready) {
        if (ready) {
            display.begin(240, 320);
            display.clear(0x0000);
            display.setCursor(20, 140);
            display.setTextColor(0xFFFF);
            display.setTextSize(3);
            display.print("Hello iPhone!");
            display.flush();
        }
    });
    transport.begin();
}

void loop() {}
```

Open the RemoteGraphics app on your iPhone, tap **BLE**, select **ESP32-Display** — your message appears instantly.

### WiFi Example

```cpp
#include <ESP32PhoneDisplay.h>
#include <transport/WiFiTransport.h>

WiFiTransport     transport("your_ssid", "your_password");
ESP32PhoneDisplay display(transport);

void setup() {
    transport.onConnected([]() {
        display.begin(240, 320);
        display.clear(0x001F);   // blue
        display.flush();
    });
    transport.begin();
}

void loop() {}
```

---

## Transport Selection

| Transport | Use when |
|-----------|----------|
| `BleTransport` | Simple setup, no WiFi credentials, up to ~1300 fillRect/s |
| `WiFiTransport` | Maximum throughput (~5500 fillRect/s), RTT monitoring |
| `DualTransport` | Best of both — iPhone chooses BLE or WiFi at connect time |

### Performance Reference

| Mode | fillRect/s | KB/s | RTT avg |
|------|-----------|------|---------|
| BLE only | ~1300 | ~23 | — |
| WiFi + BLE coexist | ~3500 | ~65 | ~80ms |
| WiFi only (`setPowerSave(false)`) | ~5500 | ~100 | ~35ms |

---

## API Reference

### ESP32PhoneDisplay

```cpp
ESP32PhoneDisplay display(transport);
```

#### Setup

```cpp
void begin(uint16_t w, uint16_t h);   // call once after transport connects
void clear(Color color = 0x0000);     // fill screen, reset cursor
void flush();   // send GFX_CMD_FLUSH to phone + force immediate BLE send
void setRotation(uint8_t r);          // 0–3 (0 = portrait)
void invertDisplay(bool invert);
uint16_t width();
uint16_t height();
```

#### Drawing

```cpp
// Pixels & lines
void drawPixel(int16_t x, int16_t y, Color color);
void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Color color);
void drawFastHLine(int16_t x, int16_t y, int16_t w, Color color);
void drawFastVLine(int16_t x, int16_t y, int16_t h, Color color);

// Rectangles
void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color);
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color);
void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, Color color);
void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, Color color);

// Circles & triangles
void drawCircle(int16_t x, int16_t y, int16_t r, Color color);
void fillCircle(int16_t x, int16_t y, int16_t r, Color color);
void drawTriangle(...);
void fillTriangle(...);

// Bitmaps (1-bit, Adafruit_GFX compatible)
void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                int16_t w, int16_t h, Color fg);
void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                int16_t w, int16_t h, Color fg, Color bg);
```

#### Text

Compatible with Adafruit_GFX text API. `print()` and `println()` work normally.

```cpp
void setCursor(int16_t x, int16_t y);
void setTextColor(Color color);
void setTextColor(Color fg, Color bg);
void setTextSize(uint8_t size);
void setTextWrap(bool wrap);
void setFont(const GFXfont *font);
void cp437(bool enable = true);
```

#### Colors

Colors are 16-bit RGB565, same as Adafruit_GFX:

```cpp
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
```

---

### BleTransport

```cpp
#include <transport/BleTransport.h>

BleTransport transport;
```

#### Setup

```cpp
void begin();                            // start BLE advertising

// Called when iPhone subscribes/unsubscribes to notifications
void onSubscribed(std::function<void(bool)> cb);

// Called when iPhone presses T1 (key='1') or T2 (key='2') in the app
void onKey(std::function<void(uint8_t key)> cb);
```

#### Connection Interval

BLE connection interval controls how often data is exchanged. Lower = more responsive, more power. iOS minimum is 15ms. Default is iOS-negotiated (~25ms).

```cpp
// Set before begin() — applied 1.5s after iPhone connects (iOS settling time)
void setConnectionInterval(uint16_t minMs, uint16_t maxMs);

// Renegotiate on active connection
void updateConnectionInterval(uint16_t minMs, uint16_t maxMs);

// Query actual negotiated interval (0 if not yet known)
float connIntervalMs() const;

// Callback fired when iOS accepts or changes the interval
void onConnInterval(std::function<void(float intervalMs)> cb);
```

Example — latency-sensitive game:
```cpp
transport.setConnectionInterval(15, 30);   // request 15–30ms, iOS decides
transport.onConnInterval([](float ms) {
    Serial.printf("BLE interval: %.1fms\n", ms);
});
```

#### Diagnostics

```cpp
BackChannelParser::Stats bcStats() const;   // returns cumulative counters
void resetBcStats();
```

`Stats` fields:

| Field | Description |
|-------|-------------|
| `key1`, `key2` | T1/T2 button presses received |
| `touch` | Touch events received |
| `pong` | Ping responses received |
| `syncErrors` | Bytes discarded waiting for frame start |
| `overruns` | Buffer overrun resets |
| `invalidFrames` | Frames with invalid length field |
| `unknownCmds` | Unrecognised command bytes |

---

### WiFiTransport

```cpp
#include <transport/WiFiTransport.h>

WiFiTransport transport("ssid", "password");
WiFiTransport transport("ssid", "password", "hostname", 9000);   // full
```

#### Setup

```cpp
void begin();

// Optional SoftAP fallback if STA connection fails
void setSoftAP(const char *apSsid, const char *apPassword,
               uint32_t staTimeoutMs = 15000);

// Callbacks
void onConnected(std::function<void()> cb);
void onDisconnected(std::function<void()> cb);
void onKey(std::function<void(uint8_t key)> cb);

// Status
bool isConnected() const;
bool isInAPMode() const;

// Power management
// setPowerSave(false) disables WiFi modem sleep for maximum throughput
void setPowerSave(bool enable);   // default true
```

#### Diagnostics

```cpp
BackChannelParser::Stats bcStats() const;
void resetBcStats();
```

Same `Stats` struct as `BleTransport`. See BleTransport diagnostics section.

#### RTT Statistics (WiFi only)

WiFiTransport sends periodic pings and measures round-trip time:

```cpp
uint32_t rttLast()  const;   // ms
uint32_t rttMin()   const;
uint32_t rttMax()   const;
uint32_t rttAvg()   const;
uint32_t rttCount() const;
void     resetRttStats();
```

---

### DualTransport

Wraps `BleTransport` and `WiFiTransport`. Both advertise simultaneously. Whichever connects first becomes active. Switching between transports is seamless.

```cpp
#include <DualTransport.h>

DualTransport transport("ssid", "password");
DualTransport transport("ssid", "password", "hostname", 9000);   // full
```

```cpp
void begin();

// Callbacks
void onConnected(std::function<void()> cb);
void onDisconnected(std::function<void()> cb);
void onKey(std::function<void(uint8_t key)> cb);

// Status
bool        isBleActive()  const;
bool        isWifiActive() const;
const char* activeTransportName() const;   // "BLE", "WiFi", or "none"

// BLE connection interval (delegates to BleTransport)
void  setConnectionInterval(uint16_t minMs, uint16_t maxMs);
void  updateConnectionInterval(uint16_t minMs, uint16_t maxMs);
float connIntervalMs() const;
void  onConnInterval(std::function<void(float)> cb);

// WiFi RTT stats (delegates to WiFiTransport)
uint32_t rttLast() / rttMin() / rttMax() / rttAvg() / rttCount();
void     resetRttStats();

// Power management
// setPowerSave(false) shuts down BLE when WiFi connects — one-way, no BLE after.
void setPowerSave(bool enable);
```

---

### RemoteTouchScreen

Drop-in replacement for `Adafruit_TouchScreen` / `Adafruit_FT6206`. Receives touch from the iPhone's screen over the BLE or WiFi back-channel.

```cpp
#include <touch/RemoteTouchScreen.h>

RemoteTouchScreen ts(transport);
```

#### Setup

```cpp
// Call after transport connects (and after display.begin())
void begin(uint8_t mode = TOUCH_MODE_RESISTIVE,
           uint16_t interval_ms = 50);   // iPhone MOVE event rate in ms

void end();                               // disable touch
void setDelay(uint16_t interval_ms);      // change rate without reconnecting
```

#### Standard API — current position

Use this for games and UI where only the current finger position matters:

```cpp
TSPoint p = ts.getPoint();
if (p.z > RemoteTouchScreen::MINPRESSURE) {
    // p.x, p.y — virtual display coordinates
    // p.z — 128 (contact) or 0 (lifted)
}

bool touched  = ts.touched();
bool active   = ts.isActive();   // false if begin() called before transport connected
```

`getPoint()` always returns the most recent position and clears the path queue.

#### Path API — ordered history

Use this for drawing apps where every point in the finger's path matters:

```cpp
while (ts.available()) {
    TSPoint p = ts.getQueuedPoint();   // FIFO — oldest first
    if (p.z > RemoteTouchScreen::MINPRESSURE) {
        display.fillCircle(p.x, p.y, radius, color);
    }
}
display.flush();
```

#### Diagnostics

```cpp
uint8_t  available() const;        // queued point count
uint16_t queueOverflows() const;   // points dropped (radio stall indicator)
void     resetOverflows();
```

The queue holds 16 points, covering ~256ms of stall at 16ms intervals. A non-zero overflow count indicates a radio stall long enough to lose touch data.

---

### ESP32PhoneDisplay_Compat

Drop-in replacement for `Adafruit_TFTLCD`. Allows porting existing sketch code with minimal changes.

```cpp
#include <ESP32PhoneDisplay_Compat.h>

ESP32PhoneDisplay_Compat tft(transport, 240, 320);

tft.begin();            // instead of tft.reset() + tft.begin(id)
tft.fillScreen(BLACK);  // same as Adafruit_TFTLCD
tft.flush();            // call at end of frame — no equivalent in hardware driver
```

Also exposes `getTextBounds()` for text layout calculations.

---

### Custom Transport

Implement `GraphicsTransport` to use any channel:

```cpp
#include <GraphicsTransport.h>

class MyTransport : public GraphicsTransport {
public:
    void send(const uint8_t *data, uint16_t len) override {
        // write bytes to your channel
    }
    bool canSend() const override {
        return _connected;
    }
    // Optional: void flush() override { ... }
    // Optional: void reset() override { ... }
    // Optional: void onTouch(...) override { ... }
};
```

---

## Touch Input — Important Notes

### Logging from BLE Callbacks

BLE callbacks (`onKey`, `onSubscribed`, `onConnInterval`) run on core 0 (the NimBLE host task). `Serial.printf()` is unreliable from this context because the ESP32's UART TX interrupt can be masked by NimBLE internal critical sections and flash cache operations.

**Espressif's documented solution** for logging from BLE callbacks is `ESP_DRAM_LOGx`:

```cpp
#include "esp_log.h"

transport.onKey([](uint8_t key) {
    ESP_DRAM_LOGI(DRAM_STR("APP"), "Key: %c", key);   // reliable
    // Serial.printf("Key: %c\n", key);                // unreliable
});
```

For app code that only prints from `loop()`, plain `Serial.printf()` is safe — `loop()` runs on core 1 which is not affected by NimBLE operations.

**Alternative pattern** — defer prints to `loop()` using a volatile flag:

```cpp
static volatile int _keyMsg = 0;   // set on core 0, printed on core 1

transport.onKey([](uint8_t key) {
    if (key == '1') _keyMsg = 1;
});

void loop() {
    if (_keyMsg) { Serial.println("Key 1"); _keyMsg = 0; }
    // ...
}
```

See [Espressif logging documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/log.html) and [flash concurrency constraints](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/spi_flash/spi_flash_concurrency.html) for full details.

### Touch Interval

The iPhone throttles touch MOVE events to `interval_ms` (default 50ms, 20Hz). Lower values give more responsive touch at the cost of more BLE/WiFi back-channel traffic.

```cpp
ts.begin(TOUCH_MODE_RESISTIVE, 16);   // ~60Hz — good for games
ts.begin(TOUCH_MODE_RESISTIVE, 50);   // ~20Hz — good for UI (default)
```

### BLE Connection Interval and Touch Responsiveness

At the default BLE connection interval (~25ms), touch events arrive at most once per interval. For latency-sensitive applications, request a shorter interval:

```cpp
transport.setConnectionInterval(15, 30);   // 15–30ms
```

iOS accepts as low as 15ms for foreground apps. The request is made 1.5 seconds after connect to allow iOS settling time.

---

## Frame Rate and Game Loops

BLE limits throughput to one notification per connection interval (~25ms = ~40fps). Each `send()` call blocks until the drain task has capacity. This naturally gates your loop to the BLE frame rate.

For fixed-rate game loops, use a millis-based frame budget:

```cpp
void loop() {
    uint32_t frameStart = millis();

    // game logic and drawing here
    tft.flush();

    // spin-wait for remainder — delay(1) yields to BLE stack
    while (millis() - frameStart < 33) delay(1);   // 33ms = ~30fps
}
```

`delay(1)` inside the spin-wait is essential — it yields to FreeRTOS, allowing the BLE drain task and UART TX ISR to run.

---

## Examples

| Example | Transport | Description |
|---------|-----------|-------------|
| `BLE_HelloWorld` | BLE | Minimal text display |
| `BLE_TouchButtons` | BLE | Touch-driven button UI |
| `BLE_TouchPaint` | BLE | Finger drawing with path API |
| `BLE_Telemetry` | BLE | Real-time sensor display |
| `WiFi_HelloWorld` | WiFi | Minimal WiFi display |
| `DualTransport_TouchPaint` | Dual | TouchPaint with auto transport switching |
| `BandwidthTest` | Dual | Measures fillRect/s and KB/s throughput |
| `Breakout` | BLE | Classic Breakout — ported from Adafruit_TFTLCD |
| `Breakout_II` | BLE | Fixed 30fps Breakout with speed multiplier |
| `CompatLayer` | BLE | Demonstrates ESP32PhoneDisplay_Compat API |
| `CustomTransport` | Custom | Shows how to implement GraphicsTransport |
| `SerialTest` | — | Protocol smoke test via Serial |

---

## Installation

### PlatformIO (recommended)

Add to `platformio.ini`:

```ini
lib_deps =
    h2zero/NimBLE-Arduino @ ^2.0.0
    me-no-dev/AsyncTCP @ ^1.1.1
    adafruit/Adafruit GFX Library @ ^1.11.0
    # ESP32PhoneDisplay from local path or GitHub
```

### Arduino IDE

1. Download this repository as a ZIP
2. Sketch → Include Library → Add .ZIP Library
3. Install `NimBLE-Arduino`, `AsyncTCP`, and `Adafruit GFX Library` from Library Manager

---

## iOS App

Install **RemoteGraphics** from the App Store. The app supports:

- BLE connection — tap BLE, select your device
- WiFi connection — tap WiFi, enter hostname or IP
- T1 / T2 buttons — sent as `onKey` callbacks to your sketch
- Full-screen touch — coordinates mapped to virtual display space
- Auto-reconnect

---

## Architecture

```
sketch (loop, core 1)
    │
    ├── ESP32PhoneDisplay / ESP32PhoneDisplay_Compat
    │       encodes GFX commands as compact binary
    │
    ├── GraphicsTransport (abstract)
    │       ├── BleTransport    → NimBLE notifications (core 0 drain task)
    │       ├── WiFiTransport   → AsyncTCP writes (core 0 background task)
    │       └── DualTransport   → wraps both, active one receives sends
    │
    └── RemoteTouchScreen
            registers onTouch callback with transport
            queues touch events from iPhone back-channel
            app reads via getPoint() or getQueuedPoint()
```

Back-channel (iPhone → ESP32) carries:
- Touch DOWN / MOVE / UP events
- T1 / T2 key presses
- Pong responses (WiFi heartbeat)

---

## License

MIT License — see LICENSE file.

Original Breakout example by Enrique Albertos (public domain).