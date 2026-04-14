#pragma once

#include <Arduino.h>
#include <functional>
#include <NimBLEDevice.h>
#include <freertos/semphr.h>
#include <freertos/stream_buffer.h>
#include "../Protocol.h"
#include "../GraphicsTransport.h"
#include "../BackChannelParser.h"

// Nordic UART Service UUIDs
static const char *SERVICE_UUID           = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *TX_CHARACTERISTIC_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *RX_CHARACTERISTIC_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

class BleTransport : public GraphicsTransport
{
public:
    BleTransport();

    void begin();
    void startAdvertising();

    bool     canSend() const;
    uint16_t effectiveChunkSize() const;

    // Send bytes — writes to stream buffer.
    // 0 = non-blocking (drop immediately if full).
    // portMAX_DELAY = block forever (stall caller until BLE drains).
    // Default: 500ms — matches roughly 2 BLE connection intervals of backlog,
    // after which the link is probably lost and dropping is better than stalling.
    void send(const uint8_t *data, uint16_t len) override;

    // Configure how long sendBytes() waits when the TX buffer is full.
    // Call before begin(). Default is 500ms.

    // No-op — kept so call sites in main.cpp compile without change.
    void update() {}

    // Callbacks
    void onKey(std::function<void(uint8_t)> cb)                     { _bc.onKey(cb);   }
    void onTouch(std::function<void(uint8_t, int16_t, int16_t, uint8_t)> cb) override { _bc.onTouch(cb); }
    void onSubscribed(std::function<void(bool)> cb) { _subscribedCallback = cb; }

    // RX
    bool   hasRxData() const;
    size_t readRx(uint8_t *dst, size_t maxLen);

private:
    friend class ServerCB;
    friend class TxCharCB;
    friend class RxCharCB;

    NimBLEServer         *pServer  = nullptr;
    NimBLECharacteristic *pTxChar  = nullptr;
    NimBLECharacteristic *pRxChar  = nullptr;

    bool     _connected        = false;
    bool     _notifySubscribed = false;
    uint16_t _mtu              = 23;

    volatile bool _cccdNotify     = false;
    volatile bool _cccdIndicate   = false;
    volatile int  _lastStatusCode = 0;

    // ── TX stream buffer ──────────────────────────────────────────────────────
    // Single producer (sendBytes), single consumer (drain task).
    // xStreamBuffer is safe for this pattern without a mutex.
    static constexpr size_t TX_STREAM_BUF_SIZE = 8192;  // absorbs burst writes
    StreamBufferHandle_t _txStream = nullptr;

    // onStatus semaphore — given by NimBLE when packet is queued to controller.
    // Drain task takes this before sending next chunk — natural flow control.
    SemaphoreHandle_t _txDone = nullptr;

    // Drain task handle — stored so we can notify it on disconnect
    TaskHandle_t _drainTaskHandle = nullptr;

    // How long sendBytes() blocks when TX stream buffer is full.

    // ── RX ────────────────────────────────────────────────────────────────────
    static constexpr size_t RX_BUF_SIZE = 256;
    uint8_t         rxBuf[RX_BUF_SIZE];
    volatile size_t rxLen = 0;

    BackChannelParser _bc;
    std::function<void(bool)> _subscribedCallback;

    // ── Drain task ────────────────────────────────────────────────────────────
    static void drainTaskFunc(void *arg);
    void        runDrainLoop();

    // ── NimBLE callbacks ──────────────────────────────────────────────────────
    class ServerCB : public NimBLEServerCallbacks {
    public:
        explicit ServerCB(BleTransport *o) : _owner(o) {}
        void onConnect(NimBLEServer*, NimBLEConnInfo&) override;
        void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override;
        void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override;
    private:
        BleTransport *_owner;
    };

    class TxCharCB : public NimBLECharacteristicCallbacks {
    public:
        explicit TxCharCB(BleTransport *o) : _owner(o) {}
        void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t) override;
        void onStatus(NimBLECharacteristic*, int) override;
    private:
        BleTransport *_owner;
    };

    class RxCharCB : public NimBLECharacteristicCallbacks {
    public:
        explicit RxCharCB(BleTransport *o) : _owner(o) {}
        void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) override;
    private:
        BleTransport *_owner;
    };
};