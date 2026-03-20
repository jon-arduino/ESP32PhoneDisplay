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
