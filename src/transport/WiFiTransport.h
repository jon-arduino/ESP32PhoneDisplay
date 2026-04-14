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

// -----------------------------------------------------------------------------
//  WiFiTransport — WiFi transport for ESP32PhoneDisplay
//
//  Connects to WiFi, advertises via mDNS, accepts a TCP connection from the
//  iPhone app, and streams GFX commands over TCP.
//
//  GFX bytes are accumulated in an internal buffer and sent as a single TCP
//  write on flush(). This batching avoids Nagle algorithm delays and
//  head-of-line blocking on iOS.
//
//  A background FreeRTOS task handles:
//    - Ping/pong heartbeat (keeps iPhone connection alive)
//    - Auto-flush (sends buffered bytes within 100ms if flush() not called)
//
//  Nothing required in loop() — the task manages everything automatically.
//
//  Usage:
//    WiFiTransport transport("MySSID", "MyPassword", "esp32-display");
//    transport.setSoftAP("ESP32-Display", "password123");  // optional fallback
//    transport.begin();
//
//    while (!transport.canSend()) { delay(100); }  // wait for iPhone
//    display.begin(240, 320);
//    display.fillScreen(0x0000);
//    display.flush();   // recommended — not required
// -----------------------------------------------------------------------------

class WiFiTransport : public GraphicsTransport
{
public:
    WiFiTransport(const char *ssid,
                  const char *password,
                  const char *mdnsHostname = "esp32-display",
                  uint16_t    tcpPort      = 9000);

    // ── SoftAP fallback ───────────────────────────────────────────────────────
    // Call before begin(). If STA connection fails within staTimeoutMs,
    // ESP32 starts its own network. iPhone joins it manually then connects.
    void setSoftAP(const char *apSsid,
                   const char *apPassword,
                   uint32_t    staTimeoutMs = 15000)
    {
        _apSsid       = apSsid;
        _apPassword   = apPassword;
        _staTimeoutMs = staTimeoutMs;
    }

    // Connect to WiFi, start mDNS + TCP server, spawn background task.
    // Blocks until WiFi associates (or SoftAP fallback starts).
    // Does not wait for iPhone to connect — use canSend() to check.
    void begin();

    // ── GraphicsTransport interface ───────────────────────────────────────────

    // True when an iPhone TCP client is connected and ready.
    bool canSend() const override;

    // Accumulate bytes into internal buffer. Thread-safe.
    // Nothing sent to TCP until flush() or auto-flush (within 100ms).
    void send(const uint8_t *data, uint16_t len) override;

    // Send entire accumulated buffer as one TCP write.
    // Call at explicit frame boundaries for deterministic rendering.
    // Not required — auto-flush sends within 100ms if not called.
    void flush() override;

    // Discard buffered bytes without sending.
    // Called automatically on disconnect to prevent stale data.
    void reset() override;

    // ── Touch back-channel ────────────────────────────────────────────────────
    void onTouch(std::function<void(uint8_t cmd,
                                    int16_t x,
                                    int16_t y,
                                    uint8_t z)> cb) override
    {
        _bc.onTouch(cb);
    }

    // ── Status ────────────────────────────────────────────────────────────────
    bool isConnected()   const;   // WiFi associated (not necessarily iPhone connected)
    bool isInAPMode()    const { return _apMode; }

    // ── Configuration ─────────────────────────────────────────────────────────
    void setHeartbeat(uint32_t pingIntervalMs, uint32_t pongTimeoutMs)
    {
        _pingIntervalMs = pingIntervalMs;
        _pongTimeoutMs  = pongTimeoutMs;
        _pingEarlyMs    = pingIntervalMs / 3;
    }

    void setPingEarlyMs(uint32_t earlyMs) { _pingEarlyMs = earlyMs; }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void onConnected   (std::function<void()> cb) { _onConnected    = cb; }
    void onDisconnected(std::function<void()> cb) { _onDisconnected = cb; }
    void onFirstPong   (std::function<void()> cb) { _onFirstPong    = cb; }
    void onKey         (std::function<void(uint8_t)> cb) { _bc.onKey(cb); }

    // ── SoftAP switch ─────────────────────────────────────────────────────────
    // Drop current connection and switch to SoftAP mode on demand.
    // Requires setSoftAP() to have been called first.
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

    // ── TCP ───────────────────────────────────────────────────────────────────
    AsyncServer *_server = nullptr;
    AsyncClient *_client = nullptr;

    // ── TX buffer ─────────────────────────────────────────────────────────────
    // Accumulates GFX bytes between flush() calls.
    // Mutex protects both _txBuf (in send()) and TCP writes (in flushLocked(),
    // sendPingNow()) from concurrent access between the background task and
    // any task calling send()/flush().
    std::vector<uint8_t> _txBuf;
    uint32_t             _lastSendMs = 0;
    static constexpr size_t TX_BUF_RESERVE = 4096;
    static constexpr uint32_t AUTO_FLUSH_MS = 100;

    SemaphoreHandle_t _writeMutex = nullptr;

    // ── Heartbeat ─────────────────────────────────────────────────────────────
    uint32_t _pingIntervalMs   = 3000;
    uint32_t _pongTimeoutMs    = 9000;
    uint32_t _pingEarlyMs      = 1000;
    uint32_t _lastPingSentMs   = 0;
    bool     _waitingForPong   = false;
    bool     _pingNeeded       = false;
    uint8_t  _loggedThresholds = 0;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void()> _onConnected;
    std::function<void()> _onDisconnected;
    std::function<void()> _onFirstPong;
    bool _firstPongReceived   = false;

    BackChannelParser _bc;

    // ── Background task ───────────────────────────────────────────────────────
    TaskHandle_t _taskHandle = nullptr;
    static void  taskFunc(void *arg);

    // ── Private methods ───────────────────────────────────────────────────────
    void update();      // heartbeat — called from task, zero-timeout mutex take
    void autoFlush();   // flush if bytes idle > AUTO_FLUSH_MS — called from task

    // Send _txBuf over TCP — caller must hold _writeMutex
    void flushLocked();

    // Send ping frame — caller must hold _writeMutex
    void sendPingNow();

    void startMDNS();
    void startTCPServer();
    void startSoftAP();
    void spawnTask();

    void onClientConnected(AsyncClient *client);
    void dropClient(const char *reason);
};