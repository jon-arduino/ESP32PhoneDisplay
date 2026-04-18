#include "WiFiTransport.h"
#include <string.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Design notes
//
//  send() accumulates GFX bytes into _txBuf (std::vector) under _writeMutex.
//  flush() drains _txBuf to TCP under _writeMutex.
//  A background task handles ping/pong and auto-flush — it takes _writeMutex
//  with zero timeout so it never blocks loop().
//
//  _client->write() is only ever called while _writeMutex is held, which
//  serialises all TCP writes and prevents concurrent access to AsyncTCP.
//
//  The task sleeps for TASK_SLEEP_MS between iterations so IDLE and other
//  tasks get CPU time — no watchdog starvation.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t TASK_SLEEP_MS  = 10;   // task yield interval
static constexpr uint32_t AUTO_FLUSH_MS  = 100;  // max time bytes sit in _txBuf
static constexpr uint32_t WRITE_TIMEOUT  = 2000; // max ms for a single TCP write

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────
WiFiTransport::WiFiTransport(const char *ssid, const char *password,
                             const char *mdnsHostname, uint16_t tcpPort)
    : _ssid(ssid), _password(password),
      _mdnsHostname(mdnsHostname), _tcpPort(tcpPort)
{
    _bc.onPong([this]() {
        _ping.onPongReceived();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  begin()
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::begin()
{
    // Heap allocations here — safe, called from setup() after scheduler starts
    if (_writeMutex == nullptr) {
        _writeMutex = xSemaphoreCreateMutex();
        configASSERT(_writeMutex);
    }
    _txBuf.reserve(TX_BUF_RESERVE);

    WiFi.mode(_apSsid ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(_ssid, _password);
    Serial.print("[WiFi] Connecting");

    uint32_t start   = millis();
    uint32_t timeout = _apSsid ? _staTimeoutMs : portMAX_DELAY;

    while (WiFi.status() != WL_CONNECTED) {
        if (_apSsid && (millis() - start >= timeout)) {
            Serial.println("\n[WiFi] STA timeout — starting SoftAP");
            WiFi.disconnect(true);
            startSoftAP();
            return;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.printf("\n[WiFi] Connected — IP: %s\n",
                  WiFi.localIP().toString().c_str());
    _apMode = false;
    WiFi.setSleep(_powerSave);   // apply power save setting
    startMDNS();
    startTCPServer();
    spawnTask();
}

// ─────────────────────────────────────────────────────────────────────────────
//  GraphicsTransport interface
// ─────────────────────────────────────────────────────────────────────────────

bool WiFiTransport::canSend() const
{
    return _client != nullptr && _client->connected();
}

bool WiFiTransport::isConnected() const
{
    return _apMode || (WiFi.status() == WL_CONNECTED);
}

// Accumulate bytes into _txBuf — called from loop()/sketch task.
// Takes mutex with portMAX_DELAY — suspends properly, IDLE runs, no watchdog.
void WiFiTransport::send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || !canSend()) return;

    xSemaphoreTake(_writeMutex, portMAX_DELAY);
    _txBuf.insert(_txBuf.end(), data, data + len);
    _lastSendMs = millis();
    xSemaphoreGive(_writeMutex);
}

// Drain _txBuf to TCP — called from sketch at frame boundaries.
// Takes mutex with portMAX_DELAY.
void WiFiTransport::flush()
{
    xSemaphoreTake(_writeMutex, portMAX_DELAY);
    flushLocked();
    xSemaphoreGive(_writeMutex);
}

// Discard buffer — called on disconnect.
void WiFiTransport::reset()
{
    xSemaphoreTake(_writeMutex, portMAX_DELAY);
    _txBuf.clear();
    _lastSendMs = 0;
    xSemaphoreGive(_writeMutex);
}

// ─────────────────────────────────────────────────────────────────────────────
//  flushLocked() — caller MUST hold _writeMutex
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::flushLocked()
{
    if (_txBuf.empty() || !canSend()) {
        _txBuf.clear();
        _lastSendMs = 0;
        return;
    }

    size_t   total   = _txBuf.size();
    size_t   sent    = 0;
    uint32_t start   = millis();

    while (sent < total) {
        size_t space = _client->space();
        if (space == 0) {
            if (millis() - start > WRITE_TIMEOUT) {
                Serial.printf("[WiFi] flush timeout — sent %u/%u bytes\n",
                              sent, total);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t chunk   = min(space, total - sent);
        size_t written = _client->write(
            reinterpret_cast<const char *>(_txBuf.data() + sent), chunk);
        if (written == 0) {
            // AsyncTCP not ready — brief yield and retry
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        sent  += written;
        start  = millis();
    }

    _txBuf.clear();
    _lastSendMs  = 0;
    _lastFlushMs = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendPingLocked() — caller MUST hold _writeMutex
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::sendPingLocked()
{
    if (!canSend()) return;
    uint8_t frame[4] = {0xA5, 0x01, 0x00, GFX_CMD_PING};
    size_t written = _client->write(reinterpret_cast<const char *>(frame), 4);
    if (written == 4) _ping.onPingSent();
    // if write failed, pingNeeded() stays true — task retries next cycle
}

// ─────────────────────────────────────────────────────────────────────────────
//  Background task — ping/pong heartbeat + auto-flush
//
//  Sleeps TASK_SLEEP_MS between iterations so IDLE and other tasks run.
//  Takes _writeMutex with zero timeout — never blocks loop().
//  If mutex is busy (sketch calling send/flush), skips this cycle.
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::taskFunc(void *arg)
{
    static_cast<WiFiTransport *>(arg)->runTask();
}

void WiFiTransport::runTask()
{
    for (;;) {
        // Sleep first — gives IDLE and loopTask guaranteed CPU time each cycle
        vTaskDelay(pdMS_TO_TICKS(TASK_SLEEP_MS));

        if (!canSend()) {
            _ping.onDisconnected();
            continue;
        }

        _ping.tick(millis());

        if (_ping.isTimedOut()) {
            dropClient("pong timeout");
            continue;
        }

        // Zero-timeout mutex take — never blocks loop()
        if (xSemaphoreTake(_writeMutex, 0) != pdTRUE) continue;

        // Send ping if due — between GFX data, never mid-frame
        if (_ping.pingNeeded()) sendPingLocked();

        // Auto-flush — flush at least every AUTO_FLUSH_MS regardless of
        // how often send() is called. Prevents bursty display updates.
        if (!_txBuf.empty() &&
            millis() - _lastFlushMs >= AUTO_FLUSH_MS) {
            flushLocked();
        }

        xSemaphoreGive(_writeMutex);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  switchToSoftAP()
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::switchToSoftAP()
{
    if (!_apSsid) {
        Serial.println("[WiFi] switchToSoftAP: call setSoftAP() first");
        return;
    }
    if (_apMode) return;

    Serial.println("[WiFi] Switching to SoftAP...");

    _ping.onDisconnected();
    _bc.reset();

    if (_onDisconnected) _onDisconnected();

    if (_client) { _client->close(); _client = nullptr; }
    if (_server) { _server->end(); delete _server; _server = nullptr; }
    MDNS.end();

    reset();
    WiFi.disconnect(true);
    startSoftAP();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private helpers
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::startSoftAP()
{
    _apMode = true;
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(_apSsid, _apPassword)) {
        Serial.println("[WiFi] ERROR: softAP() failed");
        return;
    }
    IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    Serial.printf("[WiFi] SoftAP — SSID: %s  IP: %s\n",
                  _apSsid, WiFi.softAPIP().toString().c_str());
    WiFi.setSleep(_powerSave);
    startMDNS();
    startTCPServer();
    spawnTask();
}

void WiFiTransport::startMDNS()
{
    if (!MDNS.begin(_mdnsHostname)) {
        Serial.println("[WiFi] ERROR: mDNS failed");
        return;
    }
    MDNS.addService("uart", "tcp", _tcpPort);
    MDNS.addServiceTxt("uart", "tcp", "board", "ESP32");
    MDNS.addServiceTxt("uart", "tcp", "version", "1.0");
    Serial.printf("[WiFi] mDNS: %s.local port %d\n", _mdnsHostname, _tcpPort);
}

void WiFiTransport::startTCPServer()
{
    _server = new AsyncServer(_tcpPort);
    _server->onClient([](void *arg, AsyncClient *client) {
        static_cast<WiFiTransport *>(arg)->onClientConnected(client);
    }, this);
    _server->begin();
    Serial.printf("[WiFi] TCP server on port %d\n", _tcpPort);
}

void WiFiTransport::spawnTask()
{
    if (_taskHandle != nullptr) return;
    // Core 0 — away from loopTask (core 1), alongside WiFi stack.
    // Task sleeps TASK_SLEEP_MS each cycle so IDLE0 runs freely.
    xTaskCreatePinnedToCore(
        taskFunc, "wifi_transport", 4096, this, 1, &_taskHandle, 0);
}

void WiFiTransport::onClientConnected(AsyncClient *client)
{
    if (_client) {
        AsyncClient *prev = _client;
        _client = nullptr;
        prev->close();
    }

    _client = client;
    _client->setNoDelay(true);

    _bc.reset();
    reset();   // clears _txBuf under mutex — safe against background task
    _ping.onConnected();

    Serial.printf("[WiFi] iPhone connected from %s\n",
                  client->remoteIP().toString().c_str());

    if (_onConnected) _onConnected();

    client->onData([](void *arg, AsyncClient*, void *data, size_t len) {
        static_cast<WiFiTransport *>(arg)->_bc.feed(
            static_cast<uint8_t *>(data), len);
    }, this);

    client->onDisconnect([](void *arg, AsyncClient *c) {
        auto *self = static_cast<WiFiTransport *>(arg);
        Serial.println("[WiFi] iPhone disconnected");
        if (self->_client == c) {
            self->_client = nullptr;
            self->_ping.onDisconnected();
            if (self->_writeMutex) {
                xSemaphoreTake(self->_writeMutex, portMAX_DELAY);
                self->_txBuf.clear();
                self->_lastSendMs = 0;
                xSemaphoreGive(self->_writeMutex);
            }
            if (self->_onDisconnected) self->_onDisconnected();
        }
        delete c;
    }, this);

    client->onError([](void *arg, AsyncClient *c, int8_t error) {
        auto *self = static_cast<WiFiTransport *>(arg);
        Serial.printf("[WiFi] Client error: %d\n", error);
        if (self->_client == c) {
            self->_client = nullptr;
            self->_ping.onDisconnected();
            if (self->_writeMutex) {
                xSemaphoreTake(self->_writeMutex, portMAX_DELAY);
                self->_txBuf.clear();
                self->_lastSendMs = 0;
                xSemaphoreGive(self->_writeMutex);
            }
            if (self->_onDisconnected) self->_onDisconnected();
        }
    }, this);

    client->onTimeout([](void*, AsyncClient *c, uint32_t time) {
        Serial.printf("[WiFi] TCP timeout at %u ms\n", time);
        c->close();
    }, this);
}

void WiFiTransport::dropClient(const char *reason)
{
    Serial.printf("[WiFi] Dropping client: %s\n", reason);
    _ping.onDisconnected();
    if (_client) _client->close();
}