# DualTransport_TouchPaint

Finger painting that works over BLE or WiFi — whichever the iPhone connects
via. Switching transport is seamless: disconnect from one, connect via the
other. The active transport name shows in the header so you can see it switch.

## What this example demonstrates

- **DualTransport** — BLE and WiFi advertise simultaneously, auto-switch on connect
- **`activeTransportName()`** — header shows "ble" or "wifi" live
- **`setSoftAP()`** — ESP32 creates its own WiFi access point, no router needed
- **`setPowerSave(false)`** — improves touch latency on WiFi
- **Touch queue draining + batched flush** — same low-latency pattern as
  `BLE_TouchPaint2`, works identically over both transports
- **Canvas persistence** — framebuffer survives app switching and brief drops;
  only a full reconnect clears the canvas
- **`onDisplayAvailable`** — pauses drawing when display goes offline,
  resumes without clearing canvas

## Hardware required

- Any ESP32 board with WiFi (ESP32, ESP32-S3, etc.)
- No additional hardware — the iPhone is the display and touchscreen

## WiFi setup

Two options — choose one:

**Option A — connect to your existing network:**
Set `WIFI_SSID` and `WIFI_PASSWORD` to your network credentials. The iPhone
app connects via WiFi → your router → ESP32.

**Option B — connect directly to the ESP32 (no router):**
The ESP32 creates a WiFi access point named **ESP32-Display**. On your
iPhone, join that network using `SOFTAP_PASSWORD`. The app then connects
directly to the ESP32 IP.

SoftAP and router WiFi work simultaneously — the iPhone can use either.

## How to run

1. Set your WiFi credentials or choose the SoftAP option
2. Flash to your ESP32
3. Open RemoteGraphics on your iPhone
4. Connect via **Bluetooth** or **Wi-Fi** — your choice
5. Draw on the screen. Watch the header show "via: ble" or "via: wifi"
6. Disconnect and reconnect via the other transport — drawing resumes

## Transport switching

```
BLE connect  →  header shows "via: ble"  →  draw
BLE disconnect, WiFi connect  →  header shows "via: wifi"  →  draw resumes
WiFi disconnect, BLE connect  →  header shows "via: ble"   →  draw resumes
```

Each new connection clears the canvas. Brief drops within NimBLE supervision
timeout (~20s for BLE) or TCP keepalive timeout recover without clearing.

## setPowerSave(false)

By default, BLE power save mode can cause the BLE radio to be less responsive
when WiFi is active. `setPowerSave(false)` disables this — both transports
stay fully responsive simultaneously. Recommended for any dual-transport app
that requires low latency.

## Performance

Touch queue draining and batched flush work identically over BLE and WiFi:

```cpp
bool drew = false;
while (ts.available()) {
    TSPoint p = ts.getQueuedPoint();
    display.fillCircle(p.x, p.y, PENRADIUS, currentColor);
    drew = true;
}
if (drew) display.flush();
```

WiFi typically delivers even lower latency than BLE since TCP has no fixed
connection interval — packets are sent as fast as the network allows.

## See also

- `BLE_TouchPaint2` — BLE-only version with full explanation of queue
  draining and batched flush
- `BandwidthTest` — measures and compares BLE vs WiFi throughput
- [docs/transport.md](../../docs/transport.md) — DualTransport architecture,
  BLE vs WiFi performance characteristics