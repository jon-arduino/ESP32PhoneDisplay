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
    _transport.onTouch([this](uint8_t cmd, int16_t x, int16_t y, uint8_t z) {
        handleTouch(cmd, x, y, z);
    });

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
    portENTER_CRITICAL(&_mux);
    _current = TSPoint();
    clearQueue();
    portEXIT_CRITICAL(&_mux);
}

void RemoteTouchScreen::setDelay(uint16_t interval_ms)
{
    GfxTouchDelayPayload p;
    p.move_interval_ms = interval_ms;
    sendCommand(GFX_CMD_TOUCH_DELAY, &p, sizeof(p));
}

// ── Standard API ──────────────────────────────────────────────────────────────

// Returns newest point and discards path queue.
// Standard UI/game pattern — no path history needed.
TSPoint RemoteTouchScreen::getPoint()
{
    portENTER_CRITICAL(&_mux);
    TSPoint p = _current;
    clearQueue();   // discard path history — caller wants current state only
    portEXIT_CRITICAL(&_mux);
    return p;
}

bool RemoteTouchScreen::touched() const
{
    return _current.z > MINPRESSURE;
}

// ── Path API ──────────────────────────────────────────────────────────────────

uint8_t RemoteTouchScreen::available() const
{
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&_mux));
    uint8_t count = (_qHead - _qTail) % QUEUE_SIZE;
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&_mux));
    return count;
}

TSPoint RemoteTouchScreen::getQueuedPoint()
{
    portENTER_CRITICAL(&_mux);
    TSPoint p;
    if (_qHead != _qTail) {
        p = _queue[_qTail];
        _qTail = (_qTail + 1) % QUEUE_SIZE;
    }
    portEXIT_CRITICAL(&_mux);
    return p;
}

// ── Diagnostics ───────────────────────────────────────────────────────────────

uint16_t RemoteTouchScreen::queueOverflows() const { return _overflows; }
void     RemoteTouchScreen::resetOverflows()        { _overflows = 0;   }

// ── Internal ──────────────────────────────────────────────────────────────────

// Called from BLE/WiFi receive task (core 0) — protected by spinlock.
void RemoteTouchScreen::handleTouch(uint8_t cmd, int16_t x, int16_t y, uint8_t z)
{
    portENTER_CRITICAL(&_mux);

    switch (cmd) {
        case BC_CMD_TOUCH_DOWN:
        case BC_CMD_TOUCH_MOVE: {
            TSPoint p(x, y, (int16_t)z);
            _current = p;
            enqueue(p);
            break;
        }
        case BC_CMD_TOUCH_UP:
            _current = TSPoint();   // z=0, finger lifted
            // Enqueue a zero-z point so path readers know touch ended
            enqueue(TSPoint(x, y, 0));
            break;
        default:
            break;
    }

    portEXIT_CRITICAL(&_mux);
}

// Must be called with _mux held.
void RemoteTouchScreen::enqueue(const TSPoint &p)
{
    uint8_t nextHead = (_qHead + 1) % QUEUE_SIZE;
    if (nextHead == _qTail) {
        // Queue full — drop oldest point, advance tail
        _qTail = (_qTail + 1) % QUEUE_SIZE;
        _overflows++;
    }
    _queue[_qHead] = p;
    _qHead = nextHead;
}

// Must be called with _mux held.
void RemoteTouchScreen::clearQueue()
{
    _qHead = _qTail = 0;
}

// ── Private ───────────────────────────────────────────────────────────────────

void RemoteTouchScreen::sendCommand(uint8_t cmd,
                                    const void *payload,
                                    uint16_t payloadLen)
{
    if (!_transport.canSend()) return;

    const uint16_t len   = 1 + payloadLen;
    const uint16_t total = 3 + 1 + payloadLen;

    if (total <= 64) {
        uint8_t buf[64];
        buf[0] = MAGIC;
        putU16LE(&buf[1], len);
        buf[3] = cmd;
        if (payload && payloadLen)
            memcpy(&buf[4], payload, payloadLen);
        _transport.send(buf, total);
    }
}