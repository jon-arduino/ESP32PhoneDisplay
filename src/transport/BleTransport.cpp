#include "BleTransport.h"
#include <NimBLE2904.h>
// Forward declare os_msys_num_free() -- part of NimBLE host stack.
// Avoids include path issues with NimBLE-Arduino vs ESP-IDF layouts.
extern "C" int os_msys_num_free(void);

// ─────────────────────────────────────────────────────────────────────────────
//  NimBLE server callbacks
// ─────────────────────────────────────────────────────────────────────────────

void BleTransport::ServerCB::onConnect(NimBLEServer*, NimBLEConnInfo& connInfo)
{
    _owner->_connected        = true;
    _owner->_notifySubscribed = false;
    _owner->_bc.reset();
    Serial.println("[BLE] connected");
    Serial.print("[BLE] peer: ");
    Serial.println(connInfo.getAddress().toString().c_str());
}

void BleTransport::ServerCB::onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason)
{
    _owner->_connected        = false;
    _owner->_notifySubscribed = false;
    _owner->_cccdNotify       = false;
    _owner->_cccdIndicate     = false;
    _owner->_bc.reset();

    // Flush stream buffer so drain task doesn't send stale data on reconnect
    xStreamBufferReset(_owner->_txStream);

    // Unblock drain task if it is waiting on _txDone semaphore
    xSemaphoreGive(_owner->_txDone);

    Serial.printf("[BLE] disconnected reason=%d\n", reason);
    _owner->startAdvertising();
}

void BleTransport::ServerCB::onMTUChange(uint16_t mtu, NimBLEConnInfo&)
{
    _owner->_mtu = mtu;
    Serial.printf("[BLE] MTU=%d\n", mtu);
}

// ─────────────────────────────────────────────────────────────────────────────
//  TX characteristic callbacks
// ─────────────────────────────────────────────────────────────────────────────

void BleTransport::TxCharCB::onSubscribe(NimBLECharacteristic*,
                                        NimBLEConnInfo&,
                                        uint16_t subValue)
{
    _owner->_cccdNotify       = (subValue & 0x0001) != 0;
    _owner->_cccdIndicate     = (subValue & 0x0002) != 0;
    _owner->_notifySubscribed = _owner->_cccdNotify || _owner->_cccdIndicate;

    Serial.printf("[BLE] subscribed notify=%s indicate=%s\n",
                  _owner->_cccdNotify   ? "YES" : "NO",
                  _owner->_cccdIndicate ? "YES" : "NO");

    if (_owner->_notifySubscribed) {
        // Prime the semaphore so drain task can send the first chunk
        xSemaphoreGive(_owner->_txDone);
    }

    if (_owner->_subscribedCallback)
        _owner->_subscribedCallback(_owner->_notifySubscribed);
}

// Called by NimBLE stack when notification is queued to the controller.
// Gives the semaphore so the drain task can send the next chunk.
void BleTransport::TxCharCB::onStatus(NimBLECharacteristic*, int code)
{
    _owner->_lastStatusCode = code;
    xSemaphoreGive(_owner->_txDone);
}

// ─────────────────────────────────────────────────────────────────────────────
//  RX characteristic callback
// ─────────────────────────────────────────────────────────────────────────────

void BleTransport::RxCharCB::onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo&)
{
    std::string v = pChar->getValue();
    size_t n = v.size();
    _owner->_bc.feed(reinterpret_cast<const uint8_t*>(v.data()), n);
    if (n > RX_BUF_SIZE) n = RX_BUF_SIZE;
    memcpy(_owner->rxBuf, v.data(), n);
    _owner->rxLen = n;
}

// ─────────────────────────────────────────────────────────────────────────────
//  BleTransport
// ─────────────────────────────────────────────────────────────────────────────

BleTransport::BleTransport()
{
    // No heap allocations here — constructor runs before FreeRTOS scheduler.
    // Stream buffer and semaphore are created in begin().
}

