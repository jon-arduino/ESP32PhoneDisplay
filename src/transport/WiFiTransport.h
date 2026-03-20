#pragma once

#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <freertos/semphr.h>

#include "../Protocol.h"
#include "../BackChannelParser.h"

using DataCallback = std::function<void(const uint8_t *data, size_t len)>;

class WiFiManager
{
public:
    WiFiManager(const char *ssid, const char *password,
                const char *mdnsHostname = "esp32-uart",
                uint16_t tcpPort = 9000);

    // ── SoftAP fallback ───────────────────────────────────────────────────────
    // Call before begin() to enable SoftAP fallback when STA connection fails.
    // If not called, begin() blocks indefinitely waiting for STA (original behaviour).
    // staTimeoutMs: how long to try STA before giving up and starting SoftAP.
    void setSoftAP(const char *apSsid,
                   const char *apPassword,
                   uint32_t    staTimeoutMs = 15000)
    {
        _apSsid       = apSsid;
        _apPassword   = apPassword;
        _staTimeoutMs = staTimeoutMs;
    }

    // Returns true if operating as SoftAP (iPhone must join ESP32's network).
    // Returns false if connected to a router in STA mode (normal operation).
    bool isInAPMode() const { return _apMode; }

    // Tear down current WiFi connection and bring up SoftAP immediately.
    // Requires setSoftAP() to have been called with credentials beforehand.
    // Safe to call from loop() / console handler.
    void switchToSoftAP();

    void begin();

    // ── Configuration ─────────────────────────────────────────────────────────
    void setHeartbeat(uint32_t pingIntervalMs, uint32_t pongTimeoutMs)
    {
        _pingIntervalMs = pingIntervalMs;
        _pongTimeoutMs  = pongTimeoutMs;
        // Default early-send window = 1/3 of interval (e.g. 1000ms for 3s interval).
        // send() will pre-emptively ping if a frame arrives within this window
        // of the next scheduled ping, preventing a full frame from delaying it.
        _pingEarlyMs = pingIntervalMs / 3;
    }

    // Override the early-send window if the default isn't right for your
    // frame sizes. Set to 0 to disable early send (original behaviour).
    void setPingEarlyMs(uint32_t earlyMs) { _pingEarlyMs = earlyMs; }

    // ── Status ────────────────────────────────────────────────────────────────
    bool isConnected() const;
    bool clientConnected() const { return _client != nullptr; }
    size_t clientSpace() const { return _client ? _client->space() : 0; }

    // ── GFX data send (any task, serialised by _writeMutex) ───────────────────
    // Takes _writeMutex for the full frame write.
    // On entry: if a ping is due or within _pingEarlyMs of due, sends it first
    //           so it lands before the frame rather than being delayed by it.
    // On exit:  if _pingNeeded is still set, sends ping at the frame boundary
    //           before releasing the mutex.
    void send(const uint8_t *data, size_t len);
    void send(const char *str);

    // Framed back-channel command (used internally for ping; public for sendCmd)
    void sendCmd(uint8_t cmd, const uint8_t *payload = nullptr, size_t payloadLen = 0);

    // ── Heartbeat + maintenance (heartbeat task, ~100ms) ──────────────────────
    void update();

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void onData(DataCallback cb) { _dataCallback = cb; }
    void onConnected(void (*cb)()) { _onConnected = cb; }
    void onDisconnected(void (*cb)()) { _onDisconnected = cb; }
    void onFirstPong(void (*cb)()) { _onFirstPong = cb; }
    void onKey(void (*cb)(uint8_t key)) { _bc.onKey(cb); }
    void onTouch(void (*cb)(uint8_t cmd, int16_t x, int16_t y)) { _bc.onTouch(cb); }

private:
    // ── STA credentials ───────────────────────────────────────────────────────
    const char *_ssid;
    const char *_password;

    // ── SoftAP config (optional) ──────────────────────────────────────────────
    const char *_apSsid       = nullptr;
    const char *_apPassword   = nullptr;
    uint32_t    _staTimeoutMs = 15000;
    bool        _apMode       = false;

    // ── Network / TCP ─────────────────────────────────────────────────────────
    const char  *_mdnsHostname;
    uint16_t     _tcpPort;

    AsyncServer *_server = nullptr;
    AsyncClient *_client = nullptr;

    SemaphoreHandle_t _writeMutex = nullptr;

    uint32_t _pingIntervalMs   = 3000;
    uint32_t _pongTimeoutMs    = 9000;
    uint32_t _pingEarlyMs      = 1000;
    uint32_t _lastPingSentMs   = 0;
    bool     _waitingForPong   = false;
    bool     _pingNeeded       = false;
    uint8_t  _loggedThresholds = 0;

    DataCallback _dataCallback;
    void (*_onConnected)()    = nullptr;
    void (*_onDisconnected)() = nullptr;
    void (*_onFirstPong)()    = nullptr;
    bool _firstPongReceived   = false;

    BackChannelParser _bc;

    void sendPingNow();
    void startMDNS();
    void startTCPServer();
    void startSoftAP();
    void onClientConnected(AsyncClient *client);
    void dropClient(const char *reason);
};
