#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Protocol.h — back-channel (iPhone -> ESP32) framing and command constants
//
//  The GFX command opcodes and payload structs live in GraphicsProtocol.h.
//  This file covers only the back-channel: touch, keys, and pong.
//
//  Frame format (all back-channel frames):
//    [BC_MAGIC]  1 byte   0xA5
//    [lenLo]     1 byte   little-endian uint16: bytes after 3-byte header
//    [lenHi]     1 byte
//    [cmd]       1 byte   BC_CMD_* constant
//    [payload]   0+ bytes command-specific
// -----------------------------------------------------------------------------

static constexpr uint8_t BC_MAGIC = 0xA5;

// ── Transport ─────────────────────────────────────────────────────────────────
static constexpr uint8_t BC_CMD_PING = 0xF0;  // reserved
static constexpr uint8_t BC_CMD_PONG = 0xF1;  // reply to GFX_CMD_PING, no payload

// ── Key commands (GFX test buttons) ──────────────────────────────────────────
static constexpr uint8_t BC_CMD_KEY1 = 0xF2;  // no payload
static constexpr uint8_t BC_CMD_KEY2 = 0xF3;  // no payload

// ── Touch events ─────────────────────────────────────────────────────────────
//  Coordinates are mapped to virtual display space (0,0)..(w-1,h-1).
//  Compatible with Adafruit_TouchScreen TSPoint API — see RemoteTouchScreen.h.
//
//  DOWN/MOVE payload: x(2LE) y(2LE) z(1)   — z=0xFF on contact
//  UP    payload:     none
static constexpr uint8_t BC_CMD_TOUCH_DOWN = 0x10;
static constexpr uint8_t BC_CMD_TOUCH_MOVE = 0x11;
static constexpr uint8_t BC_CMD_TOUCH_UP   = 0x12;

static constexpr uint8_t BC_TOUCH_PRESSURE    = 0xFF;
static constexpr uint8_t BC_TOUCH_PAYLOAD_LEN = 5;    // x(2)+y(2)+z(1)
static constexpr uint8_t BC_NO_PAYLOAD_LEN    = 0;
