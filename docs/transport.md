# Implementing a Custom Transport

ESP32PhoneDisplay separates the graphics API from the communication channel via the `GraphicsTransport` abstract base class. You can use any transport channel by implementing four methods.

## The interface

```cpp
class GraphicsTransport {
public:
    virtual void send(const uint8_t *data, uint16_t len) = 0;  // required
    virtual bool canSend() const = 0;                          // required
    virtual void flush() {}                                    // optional
    virtual void reset() {}                                    // optional
};
```

## Minimal implementation

```cpp
#include <GraphicsTransport.h>

class MyTransport : public GraphicsTransport {
public:
    void send(const uint8_t *data, uint16_t len) override {
        // deliver bytes to your channel
    }

    bool canSend() const override {
        // return true when ready to send
        return true;
    }
};
```

## Design guidelines

**`send()` should return quickly.** The GFX task calls `send()` on every drawing operation. If `send()` blocks for long periods (e.g. waiting for a slow radio), drawing will stall. Buffer internally and drain in a background task if your channel is slow.

**`canSend()` is checked before every command.** If it returns false, the command is silently dropped. Use this to avoid sending during connection setup, before the receiver is ready, etc.

**`flush()` marks frame boundaries.** Called when the sketch calls `display.flush()`. If your channel buffers data, drain the buffer here. If you send immediately in `send()`, leave `flush()` as a no-op.

**`reset()` discards buffered data.** Called on reconnect to clear stale commands that were queued before the connection dropped. If you don't buffer, leave it as a no-op.

## LoRa example

```cpp
#include <GraphicsTransport.h>
#include <LoRa.h>

class LoRaTransport : public GraphicsTransport {
public:
    void begin() {
        LoRa.begin(915E6);
        _ready = true;
    }

    void send(const uint8_t *data, uint16_t len) override {
        // LoRa packets have a max size — buffer and send in chunks
        for (uint16_t i = 0; i < len; i++) {
            _buf[_bufLen++] = data[i];
            if (_bufLen >= MAX_PKT) flush();
        }
    }

    bool canSend() const override { return _ready; }

    void flush() override {
        if (_bufLen == 0) return;
        LoRa.beginPacket();
        LoRa.write(_buf, _bufLen);
        LoRa.endPacket();
        _bufLen = 0;
    }

    void reset() override { _bufLen = 0; }

private:
    static constexpr uint8_t MAX_PKT = 255;
    uint8_t  _buf[MAX_PKT];
    uint8_t  _bufLen = 0;
    bool     _ready  = false;
};
```

## Thread safety

If `send()` may be called from multiple FreeRTOS tasks simultaneously, protect your internal buffer with a mutex. The included `BleTransport` uses an `xStreamBuffer` (single producer, single consumer) and `WiFiTransport` uses a mutex for this reason.

## Receiver

Your custom transport needs a matching receiver on the iPhone side (or any platform). The wire protocol is documented in [protocol.md](protocol.md) — it's a simple length-prefixed binary format that's straightforward to implement in any language.
