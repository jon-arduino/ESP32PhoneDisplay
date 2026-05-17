# WiFi_HelloWorld

The WiFi equivalent of `BLE_HelloWorld`. Draws a screen on the iPhone
over WiFi, handles connect/reconnect cleanly.

Connection handling, callbacks, session management, and drawing code are
identical to BLE_HelloWorld — only the transport changes. Reading both
examples side by side shows exactly what differs between BLE and WiFi.

## What this example demonstrates

- **WiFiTransport** — same API as BleTransport, different transport layer
- **`setDeviceName()`** — custom mDNS hostname ("Hello_ESP32.local")
- **`setSoftAP()`** — direct iPhone→ESP32 connection without a router
- **Same callback pattern as BLE** — `onDisplayAvailable`, `onConnected`,
  `onDisconnected`, `onRedrawRequest`
- **`_displayReset` flag** — `begin()` only re-sent on new TCP connection

## Hardware required

- Any ESP32 board with WiFi
- No additional hardware — the iPhone is the display

## WiFi setup

**Option A — existing network:** set `WIFI_SSID` and `WIFI_PASSWORD`.
The device appears in the iPhone app as `Hello_ESP32` (mDNS).

**Option B — direct connection (no router):** leave credentials as
placeholders. The ESP32 creates a WiFi access point named `Hello_ESP32-AP`.
Join that network on your iPhone using `SOFTAP_PASSWORD`, then connect in
the app.

## Device naming

This example uses `setDeviceName("Hello_ESP32")` and
`setSoftAP("Hello_ESP32-AP", ...)` to give the device recognisable names.
The default ("esp32-display") is fine for single-device use but becomes
ambiguous with multiple devices. See [docs/transport.md](../../docs/transport.md)
for the full device naming guide including the MAC address approach for
unique auto-naming.

## BLE vs WiFi

The connection handling code is identical. The practical differences:

| | BLE | WiFi |
|---|---|---|
| Infrastructure | None needed | Router or SoftAP |
| Discovery | By name in app | mDNS (name.local) |
| Range | ~10–30m | Network range |
| Power | Lower | Higher |
| Performance | Comparable (with setConnectionInterval) | Comparable |

See [docs/transport.md](../../docs/transport.md) for the full trade-off discussion.

## See also

- `BLE_HelloWorld` — identical example over BLE
- `DualTransport_TouchPaint` — both transports simultaneously
- [docs/transport.md](../../docs/transport.md) — BLE vs WiFi trade-offs,
  device naming, transport architecture