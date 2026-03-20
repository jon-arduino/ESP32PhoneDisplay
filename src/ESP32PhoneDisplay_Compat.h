#pragma once

// -----------------------------------------------------------------------------
//  ESP32PhoneDisplay_Compat — drop-in Adafruit_GFX subclass
//
//  Subclasses Adafruit_GFX so it works anywhere an Adafruit_GFX* is expected.
//
//  Requires an explicit transport — include whichever you need:
//    #include <transport/BleTransport.h>
//    #include <transport/WiFiTransport.h>
//
//  Usage:
//    BleTransport transport;
//    ESP32PhoneDisplay_Compat tft(transport, 240, 320);
//    tft.begin();
//
//  Porting an existing sketch:
//    Before:  Adafruit_ST7735 tft(CS, DC, RST);
//             tft.initR(INITR_BLACKTAB);
//    After:   BleTransport transport;
//             ESP32PhoneDisplay_Compat tft(transport);
//             transport.begin();
//             tft.begin();
//
//  Note: drawCircle, fillCircle, drawRoundRect and text rendering decompose
//  to drawPixel/drawFastHLine calls via Adafruit_GFX (non-virtual limitation).
//  For compact single-command rendering of all shapes, use ESP32PhoneDisplay.
// -----------------------------------------------------------------------------

#include <Adafruit_GFX.h>
#include "GraphicsTransport.h"
#include "GraphicsProtocol.h"
#include "Protocol.h"

using Color = uint16_t;

class ESP32PhoneDisplay_Compat : public Adafruit_GFX
{
public:
    // Pass any GraphicsTransport — BleTransport, WiFiTransport, or custom.
    // w/h: virtual display size in pixels (default 240x320).
    explicit ESP32PhoneDisplay_Compat(GraphicsTransport &transport,
                                      uint16_t w = 240, uint16_t h = 320);

    // Send BEGIN command to iPhone — call after transport is connected.
    void begin();

    // Returns true when the transport is connected and ready to send.
    bool isConnected() const { return _transport.canSend(); }

    // Push buffered commands — call at end of each frame.
    void flush();

    // ── Required Adafruit_GFX override ───────────────────────────────────────
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;

    // ── Performance overrides ─────────────────────────────────────────────────
    // These send single compact commands rather than decomposing to drawPixel.
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void fillScreen(uint16_t color) override;

private:
    GraphicsTransport &_transport;

    void sendCommand(uint8_t cmd, const void *payload, uint16_t payloadLen);
};
