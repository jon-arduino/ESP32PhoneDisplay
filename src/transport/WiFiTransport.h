#pragma once

#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
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
//    send()  — writes framed packet [lenLo][lenHi][data] into ring buffer.
//              Returns immediately. Notifies transport task.
//    flush() — notifies transport task to drain ring now.
//    Transport task (core 0) — sole writer to TCP socket.
//              Drains ring buffer, sends pings, handles auto-flush.
//              No mutex needed — single writer on TCP socket.
//
//  Ring buffer:
//    Fixed 8KB SPSC ring. Producer: send() on loop/any task.
//    Consumer: transport task. std::atomic head/tail for thread safety.
//    Packets stored as [lenLo][lenHi][data...] — never split on send.
//
//  Ping/pong:
//    Managed by PingPong object. Transport task checks pingNeeded() and
//    sends ping between packets — never mid-packet.
//
//  Nothing required in loop() — transport task manages everything.
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

    // Connect to WiFi, start mDNS + TCP server, spawn transport task.
    void begin();

    // ── GraphicsTransport interface ───────────────────────────────────────────

    // True when an iPhone TCP client is connected.
    bool canSend() const override;

    // Write framed packet into ring buffer. Blocks briefly if ring is full.
    // Never drops — blocks until space is available (max ~50ms warning).
    void send(const uint8_t *data, uint16_t len) override;

    // Notify transport task to drain ring immediately.
    void flush() override;

    // Discard ring buffer contents — called on disconnect.
    void reset() override;

    // ── Touch back-channel ────────────────────────────────────────────────────
    void onTouch(std::function<void(uint8_t, int16_t, int16_t, uint8_t)> cb) override
    {
        _bc.onTouch(cb);
    }

    // ── Status ────────────────────────────────────────────────────────────────
    bool isConnected() const;
    bool isInAPMode()  const { return _apMode; }

    // ── Configuration ─────────────────────────────────────────────────────────
    void setHeartbeat(uint32_t pingIntervalMs, uint32_t pongTimeoutMs)
    {
        _ping.setInterval(pingIntervalMs);
        _ping.setTimeout(pongTimeoutMs);
    }

    // ── RTT statistics ────────────────────────────────────────────────────────
    uint32_t rttLast()  const { return _ping.rttLast();  }
    uint32_t rttMin()   const { return _ping.rttMin();   }
    uint32_t rttMax()   const { return _ping.rttMax();   }
    uint32_t rttAvg()   const { return _ping.rttAvg();   }
    uint32_t rttCount() const { return _ping.rttCount(); }
    void     resetRttStats()  { _ping.resetStats();      }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void onConnected   (std::function<void()> cb)          { _onConnected    = cb; }
    void onDisconnected(std::function<void()> cb)          { _onDisconnected = cb; }
    void onFirstPong   (std::function<void()> cb)          { _ping.onFirstPong(cb); }
    void onRtt         (std::function<void(uint32_t)> cb)  { _ping.onRtt(cb); }
    void onKey         (std::function<void(uint8_t)> cb)   { _bc.onKey(cb);  }

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

    // ── TCP ───────────────────────────────────────────────────────────────────
    AsyncServer *_server = nullptr;
    AsyncClient *_client = nullptr;

    // ── Ring buffer (SPSC — send() produces, transport task consumes) ─────────
    static constexpr size_t    TX_BUF_SIZE      = 16384;  // ring buffer
    static constexpr size_t    MAX_PAYLOAD_SIZE = 8192;   // max single send() call
    static constexpr uint32_t  AUTO_FLUSH_MS = 100;

    uint8_t                  _txBuf[TX_BUF_SIZE];
    std::atomic<uint32_t>    _head{0};   // producer index (send())
    std::atomic<uint32_t>    _tail{0};   // consumer index (transport task)
    std::atomic<bool>        _flushRequested{false};
    uint32_t                 _lastDrainMs = 0;

    // ── Transport task ────────────────────────────────────────────────────────
    TaskHandle_t _taskHandle = nullptr;
    static void  taskFunc(void *arg);
    void         runTask();
    void         drainRing();
    void         sendPingFrame();
    void         spawnTask();

    // ── Ring buffer helpers ───────────────────────────────────────────────────
    uint32_t ringFree() const;
    uint32_t ringUsed() const;

    // ── Ping/pong ─────────────────────────────────────────────────────────────
    PingPong _ping;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void()> _onConnected;
    std::function<void()> _onDisconnected;
    BackChannelParser     _bc;

    // ── Private helpers ───────────────────────────────────────────────────────
    void startMDNS();
    void startTCPServer();
    void startSoftAP();
    void onClientConnected(AsyncClient *client);
    void dropClient(const char *reason);
};