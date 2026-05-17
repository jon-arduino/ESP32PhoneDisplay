# Transport Architecture

ESP32PhoneDisplay separates the graphics API from the communication channel
via the `GraphicsTransport` abstract base class. Two transports are provided:
`BleTransport` (BLE/Nordic UART) and `WiFiTransport` (AsyncTCP/mDNS).
You can also implement a custom transport for any channel.

---

## WiFiTransport

### Architecture

`WiFiTransport` uses an SPSC (single producer, single consumer) ring buffer
with a dedicated FreeRTOS transport task. This gives clean separation between
the graphics thread and the TCP socket:

```
send()  → writes framed packet [lenLo][lenHi][data] into ring buffer
flush() → notifies transport task to drain ring immediately
transport task (core 0):
    - drains ring, sending complete packets only
    - sends ping between packets (never mid-packet)
    - handles auto-flush if bytes idle > 100ms
```

No mutex is needed — the transport task is the sole writer to the TCP socket.

### Payload size limit

**Maximum single `send()` payload: 8192 bytes (8KB)**

This fits a 64×64 RGB565 bitmap (8192 bytes) with the ring buffer (16KB)
still having room for queued commands.

If a payload exceeds 8KB, `send()` logs a warning and drops it:
```
[WiFi] send() payload too large (N bytes, max 8192) — dropped
```

Callers with larger payloads must chunk their data. A future helper
`drawBitmapChunked()` will handle this automatically.

**Practical bitmap limits at RGB565 (2 bytes/pixel):**

| Size    | Bytes  | Fits? |
|---------|--------|-------|
| 32×32   | 2,048  | ✓     |
| 64×64   | 8,192  | ✓     |
| 64×65   | 8,320  | ✗     |
| 128×128 | 32,768 | ✗     |
| 240×320 | 153,600| ✗     |

For larger bitmaps, chunk by row or tile and call `drawBitmap()` multiple times.

### Ping/pong heartbeat

WiFiTransport sends a ping to the iPhone every 3 seconds (configurable).
If no pong is received within 9 seconds, the connection is dropped and
the iPhone can reconnect. This detects silent TCP disconnects.

RTT statistics are tracked by the `PingPong` class and exposed via:
```cpp
transport.rttLast();   // most recent RTT in ms
transport.rttMin();    // minimum RTT seen
transport.rttAvg();    // running average
transport.rttMax();    // maximum RTT seen
transport.rttCount();  // number of pongs received
transport.resetRttStats();
```

Callbacks:
```cpp
transport.onRtt([](uint32_t rttMs) {
    Serial.printf("RTT: %u ms\n", rttMs);
});
transport.onFirstPong([]() {
    Serial.println("iPhone confirmed responsive");
});
```

### Nagle algorithm

`WiFiTransport` disables the Nagle algorithm (`TCP_NODELAY`) on connect.
This ensures small GFX commands are sent immediately rather than waiting
for TCP ACKs. Critical for low-latency touch and real-time display updates.

### Configuration

```cpp
WiFiTransport transport("MySSID", "MyPassword", "esp32-display");

// Optional SoftAP fallback if home network unavailable
transport.setSoftAP("ESP32-Display", "display123", 15000);

// Adjust heartbeat (default: 3s interval, 9s timeout)
transport.setHeartbeat(5000, 15000);

transport.begin();
```

### Nothing required in loop()

The transport task manages heartbeat and auto-flush automatically.
Explicit `flush()` is recommended at frame boundaries but not required.

---

## BleTransport

Uses Nordic UART Service (NUS) over NimBLE-Arduino.

### Architecture

An `xStreamBuffer` (8KB) decouples the graphics thread from the BLE radio.
A drain task on core 0 pulls commands from the buffer and sends BLE
notifications as fast as the connection allows:

```
send()  → writes framed packet into 8KB stream buffer (never blocks on radio)
flush() → sends GFX_CMD_FLUSH marker + wakes drain task for immediate send
drain task (core 0):
    - sleeps until notified by flush() or timer
    - drains stream buffer into BLE notifications (up to MTU per notification)
    - auto-flushes if bytes are idle in the buffer
```

### Auto-flush

The drain task auto-flushes buffered commands if they sit idle for more than
one connection interval. Calling `flush()` explicitly is recommended at frame
boundaries — it ensures deterministic delivery and keeps packet count
predictable — but is not strictly required for simple displays.

### Callbacks

All BLE callbacks fire on core 0 (the NimBLE host task). Never call display
functions, `Serial.print()`, or any blocking operation from a callback.
Set a volatile flag and act on it in `loop()` instead. See the examples and
the logging note in the main README for details.

