#pragma once
#include <stdint.h>

// -----------------------------------------------------------------------------
//  GraphicsProtocol.h — GFX command opcodes and packed payload structs
//
//  This file is shared between the ESP32 library and the iPhone app renderer.
//  It defines the binary layout of every GFX command on the wire.
//
//  PACKING IS CRITICAL — structs are transmitted byte-for-byte over BLE/WiFi.
//  Do not add fields, change types, or reorder without updating both sides.
// -----------------------------------------------------------------------------

#define GFX_PROTOCOL_VERSION 2

// ── Command opcodes ───────────────────────────────────────────────────────────
enum GfxCommand : uint8_t
{
    // System
    GFX_CMD_BEGIN           = 0x01,
    GFX_CMD_FILL_SCREEN     = 0x02,
    GFX_CMD_FLUSH           = 0x03,
    GFX_CMD_SET_ROTATION    = 0x05,
    GFX_CMD_INVERT_DISPLAY  = 0x06,

    // ── Display session ───────────────────────────────────────────────────────
    GFX_CMD_CLOSE_DISPLAY   = 0x07,  // no payload — end session, clear screen
    GFX_CMD_SET_TITLE       = 0x08,  // payload: UTF-8 string (no null terminator)
    GFX_CMD_CLEAR_BUTTONS   = 0x09,  // no payload — remove all toolbar buttons
    GFX_CMD_ADD_BUTTON      = 0x0A,  // payload: keyCode(u8) labelLen(u8) label(utf8)
                                      //   keyCode: BC_CMD_KEY1(0xF2) or BC_CMD_KEY2(0xF3)
                                      //   KEY1 always renders left of KEY2
    // Reserved: GFX_CMD_FLUSH_SYNC = 0x04  — future sprint


    // Pixels & Lines
    GFX_CMD_DRAW_PIXEL      = 0x10,
    GFX_CMD_DRAW_LINE       = 0x11,
    GFX_CMD_DRAW_FAST_HLINE = 0x12,
    GFX_CMD_DRAW_FAST_VLINE = 0x13,

    // Rectangles
    GFX_CMD_DRAW_RECT       = 0x20,
    GFX_CMD_FILL_RECT       = 0x21,
    GFX_CMD_DRAW_ROUNDRECT  = 0x22,
    GFX_CMD_FILL_ROUNDRECT  = 0x23,

    // Circles & Triangles
    GFX_CMD_DRAW_CIRCLE     = 0x30,
    GFX_CMD_FILL_CIRCLE     = 0x31,
    GFX_CMD_DRAW_TRIANGLE   = 0x32,
    GFX_CMD_FILL_TRIANGLE   = 0x33,

    // Bitmaps
    GFX_CMD_DRAW_BITMAP     = 0x40,
    GFX_CMD_DRAW_BITMAP_BG  = 0x41,

    // Text
    GFX_CMD_SET_CURSOR        = 0x50,
    GFX_CMD_SET_TEXT_COLOR    = 0x51,
    GFX_CMD_SET_TEXT_COLOR_BG = 0x52,
    GFX_CMD_SET_TEXT_SIZE     = 0x53,
    GFX_CMD_SET_TEXT_WRAP     = 0x54,
    GFX_CMD_CP437             = 0x55,
    GFX_CMD_SET_FONT          = 0x56,
    GFX_CMD_WRITE_CHAR        = 0x57,

    // Optional / future
    GFX_CMD_GET_TEXT_BOUNDS   = 0x60,

    // Touch control (ESP32 → iPhone)
    // Tell the iPhone to start/stop/configure sending touch events.
    // See RemoteTouchScreen.h for the full touch API.
    GFX_CMD_TOUCH_BEGIN       = 0x70,  // start touch reporting
    GFX_CMD_TOUCH_END         = 0x71,  // stop touch reporting
    GFX_CMD_TOUCH_DELAY       = 0x72,  // update move throttle interval

    // Transport (0xF0-0xFF — see Protocol.h)
    GFX_CMD_PING              = 0xF0,
};

// ── Struct packing ────────────────────────────────────────────────────────────
#if defined(__GNUC__)
#  define GFX_PACKED __attribute__((packed))
#else
#  pragma pack(push, 1)
#  define GFX_PACKED
#endif