void BleTransport::begin()
{
    // Create stream buffer and semaphore now that heap is available
    if (_txStream == nullptr) {
        _txStream = xStreamBufferCreate(TX_STREAM_BUF_SIZE, 1);
        configASSERT(_txStream);
    }
    if (_txDone == nullptr) {
        _txDone = xSemaphoreCreateBinary();
        configASSERT(_txDone);
    }

    NimBLEDevice::init("ESP32-Display");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    Serial.printf("[BLE] MAC: %s\n", NimBLEDevice::getAddress().toString().c_str());

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCB(this));

    NimBLEService *pService = pServer->createService(SERVICE_UUID);

    pTxChar = pService->createCharacteristic(
        TX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
    );
    pTxChar->setCallbacks(new TxCharCB(this));

    pRxChar = pService->createCharacteristic(
        RX_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxChar->setCallbacks(new RxCharCB(this));

    pService->start();

    // Start drain task on core 0 (BLE stack runs on core 0 anyway).
    // Stack size 4KB is sufficient -- drain loop is simple.
    xTaskCreatePinnedToCore(
        drainTaskFunc,      // task function
        "ble_drain",        // name (for debugging)
        4096,               // stack size in bytes
        this,               // parameter
        5,                  // priority (same as NimBLE host task)
        &_drainTaskHandle,  // handle
        0                   // core 0
    );

    startAdvertising();
}

void BleTransport::startAdvertising()
{
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);

    NimBLEAdvertisementData scanResp;
    scanResp.setName("ESP32-Display");
    adv->setScanResponseData(scanResp);
    adv->setMinInterval(48);
    adv->setMaxInterval(160);
    adv->start();
    Serial.println("[BLE] advertising started");
}

bool BleTransport::canSend() const
{
    return _connected && _notifySubscribed && (pTxChar != nullptr);
}

uint16_t BleTransport::effectiveChunkSize() const
{
    uint16_t maxPayload = (_mtu > 23) ? (_mtu - 3) : 20;
    return maxPayload;
}

