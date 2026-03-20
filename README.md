# ESP32PhoneDisplay

Use your iPhone as a wireless display for ESP32 projects. Replace your TFT display with your phone — over BLE or WiFi — with a single line change.

## What it does

ESP32PhoneDisplay lets any ESP32 sketch that uses the Adafruit GFX library render its output on an iPhone instead of (or in addition to) a physical TFT. The library encodes every drawing operation as a compact binary command and streams it to the **ESP32PhoneDisplay iPhone app** over BLE or WiFi.

- Full Adafruit GFX API — `drawCircle()`, `fillRect()`, `print()`, everything
- Efficient protocol — text, circles, and shapes send single commands, not pixel streams
- Two transports included — BLE (NimBLE-Arduino) and WiFi (AsyncTCP)
- Bring your own transport — implement `GraphicsTransport` for LoRa, serial, or anything else
- Touch input back-channel — iPhone touch events arrive as `TSPoint`, compatible with Adafruit_TouchScreen

## Quick start

### Option A — Drop-in replacement (easiest porting path)

If your existing sketch uses `Adafruit_GFX*` pointers or passes the display to libraries, use `ESP32PhoneDisplay_Compat`. It subclasses `Adafruit_GFX` so it works everywhere an `Adafruit_GFX*` is expected.

```cpp
// Before:
#include <Adafruit_ST7735.h>
Adafruit_ST7735 tft(CS, DC, RST);

// After (one line change):
#include <ESP32PhoneDisplay_Compat.h>
ESP32PhoneDisplay_Compat tft;   // BLE by default

void setup() {
    tft.begin();
    tft.fillScreen(BLACK);
    tft.setCursor(10, 10);
    tft.setTextColor(WHITE);
    tft.print("Hello iPhone!");
}
```

### Option B — Native driver (best performance)

If you control your own code and don't need `Adafruit_GFX*` pointer compatibility, use the native `ESP32PhoneDisplay` class. Every GFX call — including `drawCircle()`, `drawChar()`, rounded rects — sends a single compact command.

```cpp
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>

BleTransport   transport;
ESP32PhoneDisplay display(transport);

void setup() {
    transport.begin();
    display.begin(240, 320);
    display.clear(0x0000);
    display.setCursor(10, 10);
    display.setTextColor(0xFFFF);
    display.print("Hello iPhone!");
    display.flush();
}

void loop() {
    // your app here
}
```

## Installation

### Arduino IDE
Search for **ESP32PhoneDisplay** in the Library Manager (Tools → Manage Libraries).

### PlatformIO
```ini
lib_deps = https://github.com/jonlotz/ESP32PhoneDisplay
```

### Manual
Download the ZIP from GitHub and use Sketch → Include Library → Add .ZIP Library.

## Dependencies

| Library | Purpose |
|---------|---------|
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | Base class for compat layer, font support |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) >= 2.3.9 | BLE transport |
| [AsyncTCP](https://github.com/mathieucarbou/AsyncTCP) >= 3.4 | WiFi transport |

If you implement your own transport, only Adafruit GFX Library is required.

## iPhone app

The companion iPhone app receives the command stream and renders it on screen.

- [Download from the App Store](#) *(coming soon)*
- [Build from source](https://github.com/jonlotz/ESP32PhoneDisplay-iOS)

## Transports

### BLE (NimBLE-Arduino)

```cpp
#include <transport/BleTransport.h>
BleTransport transport;
transport.begin();                    // starts advertising as "ESP32-Display"
transport.setSendTimeout(500);        // ms to wait if buffer full (default: block)
```

BLE uses Nordic UART Service (NUS). The iPhone app connects as a BLE central. Suitable for low-to-medium bandwidth UI. Stays connected when iPhone screen locks.

### WiFi (AsyncTCP + mDNS)

```cpp
#include <transport/WiFiTransport.h>
WiFiTransport transport("MySSID", "MyPassword", "esp32-display");
transport.begin();                    // connects to WiFi, advertises via mDNS
transport.setSoftAP("ESP32-Display", "display123"); // optional: SoftAP fallback
```

WiFi uses TCP over mDNS discovery. Higher bandwidth — suitable for bitmap-heavy UIs. The iPhone app discovers the ESP32 via Bonjour without needing an IP address.

### Custom transport

Implement `GraphicsTransport` to use any channel:

```cpp
#include <GraphicsTransport.h>

class MyLoRaTransport : public GraphicsTransport {
public:
    void send(const uint8_t* data, uint16_t len) override {
        lora.transmit(data, len);
    }
    bool canSend() const override {
        return lora.isReady();
    }
    void flush() override {}   // optional: no-op if you send immediately
};
```

See the [CustomTransport example](examples/CustomTransport/CustomTransport.ino) and [transport documentation](docs/transport.md).

## Touch input

```cpp
#include <touch/RemoteTouchScreen.h>

RemoteTouchScreen ts;

// Register with your transport:
transport.onTouch([](uint8_t cmd, int16_t x, int16_t y) {
    ts.handleTouch(cmd, x, y);
});

// In loop() — identical to Adafruit_TouchScreen:
TSPoint p = ts.getPoint();
if (p.z > RemoteTouchScreen::MINPRESSURE) {
    // finger is down at p.x, p.y
}
```

## Examples

| Example | Description |
|---------|-------------|
| [BLE_HelloWorld](examples/BLE_HelloWorld) | Minimal BLE sketch — connect and draw |
| [WiFi_HelloWorld](examples/WiFi_HelloWorld) | Minimal WiFi sketch — connect and draw |
| [BLE_Telemetry](examples/BLE_Telemetry) | Sensor data display with auto-updating values |
| [CompatLayer](examples/CompatLayer) | Drop-in replacement demo for existing Adafruit_GFX sketches |
| [CustomTransport](examples/CustomTransport) | Implement your own transport channel |

## Protocol

The wire protocol is documented in [docs/protocol.md](docs/protocol.md). If you want to build your own receiver (Android, desktop, etc.), everything you need is there.

## Porting from Adafruit_GFX

See [docs/porting.md](docs/porting.md) for a step-by-step migration guide.

## Contributing

Issues and pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting.

## License

MIT — see [LICENSE](LICENSE).
