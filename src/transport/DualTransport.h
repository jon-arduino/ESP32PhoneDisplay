#pragma once

// -----------------------------------------------------------------------------
//  DualTransport — GraphicsTransport adapter for simultaneous BLE + WiFi
//
//  Wraps BleTransport and WiFiTransport. Whichever connects first (or most
//  recently) becomes the active transport. If the active transport disconnects
//  and the other is still connected, it switches automatically.
//
//  The iPhone controls which transport is used — it simply connects via BLE
//  or WiFi and that channel becomes active. Disconnecting and connecting via
//  the other transport switches seamlessly.
//
//  Usage:
//    DualTransport transport(WIFI_SSID, WIFI_PASS, "esp32-display");
//    ESP32PhoneDisplay display(transport);
//    RemoteTouchScreen ts(transport);
//
//    void setup() {
//        transport.onConnected([]() {
//            display.begin(240, 320);
//            display.clear(0x0000);
//            display.flush();
//        });
//        transport.begin();
//    }
//
//    void loop() {
//        // draw here using display
//    }
//
//  Note: WiFi credentials are required. BLE always advertises regardless.
//  If you don't have WiFi credentials, use BleTransport directly.
// -----------------------------------------------------------------------------

#include "GraphicsTransport.h"
#include "BleTransport.h"
#include "WiFiTransport.h"
#include <functional>

class DualTransport : public GraphicsTransport
{
public:
    DualTransport(const char *wifiSsid,
                  const char *wifiPassword,
                  const char *mdnsHostname = "esp32-display",
                  uint16_t    tcpPort      = 9000)
        : _wifi(wifiSsid, wifiPassword, mdnsHostname, tcpPort)
    {}

    // Optional: SoftAP fallback for WiFi
    void setSoftAP(const char *apSsid,
                   const char *apPassword,
                   uint32_t    staTimeoutMs = 15000)
    {
        _wifi.setSoftAP(apSsid, apPassword, staTimeoutMs);
    }

    // Start both transports. Both advertise simultaneously.
    void begin()
    {
        // ── BLE callbacks ─────────────────────────────────────────────────────
        _ble.onSubscribed([this](bool ready) {
            if (ready) {
                Serial.println("[Dual] BLE connected — activating");
                _active = &_ble;
                if (_onConnected) _onConnected();
            } else {
                Serial.println("[Dual] BLE disconnected");
                if (_active == &_ble) {
                    // Switch to WiFi if it's connected
                    if (_wifi.canSend()) {
                        Serial.println("[Dual] Switching to WiFi");
                        _active = &_wifi;
                        if (_onConnected) _onConnected();
                    } else {
                        _active = nullptr;
                        if (_onDisconnected) _onDisconnected();
                    }
                }
            }
        });

        _ble.onKey([this](uint8_t key) {
            if (_keyCallback) _keyCallback(key);
        });

        // ── WiFi callbacks ────────────────────────────────────────────────────
        _wifi.onConnected([this]() {
            Serial.println("[Dual] WiFi connected — activating");
            _active = &_wifi;
            if (_onConnected) _onConnected();
        });

        _wifi.onDisconnected([this]() {
            Serial.println("[Dual] WiFi disconnected");
            if (_active == &_wifi) {
                // Switch to BLE if it's connected
                if (_ble.canSend()) {
                    Serial.println("[Dual] Switching to BLE");
                    _active = &_ble;
                    if (_onConnected) _onConnected();
                } else {
                    _active = nullptr;
                    if (_onDisconnected) _onDisconnected();
                }
            }
        });

        _wifi.onKey([this](uint8_t key) {
            if (_keyCallback) _keyCallback(key);
        });

        // Start both — BLE advertises, WiFi connects to network
        _ble.begin();
        _wifi.begin();
    }

    // ── GraphicsTransport interface ───────────────────────────────────────────

    bool canSend() const override
    {
        return _active != nullptr && _active->canSend();
    }

    void send(const uint8_t *data, uint16_t len) override
    {
        if (_active) _active->send(data, len);
    }

    void flush() override
    {
        if (_active) _active->flush();
    }

    void reset() override
    {
        if (_active) _active->reset();
    }

    void onTouch(std::function<void(uint8_t, int16_t, int16_t, uint8_t)> cb) override
    {
        // Register on both — only active one will fire
        _ble.onTouch(cb);
        _wifi.onTouch(cb);
    }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void onConnected   (std::function<void()> cb) { _onConnected    = cb; }
    void onDisconnected(std::function<void()> cb) { _onDisconnected = cb; }
    void onKey         (std::function<void(uint8_t)> cb)
    {
        _keyCallback = cb;
    }

    // ── Status ────────────────────────────────────────────────────────────────
    bool isBleActive()  const { return _active == &_ble;  }
    bool isWifiActive() const { return _active == &_wifi; }

    const char* activeTransportName() const
    {
        if (_active == &_ble)  return "BLE";
        if (_active == &_wifi) return "WiFi";
        return "none";
    }

private:
    BleTransport      _ble;
    WiFiTransport     _wifi;
    GraphicsTransport *_active = nullptr;

    std::function<void()>        _onConnected;
    std::function<void()>        _onDisconnected;
    std::function<void(uint8_t)> _keyCallback;
};