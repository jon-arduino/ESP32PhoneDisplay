#pragma once

#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <freertos/semphr.h>
#include <vector>
#include <functional>

#include "../GraphicsTransport.h"
#include "../GraphicsProtocol.h"
#include "../Protocol.h"
#include "../BackChannelParser.h"
#include "PingPong.h"

// -----------------------------------------------------------------------------
//  WiFiTransport — WiFi transport for ESP32PhoneDisplay
//
//  Architecture:
//    send()  — accumulates GFX bytes into _txBuf under _writeMutex.
//              Takes mutex with portMAX_DELAY — suspends properly, IDLE runs.
//    flush() — drains _txBuf to TCP under _writeMutex.
//    Background task (core 0, priority 1):
//              Sleeps 10ms each cycle — IDLE0 always gets CPU time.
//              Takes _writeMutex with zero timeout (never blocks loop()).
//              Sends ping when due, auto-flushes when bytes sit idle >100ms.
//
//  _client->write() is only called while _writeMutex is held — serialises
//  all TCP writes, prevents concurrent access to AsyncTCP internals.
//
//  Nothing required in loop() — task manages heartbeat and auto-flush.
// -----------------------------------------------------------------------------

class WiFiTransport : public GraphicsTransport
{
public:
    WiFiTransport(const char *ssid,
                  const char *password,
                  const char *mdnsHostname = "esp32-display",
                  uint16_t    tcpPort      = 9000);

    // ── SoftAP fallback ───────────────────────────────────────────────────────
    void setSoftAP(const char *apSsid,
                   const char *apPassword,
                   uint32_t    staTimeoutMs = 15000)
    {
        _apSsid       = apSsid;
        _apPassword   = apPassword;
        _staTimeoutMs = staTimeoutMs;
    }

    // Connect WiFi, start mDNS + TCP server, spawn background task.
    void begin();

    // ── GraphicsTransport interface ───────────────────────────────────────────
    bool canSend() const override;
    void send(const uint8_t *data, uint16_t len) override;
    void flush() override;
    void reset() override;

    void onTouch(std::function<void(uint8_t, int16_t, int16_t, uint8_t)> cb) override
    {
        _bc.onTouch(cb);
    }

    // Back-channel parser diagnostics
    BackChannelParser::Stats bcStats() const { return _bc.getStats(); }
    void resetBcStats()                      { _bc.resetStats(); }

    // ── Status ────────────────────────────────────────────────────────────────
    bool isConnected() const;
    bool isInAPMode()  const { return _apMode; }

    // ── RTT statistics ────────────────────────────────────────────────────────
    uint32_t rttLast()  const { return _ping.rttLast();  }
    uint32_t rttMin()   const { return _ping.rttMin();   }
    uint32_t rttMax()   const { return _ping.rttMax();   }
    uint32_t rttAvg()   const { return _ping.rttAvg();   }
    uint32_t rttCount() const { return _ping.rttCount(); }
    void     resetRttStats()  { _ping.resetStats();      }

    // ── Configuration ─────────────────────────────────────────────────────────
    void setHeartbeat(uint32_t pingIntervalMs, uint32_t pongTimeoutMs)
    {
        _ping.setInterval(pingIntervalMs);
        _ping.setTimeout(pongTimeoutMs);
    }

    // WiFi power saving — enabled by default (good for battery devices).
    // Disable for low latency interactive apps — trades power for ~5ms RTT.
    // Can be called before or after begin(); applied on connect/reconnect.
    void setPowerSave(bool enable)
    {
        _powerSave = enable;
        if (isConnected()) WiFi.setSleep(enable);
    }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void onConnected   (std::function<void()> cb)         { _onConnected    = cb; }
    void onDisconnected(std::function<void()> cb)         { _onDisconnected = cb; }
    void onFirstPong   (std::function<void()> cb)         { _ping.onFirstPong(cb); }
    void onRtt         (std::function<void(uint32_t)> cb) { _ping.onRtt(cb); }
    void onKey         (std::function<void(uint8_t)> cb)  { _bc.onKey(cb);  }

    // ── SoftAP switch ─────────────────────────────────────────────────────────
    void switchToSoftAP();

private:
    // ── Credentials ───────────────────────────────────────────────────────────
    const char *_ssid;
    const char *_password;
    const char *_mdnsHostname;
    uint16_t    _tcpPort;

    const char *_apSsid       = nullptr;
    const char *_apPassword   = nullptr;
    uint32_t    _staTimeoutMs = 15000;
    bool        _apMode       = false;
    bool        _powerSave     = true;   // WiFi modem sleep — on by default

    // ── TCP ───────────────────────────────────────────────────────────────────
    AsyncServer *_server = nullptr;
    AsyncClient *_client = nullptr;

    // ── TX buffer ─────────────────────────────────────────────────────────────
    // Accumulates GFX bytes between flush() calls.
    // Protected by _writeMutex — send() and flush() both hold it.
    // Task holds zero-timeout take — never blocks loop().
    static constexpr size_t TX_BUF_RESERVE = 4096;

    std::vector<uint8_t> _txBuf;
    uint32_t             _lastSendMs  = 0;  // last send() call time
    uint32_t             _lastFlushMs = 0;  // last flush time (auto-flush reference)
    SemaphoreHandle_t    _writeMutex = nullptr;

    // ── Ping/pong ─────────────────────────────────────────────────────────────
    PingPong _ping;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void()> _onConnected;
    std::function<void()> _onDisconnected;
    BackChannelParser     _bc;

    // ── Background task ───────────────────────────────────────────────────────
    TaskHandle_t _taskHandle = nullptr;
    static void  taskFunc(void *arg);
    void         runTask();
    void         flushLocked();    // caller must hold _writeMutex
    void         sendPingLocked(); // caller must hold _writeMutex
    void         spawnTask();

    // ── Private helpers ───────────────────────────────────────────────────────
    void startMDNS();
    void startTCPServer();
    void startSoftAP();
    void onClientConnected(AsyncClient *client);
    void dropClient(const char *reason);
};