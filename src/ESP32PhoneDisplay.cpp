// ESP32PhoneDisplay.cpp
#include "ESP32PhoneDisplay.h"
#include "GraphicsProtocol.h"
#include "Protocol.h"
#include <string.h>

// Wire format:
//   [0]    0xA5 (magic)
//   [1..2] lenLE = number of bytes in (cmd + payload)
//   [3]    cmd
//   [4..]  payload

static constexpr uint8_t GFX_MAGIC = 0xA5;

static inline void putU16LE(uint8_t out[2], uint16_t v)
{
    out[0] = static_cast<uint8_t>(v & 0xFF);
    out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

ESP32PhoneDisplay::ESP32PhoneDisplay(GraphicsTransport &transport)
    : _transport(transport) {}

void ESP32PhoneDisplay::sendCommand(uint8_t cmd, const void *payload, uint16_t payloadLen)
{
    const uint16_t len   = static_cast<uint16_t>(1 + payloadLen);
    const uint16_t total = static_cast<uint16_t>(3 + 1 + payloadLen);

    if (total <= 256) {
        uint8_t buf[256];
        buf[0] = GFX_MAGIC;
        putU16LE(&buf[1], len);
        buf[3] = cmd;
        if (payload && payloadLen)
            memcpy(&buf[4], payload, payloadLen);
        _transport.send(buf, total);
    } else {
        uint8_t *buf = (uint8_t *)malloc(total);
        if (!buf) return;
        buf[0] = GFX_MAGIC;
        putU16LE(&buf[1], len);
        buf[3] = cmd;
        if (payload && payloadLen)
            memcpy(&buf[4], payload, payloadLen);
        _transport.send(buf, total);
        free(buf);
    }
}

void ESP32PhoneDisplay::sendCommandWithTail(uint8_t cmd,
                                            const void *fixed, uint16_t fixedLen,
                                            const void *tail,  uint16_t tailLen)
{
    const uint16_t payloadLen = static_cast<uint16_t>(fixedLen + tailLen);
    const uint16_t len        = static_cast<uint16_t>(1 + payloadLen);
    const uint16_t headTotal  = static_cast<uint16_t>(3 + 1 + fixedLen);

    if (headTotal <= 256) {
        uint8_t head[256];
        head[0] = GFX_MAGIC;
        putU16LE(&head[1], len);
        head[3] = cmd;
        if (fixed && fixedLen)
            memcpy(&head[4], fixed, fixedLen);
        _transport.send(head, headTotal);
    } else {
        uint8_t *head = (uint8_t *)malloc(headTotal);
        if (!head) return;
        head[0] = GFX_MAGIC;
        putU16LE(&head[1], len);
        head[3] = cmd;
        if (fixed && fixedLen)
            memcpy(&head[4], fixed, fixedLen);
        _transport.send(head, headTotal);
        free(head);
    }

    if (tail && tailLen)
        _transport.send(reinterpret_cast<const uint8_t *>(tail), tailLen);
}

// System
void ESP32PhoneDisplay::begin(uint16_t w, uint16_t h)
{
    GfxBeginPayload p{w, h};
    sendCommand(GFX_CMD_BEGIN, &p, sizeof(p));
    sendCommand(GFX_CMD_FLUSH, nullptr, 0);   // ensure BEGIN processed before subsequent commands
    _transport.flush();                        // wake drain task immediately
    _baseW = w; _baseH = h; _rotation = 0;
}

void ESP32PhoneDisplay::flush()
{
    sendCommand(GFX_CMD_FLUSH, nullptr, 0);
    _transport.flush();
}

void ESP32PhoneDisplay::close()
{
    sendCommand(GFX_CMD_FLUSH, nullptr, 0);         // flush any pending draws first
    _transport.flush();
    sendCommand(GFX_CMD_CLOSE_DISPLAY, nullptr, 0);
    _transport.flush();                              // ensure close goes out immediately
}

void ESP32PhoneDisplay::setTitle(const char *title)
{
    if (!title) title = "";
    uint16_t len = (uint16_t)strlen(title);
    sendCommand(GFX_CMD_SET_TITLE,
                reinterpret_cast<const void*>(title), len);
}

void ESP32PhoneDisplay::setButton1(const char *label)
{
    if (!label) label = "";
    uint8_t  labelLen = (uint8_t)strlen(label);
    // GFX_CMD_ADD_BUTTON payload: keyCode(u8) labelLen(u8) label(utf8)
    uint8_t buf[2 + 8];   // keyCode + labelLen + up to 8 chars
    buf[0] = BC_CMD_KEY1;
    buf[1] = labelLen;
    memcpy(&buf[2], label, labelLen);
    sendCommand(GFX_CMD_ADD_BUTTON, buf, 2 + labelLen);
}

void ESP32PhoneDisplay::setButton2(const char *label)
{
    if (!label) label = "";
    uint8_t  labelLen = (uint8_t)strlen(label);
    uint8_t buf[2 + 8];
    buf[0] = BC_CMD_KEY2;
    buf[1] = labelLen;
    memcpy(&buf[2], label, labelLen);
    sendCommand(GFX_CMD_ADD_BUTTON, buf, 2 + labelLen);
}


void ESP32PhoneDisplay::setRotation(uint8_t r)
{
    r &= 3; _rotation = r;
    GfxSetRotationPayload p{r};
    sendCommand(GFX_CMD_SET_ROTATION, &p, sizeof(p));
}

void ESP32PhoneDisplay::invertDisplay(bool i)
{
    GfxInvertDisplayPayload p{static_cast<uint8_t>(i ? 1 : 0)};
    sendCommand(GFX_CMD_INVERT_DISPLAY, &p, sizeof(p));
}

void ESP32PhoneDisplay::clear(Color color)
{
    GfxClearPayload p{color};
    sendCommand(GFX_CMD_FILL_SCREEN, &p, sizeof(p));
}

uint16_t ESP32PhoneDisplay::width()  const { return (_rotation & 1) ? _baseH : _baseW; }
uint16_t ESP32PhoneDisplay::height() const { return (_rotation & 1) ? _baseW : _baseH; }

// Pixels & Lines
void ESP32PhoneDisplay::drawPixel(int16_t x, int16_t y, Color color)
{
    GfxDrawPixelPayload p{x, y, color};
    sendCommand(GFX_CMD_DRAW_PIXEL, &p, sizeof(p));
}

void ESP32PhoneDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Color color)
{
    GfxDrawLinePayload p{x0, y0, x1, y1, color};
    sendCommand(GFX_CMD_DRAW_LINE, &p, sizeof(p));
}

