#include "WiFiTransport.h"

WiFiManager::WiFiManager(const char *ssid, const char *password,
                         const char *mdnsHostname, uint16_t tcpPort)
    : _ssid(ssid), _password(password), _mdnsHostname(mdnsHostname), _tcpPort(tcpPort)
{
    _writeMutex = xSemaphoreCreateMutex();
    configASSERT(_writeMutex);

    _bc.onPong([this]()
               {
        _waitingForPong   = false;
        _loggedThresholds = 0;
        if (!_firstPongReceived) {
            _firstPongReceived = true;
            if (_onFirstPong) _onFirstPong();
        } });
}

void WiFiManager::begin()
{
    // ── Try STA connection ────────────────────────────────────────────────────
    WiFi.mode(_apSsid ? WIFI_AP_STA : WIFI_STA);  // AP_STA mode if SoftAP configured
    WiFi.begin(_ssid, _password);
    Serial.print("[WiFi] Connecting to STA");

    uint32_t start = millis();
    uint32_t timeout = _apSsid ? _staTimeoutMs : portMAX_DELAY;

    while (WiFi.status() != WL_CONNECTED)
    {
        if (_apSsid && (millis() - start >= timeout))
        {
            Serial.println("\n[WiFi] STA timeout — starting SoftAP");
            WiFi.disconnect(true);
            startSoftAP();
            return;
        }
        delay(500);
        Serial.print(".");
    }

    // ── STA connected ─────────────────────────────────────────────────────────
    Serial.printf("\n[WiFi] STA connected — IP: %s\n", WiFi.localIP().toString().c_str());
    _apMode = false;
    startMDNS();
    startTCPServer();
}

// ─────────────────────────────────────────────────────────────────────────────
//  update() — heartbeat task, every ~100ms
//
//  Pong watchdog + ping interval check.
//  Tries zero-timeout mutex take — if a GFX write holds it, leaves _pingNeeded
//  set. send() will fire the ping at the frame boundary before releasing.
// ─────────────────────────────────────────────────────────────────────────────
void WiFiManager::update()
{
    if (!_client || !_client->connected())
    {
        _waitingForPong = false;
        _lastPingSentMs = 0;
        _pingNeeded     = false;
        return;
    }

    uint32_t now = millis();

    // Pong watchdog logging
    if (_waitingForPong)
    {
        uint32_t el = now - _lastPingSentMs;
        if (el >= 500 && !(_loggedThresholds & 1))
        {
            _loggedThresholds |= 1;
            Serial.printf("[WiFi] Ping late by %ums — ESP32 loop may be slow\n", el);
        }
        if (el >= 1500 && !(_loggedThresholds & 2))
        {
            _loggedThresholds |= 2;
            Serial.printf("[WiFi] Ping late by %ums — WARNING: significantly delayed\n", el);
        }
        if (el >= 6000 && !(_loggedThresholds & 4))
        {
            _loggedThresholds |= 4;
            Serial.printf("[WiFi] Ping late by %ums — CRITICAL: dropping\n", el);
        }
        if (now - _lastPingSentMs >= _pongTimeoutMs)
        {
            dropClient("pong timeout");
            return;
        }
    }

    // Mark ping needed if interval elapsed
    if (now - _lastPingSentMs >= _pingIntervalMs)
        _pingNeeded = true;

    if (!_pingNeeded)
        return;

    // Zero-timeout take — never block the heartbeat task
    if (xSemaphoreTake(_writeMutex, 0) == pdTRUE)
    {
        sendPingNow();
        xSemaphoreGive(_writeMutex);
    }
    // else: send() will catch _pingNeeded at next frame boundary
}

