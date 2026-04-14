#pragma once

#include <Arduino.h>
#include <functional>

// -----------------------------------------------------------------------------
//  GraphicsTransport — abstract base class for all display transports
//
//  Implement this to use any wireless or wired channel with ESP32PhoneDisplay.
//  The library ships with BleTransport and WiFiTransport. For custom channels
//  (LoRa, serial, USB CDC, etc.) subclass this and pass it to ESP32PhoneDisplay.
//
//  Threading:
//    send() may be called from loop() or any FreeRTOS task. Implementations
//    must be thread-safe if the underlying channel requires it.
//
//  Usage:
//    class MyTransport : public GraphicsTransport {
//    public:
//        void  send(const uint8_t* data, uint16_t len) override { ... }
//        bool  canSend() const override { return connected; }
//    };
// -----------------------------------------------------------------------------
class GraphicsTransport
{
public:
    virtual ~GraphicsTransport() = default;

    // Send raw bytes. Called for every encoded GFX command.
    // Implementations should buffer internally if needed — callers assume
    // send() returns quickly and do not retry on failure.
    virtual void send(const uint8_t *data, uint16_t len) = 0;

    // Returns true when the transport is ready to accept data.
    // ESP32PhoneDisplay checks this before sending — if false, commands
    // are silently dropped. Implementations should return false when
    // not connected, before first client connection, etc.
    virtual bool canSend() const = 0;

    // Optional: flush any internally buffered data.
    // Called at explicit frame boundaries (ESP32PhoneDisplay::flush()).
    // No-op by default — only needed if your implementation buffers.
    virtual void flush() {}

    // Optional: discard any buffered bytes without sending.
    // Called on reconnect to clear stale data.
    virtual void reset() {}

    // Optional: register a callback for touch events from the iPhone.
    // cmd: BC_CMD_TOUCH_DOWN (0x10), TOUCH_MOVE (0x11), or TOUCH_UP (0x12)
    // x, y: virtual display coordinates (little-endian, pre-mapped to pixels)
    // z: BC_TOUCH_Z_CONTACT (128) on DOWN/MOVE, BC_TOUCH_Z_NONE (0) on UP
    //
    // Called from the BLE or WiFi receive task — not from loop().
    // Default implementation is a no-op for transports that don't support
    // back-channel touch. RemoteTouchScreen calls this in begin().
    virtual void onTouch(std::function<void(uint8_t cmd,
                                            int16_t x,
                                            int16_t y,
                                            uint8_t z)> cb) {}
};