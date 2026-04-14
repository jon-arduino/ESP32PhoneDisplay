#pragma once
#include <Arduino.h>
#include "../GraphicsTransport.h"
#include "../GraphicsProtocol.h"
#include "../Protocol.h"

// -----------------------------------------------------------------------------
//  RemoteTouchScreen — drop-in replacement for Adafruit_TouchScreen and
//                      Adafruit_FT6206 (capacitive single-touch)
//
//  Emulates a physical touchscreen using iPhone touch input sent over the
//  existing BLE or WiFi back-channel. No extra wiring, no ADC pins needed.
//
//  Adafruit_TouchScreen (resistive) pattern — works unchanged:
//
//    TSPoint p = ts.getPoint();
//    if (p.z > RemoteTouchScreen::MINPRESSURE) {
//        btn.press(btn.contains(p.x, p.y));
//    } else {
//        btn.press(false);
//    }
//
//  Adafruit_FT6206 (capacitive) pattern — works unchanged:
//
//    if (ts.touched()) {
//        TSPoint p = ts.getPoint();
//        tft.drawPixel(p.x, p.y, WHITE);
//    }
//
//  Key differences from physical touchscreens:
//    - Coordinates are pre-mapped to virtual display pixels. No map() or
//      calibration constants needed. Remove them when porting.
//    - z is always BC_TOUCH_Z_CONTACT (128) when touching, 0 when not.
//      This exceeds any MINPRESSURE threshold used in Adafruit sketches.
//    - begin() sends TOUCH_BEGIN to the iPhone and wires callbacks
//      automatically. No manual callback registration needed.
//
//  Thread safety:
//    handleTouch() is called from the BLE or WiFi receive task.
//    getPoint() / touched() are called from loop().
//    The _point struct is written atomically on ESP32 (aligned 16-bit
//    fields) so no mutex is needed for single-touch use.
//
//  Usage:
//    RemoteTouchScreen ts(transport);   // same transport as display
//
//    void setup() {
//        transport.begin();
//        display.begin(240, 320);
//        ts.begin();                    // sends TOUCH_BEGIN, wires callbacks
//    }
//
//    void loop() {
//        TSPoint p = ts.getPoint();
//        if (p.z > RemoteTouchScreen::MINPRESSURE) {
//            // finger at p.x, p.y
//        }
//    }
// -----------------------------------------------------------------------------

// TSPoint — mirrors Adafruit_TouchScreen's TSPoint struct.
// Compatible with btn.contains(p.x, p.y) and all Adafruit GFX button code.
struct TSPoint {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;  // BC_TOUCH_Z_CONTACT (128) when touching, 0 when not

    TSPoint() = default;
    TSPoint(int16_t x_, int16_t y_, int16_t z_) : x(x_), y(y_), z(z_) {}
};

class RemoteTouchScreen
{
public:
    // Construct with the same transport used for the display.
    explicit RemoteTouchScreen(GraphicsTransport &transport);

    // ── Setup ─────────────────────────────────────────────────────────────────

    // Start touch reporting. Sends TOUCH_BEGIN to the iPhone and registers
    // the back-channel callback on the transport automatically.
    //
    // mode:         touch emulation mode (default TOUCH_MODE_RESISTIVE)
    // interval_ms:  TOUCH_MOVE throttle in ms (default 50ms = 20Hz)
    //               0 = every event, 16 = ~60Hz, 100 = 10Hz
    void begin(uint8_t  mode        = TOUCH_MODE_RESISTIVE,
               uint16_t interval_ms = TOUCH_MOVE_INTERVAL_MS_DEFAULT);

    // Stop touch reporting. Sends TOUCH_END to the iPhone.
    void end();

    // Update the TOUCH_MOVE throttle interval while touch is active.
    // Sends TOUCH_DELAY to the iPhone immediately.
    void setDelay(uint16_t interval_ms);

    // ── Polling API (call from loop()) ────────────────────────────────────────

    // Returns the most recent touch point.
    // Compatible with Adafruit_TouchScreen::getPoint().
    // z > MINPRESSURE means finger is down.
    TSPoint getPoint() const;

    // Returns true if a finger is currently down.
    // Compatible with Adafruit_FT6206::touched().
    bool touched() const;

    // ── Internal — called by back-channel callback ────────────────────────────
    // Not for direct use in sketches — public so the lambda can access it.
    void handleTouch(uint8_t cmd, int16_t x, int16_t y, uint8_t z);

    // ── Constants ─────────────────────────────────────────────────────────────

    // Minimum z value to consider a touch valid.
    // Use in place of a magic number:
    //   if (p.z > RemoteTouchScreen::MINPRESSURE) { ... }
    // BC_TOUCH_Z_CONTACT (128) always exceeds this threshold.
    static constexpr int16_t MINPRESSURE = 1;

    // pressureThreshhold — matches Adafruit_TouchScreen public member name
    // so sketches using ts.pressureThreshhold compile without change.
    int16_t pressureThreshhold = MINPRESSURE;

private:
    GraphicsTransport &_transport;
    volatile TSPoint   _point;        // updated by back-channel task
    bool               _active = false;

    // Send a framed GFX command with payload
    void sendCommand(uint8_t cmd, const void *payload, uint16_t payloadLen);
};