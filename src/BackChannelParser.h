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
//    _bc.onTouch([&](uint8_t c, int16_t x, int16_t y)    { ts.handleTouch(c,x,y); });
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
    void onTouch(std::function<void(uint8_t cmd, int16_t x, int16_t y)> cb) { _touchCallback = cb; }

    // ── Feed raw bytes from receive handler ───────────────────────────────────
    // Call with each chunk of bytes as it arrives from BLE or TCP.
    void feed(const uint8_t *data, size_t len);

    // ── Reset parser state ────────────────────────────────────────────────────
    // Call on disconnect to discard any partial frame in progress.
    void reset() { _len = 0; }

private:
    static constexpr size_t BUF_SIZE = 16;
    uint8_t _buf[BUF_SIZE];
    size_t  _len = 0;

    std::function<void()>                                    _pongCallback;
    std::function<void(uint8_t)>                             _keyCallback;
    std::function<void(uint8_t, int16_t, int16_t)>           _touchCallback;

    void dispatch(uint8_t cmd, const uint8_t *payload, size_t payloadLen);
};
