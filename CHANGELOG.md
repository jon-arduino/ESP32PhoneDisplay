# Changelog

All notable changes to ESP32PhoneDisplay will be documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0] - 2026-03-15
### Added
- Initial release
- `ESP32PhoneDisplay` core graphics class — full Adafruit_GFX-compatible API
- `ESP32PhoneDisplay_Compat` — drop-in Adafruit_GFX subclass for zero-friction porting
- `GraphicsTransport` abstract base — implement your own transport (LoRa, serial, etc.)
- `BleTransport` — BLE transport using NimBLE-Arduino with stream buffer + drain task
- `WiFiTransport` — WiFi transport using AsyncTCP with ping/pong heartbeat
- `RemoteTouchScreen` — back-channel touch input compatible with Adafruit_TouchScreen API
- Binary command protocol — single compact command per GFX operation, no pixel decomposition
- Examples: BLE_HelloWorld, WiFi_HelloWorld, BLE_Telemetry, CompatLayer, CustomTransport


# Changelog

## 1.0.0 — 2026-04-30

Initial public release.

### Core
- `ESP32PhoneDisplay` — Adafruit GFX–compatible drawing API over any transport
- `ESP32PhoneDisplay_Compat` — drop-in replacement for Adafruit_TFTLCD
- `GraphicsTransport` — abstract base for custom transport implementations

### Transports
- `BleTransport` — NimBLE-based BLE transport with 8KB stream buffer and drain task
- `WiFiTransport` — AsyncTCP-based WiFi transport with ping/pong heartbeat and RTT stats
- `DualTransport` — simultaneous BLE + WiFi with automatic switching

### Touch
- `RemoteTouchScreen` — Adafruit_TouchScreen–compatible touch via iPhone back-channel
- 16-entry FIFO touch queue with overflow diagnostics
- Standard API (`getPoint`) and path API (`getQueuedPoint`) for drawing apps
- Configurable MOVE event throttle rate

### BLE
- Connection interval API — `setConnectionInterval()`, `updateConnectionInterval()`
- `connIntervalMs()` — query actual negotiated interval
- `onConnInterval()` — callback on interval change
- 1.5s deferred interval request (iOS settling time)
- `BackChannelParser::Stats` — cumulative diagnostic counters

### Examples
- `BLE_HelloWorld`, `BLE_TouchButtons`, `BLE_TouchPaint`, `BLE_Telemetry`
- `WiFi_HelloWorld`, `DualTransport_TouchPaint`, `BandwidthTest`
- `Breakout` — classic Breakout ported from Adafruit_TFTLCD (Enrique Albertos, public domain)
- `Breakout_II` — fixed 30fps Breakout with speed multiplier and brick flash state machine
- `CompatLayer`, `CustomTransport`, `SerialTest`