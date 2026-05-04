#include "ESP32PhoneDisplay_Compat.h"
#include "Protocol.h"
#include <string.h>

static constexpr uint8_t MAGIC = 0xA5;

static inline void putU16LE(uint8_t out[2], uint16_t v)
{
    out[0] = v & 0xFF;
    out[1] = (v >> 8) & 0xFF;
}

ESP32PhoneDisplay_Compat::ESP32PhoneDisplay_Compat(GraphicsTransport &transport,
                                                   uint16_t w, uint16_t h)
    : Adafruit_GFX(w, h), _transport(transport)
{}

void ESP32PhoneDisplay_Compat::begin()
{
    GfxBeginPayload p{ (uint16_t)WIDTH, (uint16_t)HEIGHT };
    sendCommand(GFX_CMD_BEGIN, &p, sizeof(p));
    sendCommand(GFX_CMD_FLUSH, nullptr, 0);   // ensure BEGIN processed before subsequent commands
    _transport.flush();
}

void ESP32PhoneDisplay_Compat::flush()
{
    sendCommand(GFX_CMD_FLUSH, nullptr, 0);
    _transport.flush();
}

void ESP32PhoneDisplay_Compat::close()
{
    sendCommand(GFX_CMD_FLUSH, nullptr, 0);
    _transport.flush();
    sendCommand(GFX_CMD_CLOSE_DISPLAY, nullptr, 0);
    _transport.flush();
}

void ESP32PhoneDisplay_Compat::setTitle(const char *title)
{
    if (!title) title = "";
    sendCommand(GFX_CMD_SET_TITLE,
                reinterpret_cast<const void*>(title), (uint16_t)strlen(title));
}

void ESP32PhoneDisplay_Compat::setButton1(const char *label)
{
    if (!label) label = "";
    uint8_t labelLen = (uint8_t)strlen(label);
    uint8_t buf[2 + 8];
    buf[0] = BC_CMD_KEY1;
    buf[1] = labelLen;
    memcpy(&buf[2], label, labelLen);
    sendCommand(GFX_CMD_ADD_BUTTON, buf, 2 + labelLen);
}

void ESP32PhoneDisplay_Compat::setButton2(const char *label)
{
    if (!label) label = "";
    uint8_t labelLen = (uint8_t)strlen(label);
    uint8_t buf[2 + 8];
    buf[0] = BC_CMD_KEY2;
    buf[1] = labelLen;
    memcpy(&buf[2], label, labelLen);
    sendCommand(GFX_CMD_ADD_BUTTON, buf, 2 + labelLen);
}

void ESP32PhoneDisplay_Compat::clearButtons()
{
    sendCommand(GFX_CMD_CLEAR_BUTTONS, nullptr, 0);
}

void ESP32PhoneDisplay_Compat::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    GfxDrawPixelPayload p{x, y, color};
    sendCommand(GFX_CMD_DRAW_PIXEL, &p, sizeof(p));
}

void ESP32PhoneDisplay_Compat::drawFastHLine(int16_t x, int16_t y,
                                             int16_t w, uint16_t color)
{
    GfxFastLinePayload p{x, y, w, color};
    sendCommand(GFX_CMD_DRAW_FAST_HLINE, &p, sizeof(p));
}

void ESP32PhoneDisplay_Compat::drawFastVLine(int16_t x, int16_t y,
                                             int16_t h, uint16_t color)
{
    GfxFastLinePayload p{x, y, h, color};
    sendCommand(GFX_CMD_DRAW_FAST_VLINE, &p, sizeof(p));
}

void ESP32PhoneDisplay_Compat::fillRect(int16_t x, int16_t y,
                                        int16_t w, int16_t h, uint16_t color)
{
    GfxRectPayload p{x, y, w, h, color};
    sendCommand(GFX_CMD_FILL_RECT, &p, sizeof(p));
}

void ESP32PhoneDisplay_Compat::fillScreen(uint16_t color)
{
    GfxClearPayload p{color};
    sendCommand(GFX_CMD_FILL_SCREEN, &p, sizeof(p));
}

void ESP32PhoneDisplay_Compat::sendCommand(uint8_t cmd,
                                           const void *payload,
                                           uint16_t payloadLen)
{
    if (!_transport.canSend()) return;

    const uint16_t len   = 1 + payloadLen;
    const uint16_t total = 3 + 1 + payloadLen;

    if (total <= 256) {
        uint8_t buf[256];
        buf[0] = MAGIC;
        putU16LE(&buf[1], len);
        buf[3] = cmd;
        if (payload && payloadLen)
            memcpy(&buf[4], payload, payloadLen);
        _transport.send(buf, total);
    } else {
        uint8_t *buf = (uint8_t *)malloc(total);
        if (!buf) return;
        buf[0] = MAGIC;
        putU16LE(&buf[1], len);
        buf[3] = cmd;
        if (payload && payloadLen)
            memcpy(&buf[4], payload, payloadLen);
        _transport.send(buf, total);
        free(buf);
    }
}