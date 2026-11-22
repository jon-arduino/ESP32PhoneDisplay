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

// A frame that arrives in several send() calls is not atomic: a transport's background task
// (WiFi ping, auto-flush) can slip its own bytes between the pieces and corrupt the stream.
// So the header and tails are gathered into ONE buffer and handed over in ONE send().
void ESP32PhoneDisplay::sendCommandWithTail(uint8_t cmd,
                                            const void *fixed, uint16_t fixedLen,
                                            const void *tail,  uint16_t tailLen)
{
    sendCommandWithTails(cmd, fixed, fixedLen, tail, tailLen, nullptr, 0);
}

void ESP32PhoneDisplay::sendCommandWithTails(uint8_t cmd,
                                             const void *fixed, uint16_t fixedLen,
                                             const void *t1,    uint16_t l1,
                                             const void *t2,    uint16_t l2)
{
    const uint32_t payloadLen = (uint32_t)fixedLen + l1 + l2;
    const uint32_t total      = 4 + payloadLen;               // magic + len(2) + cmd + payload
    if (total > 65535) return;                                // would not fit the 16-bit length field

    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return;
    buf[0] = GFX_MAGIC;
    putU16LE(&buf[1], static_cast<uint16_t>(1 + payloadLen));
    buf[3] = cmd;
    size_t o = 4;
    if (fixed && fixedLen) { memcpy(buf + o, fixed, fixedLen); o += fixedLen; }
    if (t1 && l1)          { memcpy(buf + o, t1, l1);          o += l1; }
    if (t2 && l2)          { memcpy(buf + o, t2, l2);          o += l2; }
    _transport.send(buf, static_cast<uint16_t>(o));
    free(buf);
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

void ESP32PhoneDisplay::fillScreen(Color color)
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
// Large images are sent as horizontal strips so that no frame exceeds ~GFX_BITMAP_STRIP_BYTES of
// pixel data (transports buffer a few KB, BLE writes are small). Each strip is a self-contained
// command, so a partial image is never misinterpreted by the display.
#ifndef GFX_BITMAP_STRIP_BYTES
#define GFX_BITMAP_STRIP_BYTES 2048
#endif

static inline int stripRows(int bytesPerRow)
{
    int rows = GFX_BITMAP_STRIP_BYTES / (bytesPerRow > 0 ? bytesPerRow : 1);
    return rows < 1 ? 1 : rows;
}

void ESP32PhoneDisplay::drawBitmap(int16_t x, int16_t y,
                                   const uint8_t *bitmap,
                                   int16_t w, int16_t h, Color color)
{
    if (!bitmap || w <= 0 || h <= 0) return;
    const int bw = (w + 7) / 8, rows = stripRows(bw);
    for (int j = 0; j < h; j += rows) {
        const int n = (h - j < rows) ? (h - j) : rows;
#ifdef GFX_LEGACY_BITMAP
        // older displays only know 0x40, which carries no colour: use the text colour instead
        setTextColor(color);
        GfxBitmapPayload p{x, (int16_t)(y + j), w, (int16_t)n};
        sendCommandWithTail(GFX_CMD_DRAW_BITMAP, &p, sizeof(p), bitmap + (size_t)j * bw, (uint16_t)(n * bw));
#else
        GfxBitmapFgPayload p{x, (int16_t)(y + j), w, (int16_t)n, color};
        sendCommandWithTail(GFX_CMD_DRAW_BITMAP_FG, &p, sizeof(p), bitmap + (size_t)j * bw, (uint16_t)(n * bw));
#endif
    }
}

void ESP32PhoneDisplay::drawBitmap(int16_t x, int16_t y,
                                   const uint8_t *bitmap,
                                   int16_t w, int16_t h, Color fg, Color bg)
{
    if (!bitmap || w <= 0 || h <= 0) return;
    const int bw = (w + 7) / 8, rows = stripRows(bw);     // rows are padded to whole bytes, like Adafruit_GFX
    for (int j = 0; j < h; j += rows) {
        const int n = (h - j < rows) ? (h - j) : rows;
        GfxBitmapBgPayload p{x, (int16_t)(y + j), w, (int16_t)n, fg, bg};
        sendCommandWithTail(GFX_CMD_DRAW_BITMAP_BG, &p, sizeof(p), bitmap + (size_t)j * bw, (uint16_t)(n * bw));
    }
}

void ESP32PhoneDisplay::drawXBitmap(int16_t x, int16_t y,
                                    const uint8_t *bitmap,
                                    int16_t w, int16_t h, Color color)
{
    if (!bitmap || w <= 0 || h <= 0) return;
    const int bw = (w + 7) / 8, rows = stripRows(bw);
    uint8_t *tmp = (uint8_t *)malloc((size_t)bw * rows);
    if (!tmp) return;
    for (int j = 0; j < h; j += rows) {
        const int n = (h - j < rows) ? (h - j) : rows;
        for (int k = 0; k < bw * n; k++) {                // X11 is LSB-first; the wire format is MSB-first
            uint8_t b = bitmap[(size_t)j * bw + k], r = 0;
            for (int i = 0; i < 8; i++) if (b & (1 << i)) r |= 0x80 >> i;
            tmp[k] = r;
        }
        GfxBitmapFgPayload p{x, (int16_t)(y + j), w, (int16_t)n, color};
        sendCommandWithTail(GFX_CMD_DRAW_BITMAP_FG, &p, sizeof(p), tmp, (uint16_t)(n * bw));
    }
    free(tmp);
}

void ESP32PhoneDisplay::drawRGBBitmap(int16_t x, int16_t y,
                                      const uint16_t *bitmap, int16_t w, int16_t h)
{
    if (!bitmap || w <= 0 || h <= 0 || 2 * (int32_t)w + 9 > 65535) return;
    const int rows = stripRows(2 * w);
    for (int j = 0; j < h; j += rows) {
        const int n = (h - j < rows) ? (h - j) : rows;
        GfxImagePayload p{x, (int16_t)(y + j), w, (int16_t)n};
        // ESP32 is little-endian, so the pixel array is already in wire order
        sendCommandWithTail(GFX_CMD_DRAW_RGB_BITMAP, &p, sizeof(p), bitmap + (size_t)j * w, (uint16_t)(2 * n * w));
    }
}

void ESP32PhoneDisplay::drawRGBBitmap(int16_t x, int16_t y,
                                      const uint16_t *bitmap, const uint8_t *mask,
                                      int16_t w, int16_t h)
{
    if (!mask) { drawRGBBitmap(x, y, bitmap, w, h); return; }
    if (!bitmap || w <= 0 || h <= 0) return;
    const int bw = (w + 7) / 8;
    if (bw + 2 * (int32_t)w + 9 > 65535) return;
    const int rows = stripRows(bw + 2 * w);
    for (int j = 0; j < h; j += rows) {
        const int n = (h - j < rows) ? (h - j) : rows;
        GfxImagePayload p{x, (int16_t)(y + j), w, (int16_t)n};
        sendCommandWithTails(GFX_CMD_DRAW_RGB_BITMAP_MASK, &p, sizeof(p),
                             mask + (size_t)j * bw, (uint16_t)(n * bw),
                             bitmap + (size_t)j * w, (uint16_t)(2 * n * w));
    }
}

void ESP32PhoneDisplay::drawGrayscaleBitmap(int16_t x, int16_t y,
                                            const uint8_t *bitmap, int16_t w, int16_t h)
{
    if (!bitmap || w <= 0 || h <= 0 || (int32_t)w + 9 > 65535) return;
    const int rows = stripRows(w);
    for (int j = 0; j < h; j += rows) {
        const int n = (h - j < rows) ? (h - j) : rows;
        GfxImagePayload p{x, (int16_t)(y + j), w, (int16_t)n};
        sendCommandWithTail(GFX_CMD_DRAW_GRAY_BITMAP, &p, sizeof(p), bitmap + (size_t)j * w, (uint16_t)(n * w));
    }
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

void ESP32PhoneDisplay::clearButtons()
{
    sendCommand(GFX_CMD_CLEAR_BUTTONS, nullptr, 0);
}

size_t ESP32PhoneDisplay::write(uint8_t c)
{
    GfxWriteCharPayload p{c};
    sendCommand(GFX_CMD_WRITE_CHAR, &p, sizeof(p));
    return 1;
}

// ── Text bounds ───────────────────────────────────────────────────────────────
//
// getTextBounds() and _charBoundsNative() are pure local arithmetic —
// no commands are sent to the phone. They mirror Adafruit_GFX::getTextBounds()
// and Adafruit_GFX::charBounds() exactly, using our locally tracked state
// (_textSize, _font, _wrap, _baseW) instead of Adafruit_GFX inheritance.
//
// This lets native ESP32PhoneDisplay sketches compute text layout (centering,
// background clearing) identically to Adafruit_GFX sketches without pulling
// in the compat layer.

void ESP32PhoneDisplay::_charBoundsNative(uint8_t c,
                                           int16_t *x,    int16_t *y,
                                           int16_t *minX, int16_t *minY,
                                           int16_t *maxX, int16_t *maxY)
{
    if (_font) {
        // ── GFXfont — per-glyph metrics ───────────────────────────────────────
        if (c == '\n') {
            *x  = 0;
            *y += (int16_t)_font->yAdvance * _textSize;
        } else if (c != '\r') {
            uint8_t first = _font->first;
            uint8_t last  = _font->last;
            if (c >= first && c <= last) {
                GFXglyph *g  = &_font->glyph[c - first];
                int16_t   xa = (int16_t)g->xAdvance * _textSize;
                // Wrap before measuring so bounds reflect post-wrap position
                if (_wrap && (*x + xa) >= (int16_t)_baseW) {
                    *x  = 0;
                    *y += (int16_t)_font->yAdvance * _textSize;
                }
                int16_t x1 = *x + (int16_t)g->xOffset * _textSize;
                int16_t y1 = *y + (int16_t)g->yOffset * _textSize;
                int16_t x2 = x1  + (int16_t)g->width   * _textSize - 1;
                int16_t y2 = y1  + (int16_t)g->height  * _textSize - 1;
                if (x1 < *minX) *minX = x1;
                if (y1 < *minY) *minY = y1;
                if (x2 > *maxX) *maxX = x2;
                if (y2 > *maxY) *maxY = y2;
                *x += xa;
            }
        }
    } else {
        // ── Built-in 5×7 font — fixed metrics ────────────────────────────────
        // Each character cell is 6×8 at textSize=1 (5px glyph + 1px gap wide,
        // 7px glyph + 1px gap tall). Bounding box expands to cover full cell.
        if (c == '\n') {
            *x  = 0;
            *y += _textSize * 8;
        } else if (c != '\r') {
            if (_wrap && (*x + _textSize * 6) > (int16_t)_baseW) {
                *x  = 0;
                *y += _textSize * 8;
            }
            int16_t x2 = *x + _textSize * 6 - 1;
            int16_t y2 = *y + _textSize * 8 - 1;
            if (*x  < *minX) *minX = *x;
            if (*y  < *minY) *minY = *y;
            if (x2  > *maxX) *maxX = x2;
            if (y2  > *maxY) *maxY = y2;
            *x += _textSize * 6;
        }
    }
}

void ESP32PhoneDisplay::getTextBounds(const char *str, int16_t x, int16_t y,
                                       int16_t *x1, int16_t *y1,
                                       uint16_t *w,  uint16_t *h)
{
    // Initialise outputs
    *x1 = x; *y1 = y; *w = *h = 0;

    if (!str || !*str) return;

    // Scan string — cx/cy track the running cursor, min/max track the envelope
    int16_t cx = x, cy = y;
    int16_t minX = 0x7FFF, minY = 0x7FFF;
    int16_t maxX = -1,     maxY = -1;

    uint8_t c;
    while ((c = (uint8_t)*str++) != '\0') {
        _charBoundsNative(c, &cx, &cy, &minX, &minY, &maxX, &maxY);
    }

    if (maxX >= minX) { *x1 = minX; *w = (uint16_t)(maxX - minX + 1); }
    if (maxY >= minY) { *y1 = minY; *h = (uint16_t)(maxY - minY + 1); }
}