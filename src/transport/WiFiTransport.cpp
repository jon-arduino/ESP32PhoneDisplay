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
//  _client->add()/send() are only ever called while _writeMutex is held, which
//  serialises all TCP writes and prevents concurrent access to AsyncTCP.
//
//  The task sleeps for TASK_SLEEP_MS between iterations so IDLE and other
//  tasks get CPU time — no watchdog starvation.
//
//  ── Rules that keep the byte stream intact (learned the hard way) ───────────
//
//  1. ONE FRAME = ONE send().  _txBuf must only ever contain WHOLE frames.
//     The auto-flush task can run between two send() calls, so a frame that is
//     sent in pieces can be split across flushes and have other bytes (a ping)
//     written into the middle of it.  ESP32PhoneDisplay assembles each frame
//     (header + payload + tails) into one buffer and makes a single send().
//
//  2. ONE WRITER.  Only flushLocked() writes to the socket.  The ping is
//     appended to _txBuf like any other frame (sendPingLocked) instead of being
//     written straight to the socket, so it can never land inside a frame.
//     Symptom of getting this wrong: a bitmap strip arrives with 4 extra bytes
//     (the 4-byte ping), pixels in the rest of that strip shift by 2 pixels,
//     the phone logs "[GFXBuf] resync: dropped 4 bytes", and the ping is eaten
//     as pixel data so the pong is never sent ("[Ping] Late ...").
//
//  3. NEVER USE AsyncClient::write() TO RETRY.  write() = add() + send() and
//     returns 0 if the send() step fails even though add() already queued the
//     bytes.  A retry loop then queues the same bytes twice (duplicated data in
//     the middle of a frame).  Use add() to queue (it returns 0 only when
//     nothing was queued) and send() separately to push.
//
//  4. FRAMES MUST FIT THE BUFFER.  send() waits until the whole frame fits in
//     TX_BUF_RESERVE (4096).  A single frame larger than that would wait
//     forever, which is why the library cuts large bitmaps into ~2 KB strips.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t TASK_SLEEP_MS  = 10;   // task yield interval
static constexpr uint32_t AUTO_FLUSH_MS  = 25;   // max time bytes sit in _txBuf (~40fps)
static constexpr uint32_t WRITE_TIMEOUT  = 2000; // max ms for a single TCP write

// Spare bytes above TX_BUF_RESERVE that only the ping may use.  send() stops at
// TX_BUF_RESERVE, so there is always room for the 4-byte ping frame and it
// can never be starved by a sketch that keeps the buffer nearly full.
static constexpr size_t   PING_HEADROOM  = 16;

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────
WiFiTransport::WiFiTransport(const char *ssid, const char *password,
                             const char *mdnsHostname, uint16_t tcpPort)
    : _ssid(ssid), _password(password),
      _mdnsHostname(mdnsHostname), _tcpPort(tcpPort),
      _deviceName(mdnsHostname)
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
    _txBuf.reserve(TX_BUF_RESERVE + PING_HEADROOM);   // headroom: see PING_HEADROOM

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
//
// The caller must pass ONE WHOLE FRAME per call (see rule 1 above).  The call
// waits until the whole frame fits; it never copies a partial frame in.
void WiFiTransport::send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || !canSend()) return;

    xSemaphoreTake(_writeMutex, portMAX_DELAY);

    // Block if buffer is full — wait for background task to drain it.
    // This prevents std::bad_alloc when loop() spins faster than auto-flush.
    // The limit is TX_BUF_RESERVE, NOT the vector's capacity: the last
    // PING_HEADROOM bytes are kept free for the ping.
    while (_txBuf.size() + len > TX_BUF_RESERVE) {
        xSemaphoreGive(_writeMutex);
        vTaskDelay(pdMS_TO_TICKS(1));   // yield — let background task flush
        xSemaphoreTake(_writeMutex, portMAX_DELAY);
    }

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
//
//  The ONLY place that writes to the socket (rule 2).  Queues the buffer with
//  add() in chunks that fit the TCP send buffer, pushing after each chunk.
//
//  Why add()+send() and not write(): see rule 3.  (The previous version used
//  write() and retried on a 0 return, which duplicated bytes mid-frame.)
//
//  KNOWN ISSUE (#4, not fixed): if the TCP send buffer stays full for
//  WRITE_TIMEOUT ms we give up, print "flush timeout", and the clear() below
//  DISCARDS the unsent bytes.  That can leave a partial frame on the wire; the
//  phone resyncs on the next 0xA5 ("[GFXBuf] resync" in its console), but that
//  frame is lost.  A fuller fix would keep the unsent tail for the next flush.
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
                              (unsigned)sent, (unsigned)total);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t chunk = min(space, total - sent);

        // add() only queues the bytes; it returns 0 only if nothing was queued,
        // so retrying on 0 is safe and can never duplicate data.
        size_t queued = _client->add(
            reinterpret_cast<const char *>(_txBuf.data() + sent), chunk);
        if (queued == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));   // nothing was queued, safe to retry
            continue;
        }
        sent  += queued;
        start  = millis();
        _client->send();                    // push; if it fails the data stays queued
    }

    _client->send();                        // final push

    _txBuf.clear();
    _lastSendMs  = 0;
    _lastFlushMs = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendPingLocked() — caller MUST hold _writeMutex
//
//  Queues the ping frame in _txBuf (rule 2) instead of writing it to the
//  socket.  _txBuf only ever holds whole frames (rule 1), so the ping always
//  lands between frames.  The auto-flush in runTask() sends it, so it may wait
//  up to AUTO_FLUSH_MS in the buffer.
//
//  It uses the PING_HEADROOM bytes above TX_BUF_RESERVE, so it always fits and
//  cannot be starved by a busy sketch.  _ping.onPingSent() runs at queue time;
//  the measured round trip therefore includes this short wait, and any data
//  already queued ahead of the ping (TCP is in order).
// ─────────────────────────────────────────────────────────────────────────────
void WiFiTransport::sendPingLocked()
{
    if (!canSend()) return;
    static const uint8_t frame[4] = {0xA5, 0x01, 0x00, GFX_CMD_PING};
    _txBuf.insert(_txBuf.end(), frame, frame + sizeof(frame));
    _lastSendMs = millis();
    _ping.onPingSent();
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

        // Queue the ping if due.  It goes into _txBuf (between whole frames),
        // and the auto-flush below sends it with everything else.
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
    if (!MDNS.begin(_deviceName.c_str())) {
        Serial.println("[WiFi] ERROR: mDNS failed");
        return;
    }
    MDNS.addService("uart", "tcp", _tcpPort);
    MDNS.addServiceTxt("uart", "tcp", "board", "ESP32");
    MDNS.addServiceTxt("uart", "tcp", "version", "1.0");
    Serial.printf("[WiFi] mDNS: %s.local port %d\n", _deviceName.c_str(), _tcpPort);
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