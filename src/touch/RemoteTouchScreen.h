#pragma once
#include <Arduino.h>
#include <freertos/portmacro.h>
#include "../GraphicsTransport.h"
#include "../GraphicsProtocol.h"
#include "../Protocol.h"

// -----------------------------------------------------------------------------
//  RemoteTouchScreen — drop-in replacement for Adafruit_TouchScreen /
//                      Adafruit_FT6206, using iPhone touch over BLE or WiFi.
//
//  Two usage patterns:
//
//  1. STANDARD — current position only (UI, games). No change from before:
//
//       TSPoint p = ts.getPoint();   // newest position, clears path queue
//       if (p.z > RemoteTouchScreen::MINPRESSURE) { /* act */ }
//
//  2. PATH — full path for drawing apps (TouchPaint):
//
//       while (ts.available()) {
//           TSPoint p = ts.getQueuedPoint();   // FIFO, oldest first
//           if (p.x != lastX || p.y != lastY) {
//               display.fillCircle(p.x, p.y, r, color);
//               lastX = p.x; lastY = p.y;
//           }
//       }
//       if (drew) display.flush();
//
//  Thread safety:
//    handleTouch() runs on the BLE/WiFi receive task (core 0).
//    getPoint() / getQueuedPoint() / available() run on loopTask (core 1).
//    A portMUX_TYPE spinlock protects all queue access — safe on dual-core.
// -----------------------------------------------------------------------------

struct TSPoint {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;

    TSPoint() = default;
    TSPoint(int16_t x_, int16_t y_, int16_t z_) : x(x_), y(y_), z(z_) {}
};

class RemoteTouchScreen
{
public:
    static constexpr int16_t  MINPRESSURE  = 1;
    static constexpr uint8_t  QUEUE_SIZE   = 16;   // handles ~256ms radio stall at 16ms intervals

    int16_t pressureThreshhold = MINPRESSURE;

    explicit RemoteTouchScreen(GraphicsTransport &transport);

    // ── Setup ─────────────────────────────────────────────────────────────────
    void begin(uint8_t  mode        = TOUCH_MODE_RESISTIVE,
               uint16_t interval_ms = TOUCH_MOVE_INTERVAL_MS_DEFAULT);
    void end();
    void setDelay(uint16_t interval_ms);

    // ── Standard API — current position, clears path queue ───────────────────
    // Returns newest touch point. Discards any queued path history.
    // Use for UI and games where current position is all that matters.
    TSPoint getPoint();

    // Returns true if finger is currently down.
    bool touched() const;

    // ── Path API — ordered history for drawing apps ───────────────────────────
    // Number of queued points waiting to be read (FIFO order).
    uint8_t available() const;

    // Pop and return the oldest queued point (FIFO).
    // Only valid when available() > 0.
    TSPoint getQueuedPoint();

    // ── Diagnostics ───────────────────────────────────────────────────────────
    // Number of points dropped due to queue full (radio stall indicator).
    uint16_t queueOverflows() const;
    void     resetOverflows();

    // ── Internal ──────────────────────────────────────────────────────────────
    void handleTouch(uint8_t cmd, int16_t x, int16_t y, uint8_t z);

private:
    GraphicsTransport  &_transport;
    bool                _active = false;

    // Current point — always newest, updated on every touch event
    TSPoint             _current;

    // Path queue — FIFO ring buffer, protected by _mux
    TSPoint             _queue[QUEUE_SIZE];
    volatile uint8_t    _qHead      = 0;   // write index (handleTouch)
    volatile uint8_t    _qTail      = 0;   // read index  (getQueuedPoint)
    volatile uint16_t   _overflows  = 0;

    portMUX_TYPE        _mux = portMUX_INITIALIZER_UNLOCKED;

    void enqueue(const TSPoint &p);
    void clearQueue();
    void sendCommand(uint8_t cmd, const void *payload, uint16_t payloadLen);
};