void ESP32PhoneDisplay::drawFastHLine(int16_t x, int16_t y, int16_t w, Color color)
{
    GfxFastLinePayload p{x, y, w, color};
    sendCommand(GFX_CMD_DRAW_FAST_HLINE, &p, sizeof(p));
}

void ESP32PhoneDisplay::drawFastVLine(int16_t x, int16_t y, int16_t h, Color color)
{
    GfxFastLinePayload p{x, y, h, color};
    sendCommand(GFX_CMD_DRAW_FAST_VLINE, &p, sizeof(p));
}

// Rectangles
void ESP32PhoneDisplay::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color)
{
    GfxRectPayload p{x, y, w, h, color};
    sendCommand(GFX_CMD_DRAW_RECT, &p, sizeof(p));
}

void ESP32PhoneDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color)
{
    GfxRectPayload p{x, y, w, h, color};
    sendCommand(GFX_CMD_FILL_RECT, &p, sizeof(p));
}

void ESP32PhoneDisplay::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, Color color)
{
    GfxRoundRectPayload p{x, y, w, h, r, color};
    sendCommand(GFX_CMD_DRAW_ROUNDRECT, &p, sizeof(p));
}

void ESP32PhoneDisplay::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, Color color)
{
    GfxRoundRectPayload p{x, y, w, h, r, color};
    sendCommand(GFX_CMD_FILL_ROUNDRECT, &p, sizeof(p));
}

// Circles & Triangles
void ESP32PhoneDisplay::drawCircle(int16_t x, int16_t y, int16_t r, Color color)
{
    GfxCirclePayload p{x, y, r, color};
    sendCommand(GFX_CMD_DRAW_CIRCLE, &p, sizeof(p));
}

