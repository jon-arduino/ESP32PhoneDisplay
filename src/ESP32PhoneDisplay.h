#pragma once

// -----------------------------------------------------------------------------
//  ESP32PhoneDisplay — core graphics API
//
//  Drop-in replacement for Adafruit_GFX that renders on an iPhone over BLE,
//  WiFi, or any custom transport. Every drawing operation sends a single
//  compact binary command — no pixel decomposition for text, circles, etc.
//
//  This header has NO transport dependencies. Include the transport you need
//  separately:
//    #include <transport/BleTransport.h>    // NimBLE-Arduino required
//    #include <transport/WiFiTransport.h>   // AsyncTCP required
//
//  Usage:
//    BleTransport        transport;
//    ESP32PhoneDisplay   display(transport);
//
//    transport.begin();
//    display.begin(240, 320);
//    display.clear(0x0000);
//    display.setCursor(10, 10);
//    display.setTextColor(0xFFFF);
//    display.print("Hello iPhone!");
//    display.flush();
//
//  Dependencies:
//    - Adafruit GFX Library (for GFXfont type only)
//    - GraphicsTransport.h (abstract base, included here)
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <stdint.h>
#include <Adafruit_GFX.h>       // GFXfont type
#include "GraphicsTransport.h"
#include "GraphicsProtocol.h"
#include "Protocol.h"

// RGB565 colour type — same as Adafruit_GFX
using Color = uint16_t;

class ESP32PhoneDisplay : public Print
{
public:
    explicit ESP32PhoneDisplay(GraphicsTransport &transport);

    // ── System ────────────────────────────────────────────────────────────────
    // Call begin() once after the transport is connected.
    // w/h define the virtual display size in pixels.
    // The iPhone app scales to fill the screen — choose any resolution.
    void begin(uint16_t w, uint16_t h);

    // ── Display session ───────────────────────────────────────────────────────
    // close() — end session gracefully. Phone clears screen, resets title and
    // buttons. Call before begin() with new dimensions, or when sketch exits.
    void close();

    // setTitle() — set iPhone navigation bar title.
    // Empty string resets to transport default ("BLE Display" / "WiFi Display").
    // Persists across brief disconnects. Cleared by begin() or close().
    void setTitle(const char *title);

    // setButton1/2() — add or update a toolbar button on the iPhone nav bar.
    // Button1 always appears left of Button2 regardless of send order.
    // When pressed, iPhone sends BC_CMD_KEY1 / BC_CMD_KEY2 over back-channel.
    // clearButtons() removes all buttons. No per-button clear — use clearButtons()
    // then re-add any buttons you want to keep.
    // Persists across brief disconnects. Cleared by begin() or close().
    void setButton1(const char *label);
    void setButton2(const char *label);
    void clearButtons();

    // Fill screen with colour and reset cursor to (0,0)
    void clear(Color color = 0x0000);

    // Push buffered commands — call at the end of each frame
    void flush();

    void setRotation(uint8_t r);      // 0–3, same as Adafruit_GFX
    void invertDisplay(bool invert);

    // ── Dimensions (after rotation) ───────────────────────────────────────────
    uint16_t width()  const;
    uint16_t height() const;

    // ── Pixels & Lines ────────────────────────────────────────────────────────
    void drawPixel(int16_t x, int16_t y, Color color);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Color color);
    void drawFastHLine(int16_t x, int16_t y, int16_t w, Color color);
    void drawFastVLine(int16_t x, int16_t y, int16_t h, Color color);

    // ── Rectangles ────────────────────────────────────────────────────────────
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color);
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, Color color);
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, Color color);

    // ── Circles & Triangles ───────────────────────────────────────────────────
    void drawCircle(int16_t x, int16_t y, int16_t r, Color color);
    void fillCircle(int16_t x, int16_t y, int16_t r, Color color);
    void drawTriangle(int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, Color color);
    void fillTriangle(int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, Color color);

    // ── Bitmaps ───────────────────────────────────────────────────────────────
    // 1-bit monochrome, compatible with Adafruit_GFX::drawBitmap()
    void drawBitmap(int16_t x, int16_t y,
                    const uint8_t *bitmap, int16_t w, int16_t h,
                    Color color);
    void drawBitmap(int16_t x, int16_t y,
                    const uint8_t *bitmap, int16_t w, int16_t h,
                    Color fg, Color bg);

    // ── Text ──────────────────────────────────────────────────────────────────
    // Compatible with Adafruit_GFX text API. print()/println() work via write().
    void setCursor(int16_t x, int16_t y);
    void setTextColor(Color color);
    void setTextColor(Color fg, Color bg);
    void setTextSize(uint8_t size);
    void setTextWrap(bool wrap);
    void cp437(bool enable = true);
    void setFont(const GFXfont *font);

    // Print interface — called by print()/println()
    size_t write(uint8_t c) override;

private:
    GraphicsTransport &_transport;

    uint16_t        _baseW    = 0;
    uint16_t        _baseH    = 0;
    uint8_t         _rotation = 0;
    int16_t         _cursorX  = 0;
    int16_t         _cursorY  = 0;
    Color           _textColor   = 0xFFFF;
    Color           _textBgColor = 0x0000;
    uint8_t         _textSize = 1;
    bool            _wrap     = true;
    bool            _cp437    = false;
    const GFXfont  *_font     = nullptr;

    void sendCommand(uint8_t cmd, const void *payload, uint16_t payloadLen);
    void sendCommandWithTail(uint8_t cmd,
                             const void *fixed,  uint16_t fixedLen,
                             const void *tail,   uint16_t tailLen);
};