```cpp
// Called when BLE connects. Use as fallback draw trigger if onDisplayAvailable
// doesn't fire first. A double-draw if both fire is harmless.
transport.onConnected([]() { ... });

// Called when BLE drops. Sets display offline immediately — on an abrupt drop,
// onDisplayAvailable(false) may not arrive from the app.
transport.onDisconnected([]() { ... });

// Called when iPhone presses T1 (key='1') or T2 (key='2') toolbar button.
transport.onKey([](uint8_t key) { ... });

// Called when iOS accepts or changes the BLE connection interval.
transport.onConnInterval([](float intervalMs) { ... });
```

### onRedrawRequest

```cpp
transport.onRedrawRequest([]() { ... });
```

The iPhone app sends `BC_CMD_REDRAW_REQUEST` in two situations:

**1. After BLE reconnect.**
The iPhone's framebuffer persists across a BLE disconnect — the display
shows stale content, not a blank screen. When BLE reconnects, any commands
the ESP32 sends are applied on top of that stale state, producing a garbled
display. The app sends `BC_CMD_REDRAW_REQUEST` to ask the ESP32 to rebuild
the full screen from scratch.

**2. When the app returns to the foreground.**
If the user switches to another app and back, BLE stays connected the whole
time but the app may not have rendered commands received while backgrounded.
The display state is unknown. The app sends `BC_CMD_REDRAW_REQUEST` on
foreground so the ESP32 can rebuild a coherent display — even though BLE
was never dropped.

`onConnected` alone cannot cover case 2. Register `onDisplayAvailable` and
`onRedrawRequest` together for complete coverage:

```cpp
transport.onDisplayAvailable([](bool available) {
    if (available) { _displayOffline = false; _redrawPending = true; }
    else             _displayOffline = true;
});
transport.onRedrawRequest([]() {
    _redrawPending = true;    // set flag — act in loop(), not here
});
// onConnected as fallback draw trigger, onDisconnected as safety net
transport.onConnected([]()    { _redrawPending = true; });
transport.onDisconnected([]() { _displayOffline = true; });
```

For static displays that never update, `onRedrawRequest` is still recommended
— without it a reconnect or app-switch leaves stale content on screen until
the next draw call.

### Connection interval

BLE connection interval controls how often data is exchanged. Lower interval
= more responsive, higher power draw. iOS minimum is 15ms. Default is
iOS-negotiated (~25ms).

```cpp
// Set before begin() — applied after iOS settling time on connect
transport.setConnectionInterval(15, 30);   // request 15–30ms

// Renegotiate on an active connection
transport.updateConnectionInterval(15, 30);

// Query current interval (0 if not yet known)
float ms = transport.connIntervalMs();
```

For latency-sensitive applications (games, touch paint), WiFi transport
gives significantly lower and more consistent latency than BLE.

### Diagnostics

```cpp
BackChannelParser::Stats s = transport.bcStats();
transport.resetBcStats();
```

See main README for `Stats` field descriptions.

---

## DualTransport

`DualTransport` (in `src/transport/DualTransport.h`) wraps both
`BleTransport` and `WiFiTransport`. Whichever transport the iPhone
connects via becomes active. Switching transports is seamless —
disconnect on one and connect on the other.

```cpp
#include <transport/DualTransport.h>

DualTransport transport("MySSID", "MyPassword", "esp32-display");
ESP32PhoneDisplay display(transport);

void setup() {
    transport.onConnected([]() {
        Serial.printf("Connected via %s\n", transport.activeTransportName());
        display.begin(240, 320);
    });
    transport.begin();
}
```

---

## Custom transport

Implement `GraphicsTransport` for any channel:

```cpp
#include <GraphicsTransport.h>

class MyTransport : public GraphicsTransport {
public:
    void send(const uint8_t *data, uint16_t len) override {
        // deliver bytes to your channel
        // must handle up to 8KB per call
    }

    bool canSend() const override {
        return _ready;
    }

    void flush() override {
        // drain internal buffer if any
    }

    void reset() override {
        // discard buffered data on disconnect
    }
};
```

### Design guidelines

**`send()` should return quickly.** Buffer internally and drain in a
background task if your channel is slow. Never block for more than a
few milliseconds.

**`canSend()` is checked before every command.** If false, the command
is silently dropped.

**`flush()` marks frame boundaries.** If you buffer, drain here.
If you send immediately in `send()`, leave as no-op.

**`reset()` discards buffered data.** Called on reconnect to clear
stale commands queued before the connection dropped.