// ─────────────────────────────────────────────────────────────────────────────
//  send() — GFX data write, any task
//
//  Entry ping check (early send):
//    If a ping is due OR is within _pingEarlyMs of being due, send it first.
//    This prevents a full telemetry frame from holding the mutex while a ping
//    sits overdue. The ping goes out at a clean boundary before the frame data,
//    which the iPhone parser handles correctly (ping is a self-contained frame).
//
//  Exit ping check:
//    If _pingNeeded is still set after the write (e.g. interval elapsed during
//    a slow TCP drain), send ping before releasing the mutex.
// ─────────────────────────────────────────────────────────────────────────────
void WiFiManager::send(const uint8_t *data, size_t len)
{
    if (!_client || !_client->connected())
        return;

    xSemaphoreTake(_writeMutex, portMAX_DELAY);

    // ── Early ping check ──────────────────────────────────────────────────────
    // Send ping before the frame if one is due or coming due within _pingEarlyMs.
    // Avoids the pattern: ping overdue → full frame transmit → ping finally sent.
    if (_pingEarlyMs > 0 && _lastPingSentMs > 0)
    {
        uint32_t elapsed = millis() - _lastPingSentMs;
        if (elapsed + _pingEarlyMs >= _pingIntervalMs)
            sendPingNow();
    }
    // Also catch any _pingNeeded already set by update()
    if (_pingNeeded)
        sendPingNow();

    // ── Write frame data ──────────────────────────────────────────────────────
    size_t sent = 0;
    const uint32_t timeoutMs = 2000;
    uint32_t start = millis();

    while (sent < len)
    {
        size_t available = _client->space();
        if (available == 0)
        {
            if (millis() - start > timeoutMs)
            {
                Serial.printf("[WiFi] send timeout — dropped %d bytes\n", (int)(len - sent));
                break;
            }
            delay(1);
            continue;
        }
        size_t chunk   = min(available, len - sent);
        size_t written = _client->write(reinterpret_cast<const char *>(data + sent), chunk);
        if (written == 0)
        {
            Serial.println("[WiFi] write() returned 0, aborting");
            break;
        }
        sent  += written;
        start  = millis();
    }

    // ── Exit ping check ───────────────────────────────────────────────────────
    // Catches the case where the interval elapsed during a slow TCP drain above.
    if (_pingNeeded)
        sendPingNow();

    xSemaphoreGive(_writeMutex);
}

void WiFiManager::send(const char *str)
{
    send(reinterpret_cast<const uint8_t *>(str), strlen(str));
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendPingNow() — caller MUST hold _writeMutex
// ─────────────────────────────────────────────────────────────────────────────
void WiFiManager::sendPingNow()
{
    if (!_client || !_client->connected())
    {
        _pingNeeded = false;
        return;
    }
    uint8_t frame[4] = {BC_MAGIC,
                        0x01, 0x00,   // length = 1 (cmd byte only)
                        GFX_CMD_PING};
    _client->write(reinterpret_cast<const char *>(frame), 4);
    _lastPingSentMs   = millis();
    _waitingForPong   = true;
    _loggedThresholds = 0;
    _pingNeeded       = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendCmd() — framed back-channel command, takes mutex
// ─────────────────────────────────────────────────────────────────────────────
void WiFiManager::sendCmd(uint8_t cmd, const uint8_t *payload, size_t payloadLen)
{
    uint16_t len = 1 + (uint16_t)payloadLen;
    uint8_t hdr[4] = {BC_MAGIC, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8), cmd};
    send(hdr, 4);
    if (payloadLen > 0 && payload)
        send(payload, payloadLen);
}

bool WiFiManager::isConnected() const
{
    // In AP mode the ESP32 is always "connected" — it's hosting the network.
    return _apMode || (WiFi.status() == WL_CONNECTED);
}

// ─────────────────────────────────────────────────────────────────────────────
//  switchToSoftAP() -- tear down STA/client and bring up SoftAP on demand
//
//  Called from loop() via console 'w' command. Drops any connected iPhone
//  client, disconnects from router, and starts the ESP32-hosted network.
//  iPhone joins "ESP32-RemoteUI" manually then reconnects via WiFi scan.
// ─────────────────────────────────────────────────────────────────────────────
void WiFiManager::switchToSoftAP()
{
    if (!_apSsid) {
        Serial.println("[WiFi] switchToSoftAP: no AP credentials set -- call setSoftAP() first");
        return;
    }
    if (_apMode) {
        Serial.printf("[WiFi] Already in AP mode -- SSID: %s  password: %s\n",
                      _apSsid, _apPassword);
        return;
    }

    Serial.println("[WiFi] Switching to SoftAP mode...");

    // Reset all internal state so the new connection starts clean
    _waitingForPong    = false;
    _pingNeeded        = false;
    _firstPongReceived = false;
    _lastPingSentMs    = 0;
    _loggedThresholds  = 0;
    _bc.reset();

    // Fire onDisconnected so main.cpp resets phoneReady, transport buffer, etc.
    // Do this BEFORE closing the client so the callback sees a clean state.
    if (_onDisconnected) _onDisconnected();

    // Drop any connected iPhone client cleanly
    if (_client) {
        _client->close();
        _client = nullptr;
    }

    // Stop TCP server and mDNS before changing network mode
    if (_server) {
        _server->end();
        delete _server;
        _server = nullptr;
    }
    MDNS.end();

    // Disconnect from router and start AP
    WiFi.disconnect(true);
    startSoftAP();
}
void WiFiManager::startSoftAP()
{
    _apMode = true;
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(_apSsid, _apPassword);
    if (!ok)
    {
        Serial.println("[WiFi] ERROR: softAP() failed");
        return;
    }

    IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));

    Serial.printf("[WiFi] SoftAP up — SSID: %s  IP: %s\n",
                  _apSsid, WiFi.softAPIP().toString().c_str());

    // mDNS and TCP server work identically in AP mode
    startMDNS();
    startTCPServer();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private
