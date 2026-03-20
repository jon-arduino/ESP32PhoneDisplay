# Contributing to ESP32PhoneDisplay

Thank you for your interest in contributing. This document covers how to report bugs, request features, and submit code.

## Reporting bugs

Use the [Bug Report](.github/ISSUE_TEMPLATE/bug_report.md) template. Please include:
- ESP32 board and Arduino core version
- NimBLE-Arduino / AsyncTCP versions
- Minimal sketch that reproduces the issue
- Serial console output
- iPhone app version

## Requesting features

Use the [Feature Request](.github/ISSUE_TEMPLATE/feature_request.md) template. Describe the use case — what are you trying to build?

## Submitting code

1. Fork the repo and create a branch from `main`
2. Keep changes focused — one feature or fix per PR
3. Test on real hardware (ESP32 + iPhone app)
4. Run arduino-lint locally if possible: `arduino-lint --library-manager submit`
5. Update `CHANGELOG.md` under `[Unreleased]`
6. Submit the PR with a clear description of what changed and why

## Code style

- C++11, Arduino-compatible
- No dynamic allocation in hot paths (send/receive loops)
- Keep `GraphicsTransport` and `Protocol.h` free of BLE/WiFi dependencies
- New transport implementations go in `src/transport/`

## Protocol changes

Changes to `Protocol.h` require coordinated updates to the iPhone app. Open an issue first to discuss before implementing — protocol changes affect all users.