### Thread safety

If `send()` may be called from multiple FreeRTOS tasks, protect your
buffer with a mutex or use an SPSC ring buffer (no mutex needed for
single producer/consumer). See `WiFiTransport` for an SPSC example.

### Wire protocol

The bytes passed to `send()` are complete framed GFX packets:
```
[0xA5][lenLo][lenHi][cmd][payload...]
```
Your transport delivers them intact to the iPhone. See [protocol.md](protocol.md)
for the full wire format.

---

## Choosing a transport

| Factor              | BLE              | WiFi             |
|---------------------|------------------|------------------|
| Setup               | Zero config      | SSID + password  |
| Latency             | 15–45ms          | 1–5ms            |
| Throughput          | ~20 KB/s         | ~500 KB/s        |
| Range               | ~10m             | ~50m             |
| Best for            | Simple displays  | Touch, animation |
| Away from network   | Always works     | Use SoftAP       |

---

## BLE vs WiFi — choosing a transport

Both transports deliver comparable performance for typical display applications
once configured correctly (connection interval set, `setNoDelay` active). The
choice comes down to your deployment context.

### BLE

**Benefits:**
- No network infrastructure needed — works anywhere, any location
- Simpler user experience — no credentials, no network joining
- Lower power consumption
- Automatic discovery — iPhone sees the device by name in the app

**Limitations:**
- Fixed connection interval (minimum 15ms, iOS negotiated) — caps throughput
- Shared 2.4GHz antenna with WiFi on ESP32 — interference when both active
- Range typically 10–30m line of sight

**Best for:** portable devices, field use, consumer products, any situation
where you don't control the network environment.

### WiFi

**Benefits:**
- Higher throughput ceiling — no fixed connection interval
- Works across a network — device can be anywhere on the LAN
- More familiar debugging tools (ping, Wireshark, etc.)
- SoftAP mode — direct iPhone→ESP32 connection without a router

**Limitations:**
- Requires WiFi credentials or SoftAP setup
- Higher power consumption
- Shared 2.4GHz antenna with BLE on ESP32
- TCP connection management adds complexity

**Best for:** fixed installations, lab/workshop use, high-throughput
applications, any situation where network infrastructure exists.

### DualTransport

Advertises both simultaneously — iPhone connects via whichever it chooses.
The active transport is reported via `activeTransportName()`. Auto-switches
when one disconnects and the other connects.

The 2.4GHz antenna sharing cost is real but manageable for most applications.
For demanding use cases requiring full performance from both radios
simultaneously, consider an ESP32 variant with an external antenna or a
module with dedicated antenna switching hardware.

### Performance in practice

With the library configured correctly, both transports feel comparable for
touch drawing and game input:

- **BLE** — set `transport.setConnectionInterval(15, 15)` for games and
  touch apps. Without this, iOS default (30–100ms) severely limits update rate.
- **WiFi** — `setNoDelay(true)` is set automatically. Auto-flush at 25ms.
  No equivalent of the connection interval bottleneck.

Neither transport requires manual flush rate tuning for typical use.

---

## Device naming

All transports advertise with a default name of **"ESP32-Display"** (BLE)
and **"esp32-display"** (WiFi mDNS). Call `setDeviceName()` before `begin()`
to override:

```cpp
transport.setDeviceName("PaintStation");
transport.begin();
```

This sets the BLE advertising name and WiFi mDNS hostname. The SoftAP SSID
(if used) is set separately via `setSoftAP()` — pass the same name if you
want them to match:

```cpp
transport.setDeviceName("PaintStation");
transport.setSoftAP("PaintStation", "mypassword");
transport.begin();
```

### Multi-device setups

Two devices advertising with identical names cause problems:

- **BLE** — both appear as identical entries in the app's device list. Users
  cannot tell them apart.
- **WiFi mDNS** — collision detection renames one unpredictably
  (e.g., "esp32-display-2").
- **SoftAP** — two APs with the same SSID appear as one network. iOS connects
  to the stronger signal — not user-controllable. Effectively broken.

For multi-device deployments, assign unique names. A simple approach is to
append the last bytes of the MAC address:

```cpp
uint8_t mac[6];
esp_read_mac(mac, ESP_MAC_WIFI_STA);
char name[32];
snprintf(name, sizeof(name), "ESP32-%02X%02X", mac[4], mac[5]);
transport.setDeviceName(name);
transport.setSoftAP(name, "mypassword");
transport.begin();
```

This gives each device a stable, unique, human-readable name (e.g.,
"ESP32-A1B2") derived from hardware — no manual configuration needed.