// ── Packet header (precedes every command on the wire) ────────────────────────
struct GFX_PACKED GfxPacketHeader
{
    uint8_t  magic;   // 0xA5
    uint16_t length;  // little-endian: bytes after this 3-byte header
};

// ── Payload structs ───────────────────────────────────────────────────────────

struct GFX_PACKED GfxBeginPayload        { uint16_t width; uint16_t height; };
struct GFX_PACKED GfxClearPayload        { uint16_t color; };
struct GFX_PACKED GfxSetRotationPayload  { uint8_t  rotation; };
struct GFX_PACKED GfxInvertDisplayPayload{ uint8_t  invert; };

struct GFX_PACKED GfxDrawPixelPayload    { int16_t x; int16_t y; uint16_t color; };
struct GFX_PACKED GfxDrawLinePayload     { int16_t x0; int16_t y0; int16_t x1; int16_t y1; uint16_t color; };
struct GFX_PACKED GfxFastLinePayload     { int16_t x; int16_t y; int16_t length; uint16_t color; };

struct GFX_PACKED GfxRectPayload         { int16_t x; int16_t y; int16_t w; int16_t h; uint16_t color; };
struct GFX_PACKED GfxRoundRectPayload    { int16_t x; int16_t y; int16_t w; int16_t h; int16_t r; uint16_t color; };
struct GFX_PACKED GfxCirclePayload       { int16_t x; int16_t y; int16_t r; uint16_t color; };
struct GFX_PACKED GfxTrianglePayload     { int16_t x0; int16_t y0; int16_t x1; int16_t y1; int16_t x2; int16_t y2; uint16_t color; };

// Bitmap payloads — bitmap bytes follow immediately after struct
struct GFX_PACKED GfxBitmapPayload       { int16_t x; int16_t y; int16_t w; int16_t h; };
struct GFX_PACKED GfxBitmapBgPayload     { int16_t x; int16_t y; int16_t w; int16_t h; uint16_t fg; uint16_t bg; };

struct GFX_PACKED GfxSetCursorPayload      { int16_t x; int16_t y; };
struct GFX_PACKED GfxSetTextColorPayload   { uint16_t color; };
struct GFX_PACKED GfxSetTextColorBgPayload { uint16_t fg; uint16_t bg; };
struct GFX_PACKED GfxSetTextSizePayload    { uint8_t size; };
struct GFX_PACKED GfxSetTextWrapPayload    { uint8_t wrap; };
struct GFX_PACKED GfxCp437Payload          { uint8_t enable; };
struct GFX_PACKED GfxSetFontPayload        { uint16_t fontId; };
struct GFX_PACKED GfxWriteCharPayload      { uint8_t c; };
struct GFX_PACKED GfxGetTextBoundsPayload  { int16_t x; int16_t y; /* text bytes follow */ };

// ── Touch control payloads ────────────────────────────────────────────────────

// TOUCH_BEGIN payload.
// mode:
//   0x00 = single touch — emulates Adafruit_TouchScreen (resistive) and
//          Adafruit_FT6206 / Adafruit_CST816 (capacitive single-touch).
//          Sketch code using getPoint() + p.z > MINPRESSURE works unchanged.
//   0x01 = single touch + gestures (swipe, double-tap, long-press) -- future
//   0x02 = multi-touch, up to 5 points, different API -- future
// move_interval_ms: throttle between TOUCH_MOVE events (uint16 LE).
//   0 = every event, 50 = 50ms (20Hz default), 100 = 10Hz.
struct GFX_PACKED GfxTouchBeginPayload {
    uint8_t  mode;               // touch mode (see above)
    uint16_t move_interval_ms;   // TOUCH_MOVE throttle in ms (little-endian)
};

// TOUCH_END — no payload, send as zero-byte payload frame

// TOUCH_DELAY payload — update move throttle while touch is active
struct GFX_PACKED GfxTouchDelayPayload {
    uint16_t move_interval_ms;   // new throttle in ms (little-endian)
};

#if !defined(__GNUC__)
#  pragma pack(pop)
#endif