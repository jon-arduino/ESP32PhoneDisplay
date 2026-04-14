#include "WiFiTransport.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────
WiFiTransport::WiFiTransport(const char *ssid, const char *password,
                             const char *mdnsHostname, uint16_t tcpPort)
    : _ssid(ssid), _password(password),
      _mdnsHostname(mdnsHostname), _tcpPort(tcpPort)
{
    _writeMutex = xSemaphoreCreateMutex();
    configASSERT(_writeMutex);

    _txBuf.reserve(TX_BUF_RESERVE);

    _bc.onPong([this]() {
        _waitingForPong    = false;
        _loggedThresholds  = 0;
        if (!_firstPongReceived) {
            _firstPongReceived = true;
            if (_onFirstPong) _onFirstPong();
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  begin() — connect to WiFi, start services, spawn background task
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::begin()
{
    WiFi.mode(_apSsid ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(_ssid, _password);
    Serial.print("[WiFi] Connecting to STA");

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

// Accumulate bytes into _txBuf. Protected by mutex.
void WiFiTransport::send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || !canSend()) return;

    xSemaphoreTake(_writeMutex, portMAX_DELAY);

    // Early ping check — send ping before frame if interval nearly elapsed.
    // Prevents a large frame holding the mutex while a ping sits overdue.
    if (_pingEarlyMs > 0 && _lastPingSentMs > 0) {
        uint32_t elapsed = millis() - _lastPingSentMs;
        if (elapsed + _pingEarlyMs >= _pingIntervalMs)
            sendPingNow();
    }
    if (_pingNeeded) sendPingNow();

    _txBuf.insert(_txBuf.end(), data, data + len);
    _lastSendMs = millis();

    xSemaphoreGive(_writeMutex);
}

// Send accumulated buffer as one TCP write. Explicit frame boundary.
void WiFiTransport::flush()
{
    xSemaphoreTake(_writeMutex, portMAX_DELAY);
    flushLocked();
    xSemaphoreGive(_writeMutex);
}

// flushLocked() — caller MUST hold _writeMutex
void WiFiTransport::flushLocked()
{
    if (_txBuf.empty() || !canSend()) {
        _txBuf.clear();
        _lastSendMs = 0;
        return;
    }

    size_t sent = 0;
    size_t len  = _txBuf.size();
    const uint32_t timeoutMs = 2000;
    uint32_t start = millis();

    while (sent < len) {
        size_t available = _client->space();
        if (available == 0) {
            if (millis() - start > timeoutMs) {
                Serial.printf("[WiFi] flush timeout — dropped %d bytes\n",
                              (int)(len - sent));
                break;
            }
            delay(1);
            continue;
        }
        size_t chunk   = min(available, len - sent);
        size_t written = _client->write(
            reinterpret_cast<const char *>(_txBuf.data() + sent), chunk);
        if (written == 0) break;
        sent  += written;
        start  = millis();
    }

    // Exit ping check — interval may have elapsed during a slow TCP drain
    if (_pingNeeded) sendPingNow();

    _txBuf.clear();
    _lastSendMs = 0;
}

// Discard buffered bytes — called on disconnect
void WiFiTransport::reset()
{
    xSemaphoreTake(_writeMutex, portMAX_DELAY);
    _txBuf.clear();
    _lastSendMs = 0;
    xSemaphoreGive(_writeMutex);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Background task — heartbeat + auto-flush, every 100ms
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::taskFunc(void *arg)
{
    WiFiTransport *self = static_cast<WiFiTransport *>(arg);
    for (;;) {
        self->update();
        self->autoFlush();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// update() — ping/pong heartbeat. Zero-timeout mutex — never blocks task.
// If mutex is held by send()/flush(), sets _pingNeeded and returns.
// send()/flush() check _pingNeeded at entry/exit and send the ping then.
void WiFiTransport::update()
{
    if (!canSend()) {
        _waitingForPong = false;
        _lastPingSentMs = 0;
        _pingNeeded     = false;
        return;
    }

    uint32_t now = millis();

    // Pong watchdog
    if (_waitingForPong) {
        uint32_t el = now - _lastPingSentMs;
        if (el >= 500 && !(_loggedThresholds & 1)) {
            _loggedThresholds |= 1;
            Serial.printf("[WiFi] Ping late %ums — loop may be busy\n", el);
        }
        if (el >= 1500 && !(_loggedThresholds & 2)) {
            _loggedThresholds |= 2;
            Serial.printf("[WiFi] Ping late %ums — WARNING\n", el);
        }
        if (el >= 6000 && !(_loggedThresholds & 4)) {
            _loggedThresholds |= 4;
            Serial.printf("[WiFi] Ping late %ums — CRITICAL\n", el);
        }
        if (now - _lastPingSentMs >= _pongTimeoutMs) {
            dropClient("pong timeout");
            return;
        }
    }

    // Schedule ping if interval elapsed
    if (now - _lastPingSentMs >= _pingIntervalMs)
        _pingNeeded = true;

    if (!_pingNeeded) return;

    // Zero-timeout take — if busy, send() will catch _pingNeeded at next frame
    if (xSemaphoreTake(_writeMutex, 0) == pdTRUE) {
        sendPingNow();
        xSemaphoreGive(_writeMutex);
    }
}

// autoFlush() — flush if bytes have been sitting idle for AUTO_FLUSH_MS.
// Safety net for sketches that never call flush() explicitly.
void WiFiTransport::autoFlush()
{
    if (_lastSendMs == 0) return;   // nothing buffered

    // Zero-timeout — don't block the task. If send()/flush() holds the
    // mutex, bytes are actively being written — skip this cycle.
    if (xSemaphoreTake(_writeMutex, 0) != pdTRUE) return;

    if (!_txBuf.empty() && (millis() - _lastSendMs >= AUTO_FLUSH_MS))
        flushLocked();

    xSemaphoreGive(_writeMutex);
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendPingNow() — caller MUST hold _writeMutex
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::sendPingNow()
{
    if (!canSend()) { _pingNeeded = false; return; }

    uint8_t frame[4] = {0xA5, 0x01, 0x00, GFX_CMD_PING};
    _client->write(reinterpret_cast<const char *>(frame), 4);
    _lastPingSentMs   = millis();
    _waitingForPong   = true;
    _loggedThresholds = 0;
    _pingNeeded       = false;
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

    // Reset state
    _waitingForPong    = false;
    _pingNeeded        = false;
    _firstPongReceived = false;
    _lastPingSentMs    = 0;
    _loggedThresholds  = 0;
    _bc.reset();

    if (_onDisconnected) _onDisconnected();

    if (_client) { _client->close(); _client = nullptr; }
    if (_server) { _server->end(); delete _server; _server = nullptr; }
    MDNS.end();

    reset();   // discard buffered bytes

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
    Serial.printf("[WiFi] SoftAP up — SSID: %s  IP: %s\n",
                  _apSsid, WiFi.softAPIP().toString().c_str());
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
    if (_taskHandle != nullptr) return;  // already running
    xTaskCreatePinnedToCore(
        taskFunc, "wifi_transport", 4096, this, 1, &_taskHandle, 1);
}

void WiFiTransport::onClientConnected(AsyncClient *client)
{
    // Drop any existing client
    if (_client) {
        AsyncClient *prev = _client;
        _client = nullptr;
        prev->close();
    }

    _client            = client;
    _bc.reset();
    reset();                    // discard stale buffered bytes
    _waitingForPong    = false;
    _pingNeeded        = false;
    _lastPingSentMs    = millis();
    _firstPongReceived = false;

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
            self->_client         = nullptr;
            self->_waitingForPong = false;
            self->reset();
            if (self->_onDisconnected) self->_onDisconnected();
        }
        delete c;
    }, this);

    client->onError([](void *arg, AsyncClient *c, int8_t error) {
        auto *self = static_cast<WiFiTransport *>(arg);
        Serial.printf("[WiFi] Client error: %d\n", error);
        if (self->_client == c) {
            self->_client         = nullptr;
            self->_waitingForPong = false;
            self->reset();
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
    _waitingForPong = false;
    if (_client) _client->close();
}