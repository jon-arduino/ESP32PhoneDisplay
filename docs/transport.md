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

Uses Nordic UART Service (NUS) over NimBLE-Arduino. An `xStreamBuffer`
(8KB) decouples the graphics thread from the BLE radio. A drain task
on core 0 sends notifications as fast as the BLE connection allows.

BLE connection interval is controlled by iOS (typically 15–45ms) and
limits throughput and latency. For latency-sensitive apps (touch paint,
games), WiFi is recommended.

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