// ─────────────────────────────────────────────────────────────────────────────

void WiFiManager::startMDNS()
{
    if (!MDNS.begin(_mdnsHostname))
    {
        Serial.println("ERROR: mDNS failed");
        return;
    }
    MDNS.addService("uart", "tcp", _tcpPort);
    MDNS.addServiceTxt("uart", "tcp", "board", "ESP32");
    MDNS.addServiceTxt("uart", "tcp", "version", "1.0");
    Serial.printf("mDNS advertising _uart._tcp as %s.local on port %d\n", _mdnsHostname, _tcpPort);
}

void WiFiManager::startTCPServer()
{
    _server = new AsyncServer(_tcpPort);
    _server->onClient([](void *arg, AsyncClient *client)
                      { static_cast<WiFiManager *>(arg)->onClientConnected(client); }, this);
    _server->begin();
    Serial.printf("TCP server listening on port %d\n", _tcpPort);
}

void WiFiManager::onClientConnected(AsyncClient *client)
{
    bool wasConnected = (_client != nullptr);

    if (_client)
    {
        AsyncClient *prev = _client;
        _client = nullptr;
        prev->close();
    }

    if (wasConnected && _onConnected)
        _onConnected();

    _client            = client;
    _bc.reset();
    _waitingForPong    = false;
    _pingNeeded        = false;
    _lastPingSentMs    = millis();
    _firstPongReceived = false;

    Serial.printf("iPhone connected from %s\n", client->remoteIP().toString().c_str());

    client->onData([](void *arg, AsyncClient *c, void *data, size_t len)
                   { static_cast<WiFiManager *>(arg)->_bc.feed(static_cast<uint8_t *>(data), len); }, this);

    client->onDisconnect([](void *arg, AsyncClient *c)
                         {
        auto *self = static_cast<WiFiManager *>(arg);
        Serial.println("iPhone disconnected");
        if (self->_client == c) {
            self->_client         = nullptr;
            self->_waitingForPong = false;
            if (self->_onDisconnected) self->_onDisconnected();
        }
        delete c; }, this);

    client->onError([](void *arg, AsyncClient *c, int8_t error)
                    {
        auto *self = static_cast<WiFiManager *>(arg);
        Serial.printf("Client error: %d\n", error);
        if (self->_client == c) {
            self->_client         = nullptr;
            self->_waitingForPong = false;
            if (self->_onDisconnected) self->_onDisconnected();
        } }, this);

    client->onTimeout([](void *arg, AsyncClient *c, uint32_t time)
                      {
        Serial.printf("Client TCP timeout at %u ms\n", time);
        c->close(); }, this);
}

void WiFiManager::dropClient(const char *reason)
{
    Serial.printf("[WiFi] Dropping client: %s\n", reason);
    _waitingForPong = false;
    if (_client)
        _client->close();
}