// ─────────────────────────────────────────────────────────────────────────────
//  sendBytes -- write to stream buffer, return immediately
//
//  The caller (GFX task / loop()) is never blocked by BLE timing.
//  If the stream buffer is full (8KB backlog) xStreamBufferSend will block
//  briefly -- this is the correct backpressure point and is rarely hit.
// ─────────────────────────────────────────────────────────────────────────────
void BleTransport::send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return;
    if (!canSend()) return;

    // Write to stream buffer. If full, block up to _sendTimeoutMs per chunk.
    // Loop handles the case where len > available space -- we keep writing
    // until all bytes are queued or we lose the connection.
    //
    // On timeout: log and drop remaining bytes. This matches WiFi behaviour
    // and prevents loop() from stalling indefinitely if BLE goes slow/dead.
    // The connection watchdog will detect the dead link and call onDisconnect,
    // which triggers initPhoneUI() on reconnect for a full redraw.
    // Write to stream buffer -- block until space is available.
    // Uses portMAX_DELAY matching the old semaphore-gated send() behavior:
    // the GFX task blocks if BLE can't keep up, and NimBLE's supervision
    // timeout is the ultimate dead-link detector that fires onDisconnect.
    // On disconnect the stream buffer is flushed and initPhoneUI() redraws.
    size_t sent = 0;
    while (sent < len) {
        if (!canSend()) return;  // disconnected -- bail cleanly
        size_t n = xStreamBufferSend(_txStream,
                                     data + sent,
                                     len  - sent,
                                     portMAX_DELAY);
        sent += n;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drain task -- runs forever on core 0
//
//  Flow:
//    1. Wait for _txDone semaphore (given by onSubscribe or previous onStatus)
//    2. Read up to one MTU chunk from stream buffer (5ms timeout)
//    3. If data: notify(), which triggers onStatus() → gives _txDone
//    4. If no data after timeout: give _txDone back and wait again
//
//  The 5ms receive timeout is the "idle flush" mechanism -- any bytes sitting
//  in the buffer are drained within 5ms even if less than one MTU worth.
// ─────────────────────────────────────────────────────────────────────────────
void BleTransport::drainTaskFunc(void *arg)
{
    static_cast<BleTransport*>(arg)->runDrainLoop();
}

void BleTransport::runDrainLoop()
{
    // Local retry buffer -- holds the current chunk until successfully sent.
    // Keeping it here (not re-injecting into the stream buffer) preserves
    // ordering: the same chunk is retried before any newer data is consumed.
    uint8_t chunk[252];   // sized for max MTU payload (MTU 255 - 3 ATT header)
    size_t  pendingN = 0; // >0: chunk[] holds data waiting to be sent/retried

    while (true) {

        // ── Wait for stack ready ──────────────────────────────────────────────
        // _txDone is given by:
        //   onSubscribe -- initial prime when iPhone subscribes
        //   onStatus    -- after each notify() is queued to controller
        if (xSemaphoreTake(_txDone, portMAX_DELAY) != pdTRUE) continue;

        if (!canSend()) {
            // Disconnected -- discard any pending retry, wait for reconnect
            pendingN = 0;
            xSemaphoreGive(_txDone);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ── Get next chunk (only if no retry pending) ─────────────────────────
        if (pendingN == 0) {
            uint16_t chunkSize = effectiveChunkSize();

            // 5ms timeout -- idle flush: partial buffers are always delivered
            // within 5ms even if less than one MTU of data is waiting.
            pendingN = xStreamBufferReceive(_txStream, chunk, chunkSize,
                                            pdMS_TO_TICKS(5));
            if (pendingN == 0) {
                // Nothing in buffer -- return semaphore and wait
                xSemaphoreGive(_txDone);
                continue;
            }
        }

        // ── Send chunk ────────────────────────────────────────────────────────
        // chunk[] holds either fresh data or a retry of the previous chunk.
        if (!canSend()) {
            pendingN = 0;
            xSemaphoreGive(_txDone);
            continue;
        }

        // Gate on mbuf availability before notify() -- canonical Espressif
        // pattern from bleprph_throughput. Proactively avoids BLE_HS_ENOMEM
        // (error=6) rather than reacting to it. Each notify needs ~2 mbufs
        // (ATT PDU + L2CAP header). We require 4 for headroom.
        // 1ms yield per check keeps CPU use negligible.
        while (os_msys_num_free() < 4 && canSend()) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!canSend()) {
            pendingN = 0;
            xSemaphoreGive(_txDone);
            continue;
        }

        _lastStatusCode = 0;
        pTxChar->setValue(chunk, pendingN);
        bool ok = (_cccdIndicate) ? pTxChar->indicate() : pTxChar->notify();

        if (!ok) {
            // Synchronous rejection -- pendingN stays set, retry next iteration
            xSemaphoreGive(_txDone);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // ── Wait for onStatus ─────────────────────────────────────────────────
        // onStatus fires when NimBLE has queued the packet to the controller.
        if (xSemaphoreTake(_txDone, pdMS_TO_TICKS(2000)) != pdTRUE) {
            Serial.println("[BLE] onStatus timeout -- retrying");
            xSemaphoreGive(_txDone);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (_lastStatusCode != 0) {
            // error=6 (BLE_HS_ENOMEM): controller ACL TX buffer full.
            // pendingN stays set -- same chunk retries next iteration.
            // 50ms delay lets the controller drain queued packets.
            // Ordering guaranteed: no newer data consumed until this succeeds.
            Serial.printf("[BLE] onStatus error=%d -- retrying chunk\n", _lastStatusCode);
            xSemaphoreGive(_txDone);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ── Success ───────────────────────────────────────────────────────────
        pendingN = 0;
        xSemaphoreGive(_txDone);
    }
}

bool BleTransport::hasRxData() const { return rxLen > 0; }

size_t BleTransport::readRx(uint8_t *dst, size_t maxLen)
{
    size_t n = (rxLen < maxLen) ? rxLen : maxLen;
    memcpy(dst, rxBuf, n);
    rxLen = 0;
    return n;
}