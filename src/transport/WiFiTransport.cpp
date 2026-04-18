#include "WiFiTransport.h"
#include <string.h>

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

// Write framed packet [lenLo][lenHi][data] into ring buffer.
// Blocks if ring is full — never drops.
void WiFiTransport::send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || !canSend()) return;

    // Hard limit — a single send() call must fit in the ring buffer.
    // Max payload is 8KB (fits a 64x64 RGB565 bitmap with room to spare).
    // Callers with larger payloads must chunk their data.
    if (len > MAX_PAYLOAD_SIZE) {
        Serial.printf("[WiFi] send() payload too large (%u bytes, max %u) — dropped\n",
                      len, (unsigned)MAX_PAYLOAD_SIZE);
        return;
    }

    const uint32_t needed = len + 2;   // 2 byte length header + data

    // Spinwait for space — blocks rather than drops
    {
        static uint32_t lastRingFullLogMs = 0;
        bool needsLog = false;
        while (ringFree() < needed) {
            if (!needsLog && (millis() - lastRingFullLogMs >= 1000)) {
                needsLog = true;
            }
            taskYIELD();
        }
        if (needsLog) {
            lastRingFullLogMs = millis();
            Serial.printf("[WiFi] Ring full — was blocked (%u bytes)\n", needed);
        }
    }

    // Write packet into ring — wrapping if needed
    uint32_t head = _head.load(std::memory_order_relaxed);
    uint32_t pos  = head;

    // Write length header
    _txBuf[pos % TX_BUF_SIZE] = len & 0xFF;         pos++;
    _txBuf[pos % TX_BUF_SIZE] = (len >> 8) & 0xFF;  pos++;

    // Write data — wraps automatically via modulo
    for (uint16_t i = 0; i < len; i++) {
        _txBuf[pos % TX_BUF_SIZE] = data[i];
        pos++;
    }

    // Commit with monotonic counter — never wraps to 0
    _head.store(pos, std::memory_order_release);
    // Do NOT notify task here — let it wake on its 5ms timeout or on flush().
    // Notifying on every send() prevents the task from ever sleeping,
    // starving loopTask and IDLE on the same core.
}

// Notify transport task to drain ring now
void WiFiTransport::flush()
{
    _flushRequested = true;
    if (_taskHandle) xTaskNotify(_taskHandle, 1, eSetBits);
}

// Discard ring buffer — called on disconnect
void WiFiTransport::reset()
{
    // Sync tail to head — buffer is now empty, monotonic relationship preserved
    uint32_t head = _head.load(std::memory_order_acquire);
    _tail.store(head, std::memory_order_release);
    _flushRequested = false;
    _lastDrainMs    = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Ring buffer helpers
// ─────────────────────────────────────────────────────────────────────────────
uint32_t WiFiTransport::ringFree() const
{
    uint32_t head = _head.load(std::memory_order_acquire);
    uint32_t tail = _tail.load(std::memory_order_acquire);
    return TX_BUF_SIZE - (head - tail) - 1;
}

uint32_t WiFiTransport::ringUsed() const
{
    uint32_t head = _head.load(std::memory_order_acquire);
    uint32_t tail = _tail.load(std::memory_order_acquire);
    return head - tail;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Transport task
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::taskFunc(void *arg)
{
    static_cast<WiFiTransport *>(arg)->runTask();
}

void WiFiTransport::runTask()
{
    for (;;) {
        // Wait for notification or 5ms timeout for auto-flush check
        xTaskNotifyWait(0, 0xFFFFFFFF, nullptr, pdMS_TO_TICKS(5));

        if (!canSend()) {
            _ping.onDisconnected();
            continue;
        }

        // Ping/pong heartbeat
        _ping.tick(millis());

        if (_ping.isTimedOut()) {
            dropClient("pong timeout");
            continue;
        }

        // Send ping between packets — never mid-packet
        if (_ping.pingNeeded())
            sendPingFrame();

        // Drain ring buffer
        drainRing();

        // Auto-flush — drain if bytes sitting idle > AUTO_FLUSH_MS
        if (ringUsed() > 0 &&
            _lastDrainMs > 0 &&
            millis() - _lastDrainMs >= AUTO_FLUSH_MS)
        {
            drainRing();
        }

        _flushRequested = false;
    }
}

// Drain ring — sends complete packets only, never splits a command.
// Breaks out if ping becomes due so runTask() can send it promptly.
void WiFiTransport::drainRing()
{
    while (canSend()) {
        // Ping has priority — break out so runTask() can send it
        if (_ping.pingNeeded()) break;

        uint32_t used = ringUsed();
        if (used < 2) break;

        uint32_t tail = _tail.load(std::memory_order_relaxed);

        uint16_t len = _txBuf[tail % TX_BUF_SIZE] |
                       (_txBuf[(tail + 1) % TX_BUF_SIZE] << 8);

        if (len == 0 || used < (uint32_t)(len + 2)) break;

        if (_client->space() < (size_t)len) break;

        uint32_t dataStart = (tail + 2) % TX_BUF_SIZE;
        uint32_t firstPart = TX_BUF_SIZE - dataStart;

        size_t written = 0;
        if (firstPart >= len) {
            written = _client->write(
                reinterpret_cast<const char *>(&_txBuf[dataStart]), len);
        } else {
            uint8_t tmp[512];
            if (len <= sizeof(tmp)) {
                memcpy(tmp, &_txBuf[dataStart], firstPart);
                memcpy(tmp + firstPart, &_txBuf[0], len - firstPart);
                written = _client->write(
                    reinterpret_cast<const char *>(tmp), len);
            }
        }

        if (written == 0) {
            taskYIELD();   // give AsyncTCP task time to process, then retry
            continue;      // tail not advanced — same packet will retry
        }

        _tail.store(tail + 2 + len, std::memory_order_release);
        _lastDrainMs = millis();
    }
}

// Send ping frame — called only from transport task between packets
void WiFiTransport::sendPingFrame()
{
    if (!canSend()) return;
    uint8_t frame[4] = {0xA5, 0x01, 0x00, GFX_CMD_PING};
    size_t written = _client->write(reinterpret_cast<const char *>(frame), 4);
    if (written == 4) _ping.onPingSent();
    // if write failed, pingNeeded() stays true — task retries next cycle
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
    xTaskCreatePinnedToCore(
        taskFunc, "wifi_transport", 4096, this, 3, &_taskHandle, 1);
}

void WiFiTransport::onClientConnected(AsyncClient *client)
{
    if (_client) {
        AsyncClient *prev = _client;
        _client = nullptr;
        prev->close();
    }

    _client = client;
    _client->setNoDelay(true);   // disable Nagle for low latency

    _bc.reset();
    reset();
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
            self->reset();
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
    _ping.onDisconnected();
    if (_client) _client->close();
}