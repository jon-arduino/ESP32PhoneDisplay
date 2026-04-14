#include "RemoteTouchScreen.h"
#include <string.h>

static constexpr uint8_t MAGIC = 0xA5;

static inline void putU16LE(uint8_t out[2], uint16_t v)
{
    out[0] = v & 0xFF;
    out[1] = (v >> 8) & 0xFF;
}

RemoteTouchScreen::RemoteTouchScreen(GraphicsTransport &transport)
    : _transport(transport)
{}

// ── Setup ─────────────────────────────────────────────────────────────────────

void RemoteTouchScreen::begin(uint8_t mode, uint16_t interval_ms)
{
    // Wire back-channel callback — fires on BLE/WiFi receive task
    _transport.onTouch([this](uint8_t cmd, int16_t x, int16_t y, uint8_t z) {
        handleTouch(cmd, x, y, z);
    });

    // Send TOUCH_BEGIN to iPhone
    GfxTouchBeginPayload p;
    p.mode             = mode;
    p.move_interval_ms = interval_ms;
    sendCommand(GFX_CMD_TOUCH_BEGIN, &p, sizeof(p));

    _active = true;
}

void RemoteTouchScreen::end()
{
    sendCommand(GFX_CMD_TOUCH_END, nullptr, 0);
    _active = false;
    _point.x = 0; _point.y = 0; _point.z = 0;
}

void RemoteTouchScreen::setDelay(uint16_t interval_ms)
{
    GfxTouchDelayPayload p;
    p.move_interval_ms = interval_ms;
    sendCommand(GFX_CMD_TOUCH_DELAY, &p, sizeof(p));
}

// ── Polling API ───────────────────────────────────────────────────────────────

TSPoint RemoteTouchScreen::getPoint() const
{
    TSPoint p;
    p.x = _point.x;
    p.y = _point.y;
    p.z = _point.z;
    return p;
}

bool RemoteTouchScreen::touched() const
{
    return _point.z > MINPRESSURE;
}

// ── Internal back-channel handler ─────────────────────────────────────────────

void RemoteTouchScreen::handleTouch(uint8_t cmd, int16_t x, int16_t y, uint8_t z)
{
    switch (cmd) {
        case BC_CMD_TOUCH_DOWN:
        case BC_CMD_TOUCH_MOVE:
            _point.x = x; _point.y = y; _point.z = (int16_t)z;
            break;
        case BC_CMD_TOUCH_UP:
            _point.x = 0; _point.y = 0; _point.z = 0;
            break;
        default:
            break;
    }
}

// ── Private ───────────────────────────────────────────────────────────────────

void RemoteTouchScreen::sendCommand(uint8_t cmd,
                                    const void *payload,
                                    uint16_t payloadLen)
{
    if (!_transport.canSend()) return;

    const uint16_t len   = 1 + payloadLen;
    const uint16_t total = 3 + 1 + payloadLen;

    if (total <= 64) {   // all touch commands are small
        uint8_t buf[64];
        buf[0] = MAGIC;
        putU16LE(&buf[1], len);
        buf[3] = cmd;
        if (payload && payloadLen)
            memcpy(&buf[4], payload, payloadLen);
        _transport.send(buf, total);
    }
}