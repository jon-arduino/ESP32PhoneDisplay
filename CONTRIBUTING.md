# Contributing

## Reporting Issues

Please include:
- ESP32 board and Arduino core version
- NimBLE-Arduino version
- iOS app version
- Transport used (BLE / WiFi / Dual)
- Minimal sketch that reproduces the issue
- Serial output if relevant

## Development Notes

### Threading Model

The library uses two cores:

- **Core 1** — Arduino `loop()`, all drawing commands, `RemoteTouchScreen::getPoint()`
- **Core 0** — NimBLE host task, BLE drain task, WiFi background task, all transport callbacks

### Serial Logging from Callbacks

BLE callbacks run on core 0. `Serial.printf()` is unreliable from this context due to NimBLE critical sections masking the UART TX interrupt (see ESP-IDF flash concurrency documentation). Use `ESP_DRAM_LOGx` for reliable logging from callbacks, or defer prints to `loop()` via volatile flags.

### Adding a New Transport

Subclass `GraphicsTransport` and implement:

```cpp
void send(const uint8_t *data, uint16_t len) override;
bool canSend() const override;
```

Optionally implement `flush()`, `reset()`, and `onTouch()`. See `examples/CustomTransport` for a complete example.

### Protocol

GFX command opcodes and payload structures are in `src/GraphicsProtocol.h`. Back-channel (iPhone → ESP32) framing is in `src/Protocol.h`. The iOS app must implement the same protocol.

### Testing

Flash `examples/BandwidthTest` to measure transport throughput. Flash `examples/SerialTest` to verify protocol encoding without needing the iOS app.

## Code Style

- C++11
- 4-space indentation
- `snake_case` for member variables with `_` prefix
- `camelCase` for methods
- All public API documented in header comments