void ESP32PhoneDisplay::fillCircle(int16_t x, int16_t y, int16_t r, Color color)
{
    GfxCirclePayload p{x, y, r, color};
    sendCommand(GFX_CMD_FILL_CIRCLE, &p, sizeof(p));
}

void ESP32PhoneDisplay::drawTriangle(int16_t x0, int16_t y0,
                                     int16_t x1, int16_t y1,
                                     int16_t x2, int16_t y2, Color color)
{
    GfxTrianglePayload p{x0, y0, x1, y1, x2, y2, color};
    sendCommand(GFX_CMD_DRAW_TRIANGLE, &p, sizeof(p));
}

void ESP32PhoneDisplay::fillTriangle(int16_t x0, int16_t y0,
                                     int16_t x1, int16_t y1,
                                     int16_t x2, int16_t y2, Color color)
{
    GfxTrianglePayload p{x0, y0, x1, y1, x2, y2, color};
    sendCommand(GFX_CMD_FILL_TRIANGLE, &p, sizeof(p));
}

// Bitmaps
void ESP32PhoneDisplay::drawBitmap(int16_t x, int16_t y,
                                   const uint8_t *bitmap,
                                   int16_t w, int16_t h, Color color)
{
    GfxBitmapPayload p{x, y, w, h};
    uint16_t nBytes = (uint16_t)(((int32_t)w * h + 7) / 8);
    sendCommandWithTail(GFX_CMD_DRAW_BITMAP, &p, sizeof(p), bitmap, nBytes);
}

void ESP32PhoneDisplay::drawBitmap(int16_t x, int16_t y,
                                   const uint8_t *bitmap,
                                   int16_t w, int16_t h, Color fg, Color bg)
{
    GfxBitmapBgPayload p{x, y, w, h, fg, bg};
    uint16_t nBytes = (uint16_t)(((int32_t)w * h + 7) / 8);
    sendCommandWithTail(GFX_CMD_DRAW_BITMAP_BG, &p, sizeof(p), bitmap, nBytes);
}

// Text
void ESP32PhoneDisplay::setCursor(int16_t x, int16_t y)
{
    _cursorX = x; _cursorY = y;
    GfxSetCursorPayload p{x, y};
    sendCommand(GFX_CMD_SET_CURSOR, &p, sizeof(p));
}

void ESP32PhoneDisplay::setTextColor(Color color)
{
    _textColor = color;
    GfxSetTextColorPayload p{color};
    sendCommand(GFX_CMD_SET_TEXT_COLOR, &p, sizeof(p));
}

void ESP32PhoneDisplay::setTextColor(Color fg, Color bg)
{
    _textColor = fg; _textBgColor = bg;
    GfxSetTextColorBgPayload p{fg, bg};
    sendCommand(GFX_CMD_SET_TEXT_COLOR_BG, &p, sizeof(p));
}

void ESP32PhoneDisplay::setTextSize(uint8_t size)
{
    _textSize = size;
    GfxSetTextSizePayload p{size};
    sendCommand(GFX_CMD_SET_TEXT_SIZE, &p, sizeof(p));
}

void ESP32PhoneDisplay::setTextWrap(bool wrap)
{
    _wrap = wrap;
    GfxSetTextWrapPayload p{static_cast<uint8_t>(wrap ? 1 : 0)};
    sendCommand(GFX_CMD_SET_TEXT_WRAP, &p, sizeof(p));
}

void ESP32PhoneDisplay::cp437(bool enable)
{
    _cp437 = enable;
    GfxCp437Payload p{static_cast<uint8_t>(enable ? 1 : 0)};
    sendCommand(GFX_CMD_CP437, &p, sizeof(p));
}

void ESP32PhoneDisplay::setFont(const GFXfont *font)
{
    _font = font;
    uint16_t id = font ? 1 : 0;
    GfxSetFontPayload p{id};
    sendCommand(GFX_CMD_SET_FONT, &p, sizeof(p));
}

size_t ESP32PhoneDisplay::write(uint8_t c)
{
    GfxWriteCharPayload p{c};
    sendCommand(GFX_CMD_WRITE_CHAR, &p, sizeof(p));
    return 1;
}