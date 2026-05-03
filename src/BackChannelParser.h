#pragma once
#include <Arduino.h>
#include <functional>
#include "Protocol.h"

// ─────────────────────────────────────────────────────────────────────────────
//  BackChannelParser — framed back-channel protocol parser
//
//  Parses the iPhone → ESP32 byte stream and fires typed callbacks for each
//  recognised command. Shared by BLEManager and WiFiManager — the framing
//  and dispatch logic lives here exactly once.
//
//  Frame format:
//    [BC_MAGIC 0xA5][lenLow][lenHigh][cmd][payload...]
//    len = 1 (cmd) + sizeof(payload)
//
//  Usage:
//    BackChannelParser _bc;
//
//    // Register callbacks once (setup or constructor):
//    _bc.onPong ([&]()                                    { handlePong();        });
//    _bc.onKey  ([&](uint8_t k)                           { handleKey(k);        });
//    _bc.onTouch([&](uint8_t c, int16_t x, int16_t y, uint8_t z) { ts.handleTouch(c,x,y,z); });
//
//    // Feed raw bytes as they arrive (called from BLE or TCP receive handler):
//    _bc.feed(data, len);
//
//  Thread safety:
//    feed() maintains internal parse state — do not call concurrently from
//    multiple tasks without external locking. Each manager calls feed() from
//    its own single receive callback, so no locking is needed in practice.
// ─────────────────────────────────────────────────────────────────────────────

class BackChannelParser
{
public:
    BackChannelParser() = default;

    // ── Callback registration ─────────────────────────────────────────────────

    // Called when iPhone responds to a ping. No payload.
    void onPong(std::function<void()> cb) { _pongCallback = cb; }

    // Called for KEY1 ('1') and KEY2 ('2') button events.
    void onKey(std::function<void(uint8_t key)> cb) { _keyCallback = cb; }

    // Called for touch events. cmd = BC_CMD_TOUCH_DOWN / TOUCH_MOVE / TOUCH_UP.
    // x, y are virtual display coordinates (0,0)–(displayW-1, displayH-1).
    // x and y are 0 for TOUCH_UP.
    void onTouch(std::function<void(uint8_t cmd, int16_t x, int16_t y, uint8_t z)> cb) { _touchCallback = cb; }

    // ── Feed raw bytes from receive handler ───────────────────────────────────
    // Call with each chunk of bytes as it arrives from BLE or TCP.
    void feed(const uint8_t *data, size_t len);

    // ── Reset parser state ────────────────────────────────────────────────────
    // Call on disconnect to discard any partial frame in progress.
    void reset() { _len = 0; }

    // ── Diagnostics ───────────────────────────────────────────────────────────
    struct Stats {
        uint32_t key1        = 0;   // KEY1 commands dispatched
        uint32_t key2        = 0;   // KEY2 commands dispatched
        uint32_t touch       = 0;   // touch events dispatched
        uint32_t pong        = 0;   // pong responses dispatched
        uint32_t syncErrors  = 0;   // bytes discarded waiting for BC_MAGIC
        uint32_t overruns    = 0;   // buffer overrun resets
        uint32_t invalidFrames = 0; // invalid frameLen resets
        uint32_t unknownCmds = 0;   // unrecognised command bytes
    };
    Stats    getStats() const  { return _stats; }
    void     resetStats()      { _stats = Stats{}; }

private:
    static constexpr size_t BUF_SIZE = 16;
    uint8_t _buf[BUF_SIZE];
    size_t  _len = 0;

    std::function<void()>                                    _pongCallback;
    std::function<void(uint8_t)>                             _keyCallback;
    std::function<void(uint8_t, int16_t, int16_t, uint8_t)>  _touchCallback;

    Stats _stats;
    void dispatch(uint8_t cmd, const uint8_t *payload, size_t payloadLen);
};