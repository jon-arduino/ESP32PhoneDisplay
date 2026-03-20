#pragma once
#include <Arduino.h>
#include "../Protocol.h"

// ─────────────────────────────────────────────────────────────────────────────
//  iPhoneTouchScreen — drop-in replacement for Adafruit_TouchScreen
//
//  Allows any Arduino/ESP32 sketch written for a resistive touchscreen to run
//  with the iPhone as the display AND touch input, with zero changes to the
//  sketch's touch-handling code.
//
//  Adafruit pattern (unchanged in ported sketch):
//
//    TSPoint p = ts.getPoint();
//    if (p.z > MINPRESSURE) {
//        btn.press(btn.contains(p.x, p.y));
//    } else {
//        btn.press(false);
//    }
//    if (btn.justPressed())  { ... }
//    if (btn.justReleased()) { ... }
//
//  Usage:
//    iPhoneTouchScreen ts;
//
//    // Register with whichever manager is active:
//    bleManager.onTouch([](uint8_t cmd, int16_t x, int16_t y) {
//        ts.handleTouch(cmd, x, y);
//    });
//    wifiManager.onTouch([](uint8_t cmd, int16_t x, int16_t y) {
//        ts.handleTouch(cmd, x, y);
//    });
//
//    // In loop() — unchanged from original sketch:
//    TSPoint p = ts.getPoint();
//
//  Thread safety:
//    handleTouch() is called from the AsyncTCP or NimBLE task.
//    getPoint() is called from loop(). The _point fields are written
//    atomically (each is a single aligned word on ESP32), so no mutex
//    is needed for the common single-touch use case.
//
//  Coordinate space:
//    iPhone sends coordinates already mapped to virtual display space
//    (0,0)–(displayWidth-1, displayHeight-1). No remapping needed on
//    the ESP32 side — p.x and p.y are ready to pass to btn.contains().
//
//  Pressure (z):
//    Capacitive screens have no real pressure. We use BC_TOUCH_PRESSURE
//    (0xFF) on DOWN/MOVE and 0 on UP. This is always > any MINPRESSURE
//    threshold used in Adafruit example sketches (typically 10–100).
// ─────────────────────────────────────────────────────────────────────────────

// Mirrors Adafruit_TouchScreen's TSPoint — compatible with btn.contains(p.x, p.y)
struct TSPoint {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;   // 0 = no touch, >0 = touching (BC_TOUCH_PRESSURE = 0xFF)
};

class iPhoneTouchScreen
{
public:
    iPhoneTouchScreen() = default;

    // ── Called from loop() — returns current touch state ─────────────────────
    // Returns a copy of the last received touch point.
    // z > 0 means finger is down; z == 0 means no touch.
    // Compatible with Adafruit_TouchScreen::getPoint().
    TSPoint getPoint() const { return _point; }

    // ── Called from back-channel callback (BLE or WiFi task) ─────────────────
    // Decodes the back-channel touch event and updates _point.
    // cmd: BC_CMD_TOUCH_DOWN, BC_CMD_TOUCH_MOVE, or BC_CMD_TOUCH_UP
    // x,y: virtual display coordinates (only meaningful for DOWN/MOVE)
    void handleTouch(uint8_t cmd, int16_t x, int16_t y)
    {
        switch (cmd) {
            case BC_CMD_TOUCH_DOWN:
            case BC_CMD_TOUCH_MOVE:
                _point = { x, y, (int16_t)BC_TOUCH_PRESSURE };
                break;
            case BC_CMD_TOUCH_UP:
                _point = { 0, 0, 0 };
                break;
            default:
                break;
        }
    }

    // ── Convenience: minimum pressure threshold ───────────────────────────────
    // Use instead of a magic number in ported sketches:
    //   if (p.z > iPhoneTouchScreen::MINPRESSURE) { ... }
    static constexpr int16_t MINPRESSURE = 1;

private:
    volatile TSPoint _point;  // updated by back-channel task, read by loop()
};
