// GFX_Pointer — using libraries that require Adafruit_GFX*
//
// Some Arduino libraries and sketches accept Adafruit_GFX* (or Adafruit_GFX&)
// to draw to any compatible display. This example shows the three ways to
// handle this when porting to ESP32PhoneDisplay, in order of preference.
//
// ── Option 1 — Modify the library (BEST) ─────────────────────────────────────
//
// If you own the code or can copy it, change the parameter type from
// Adafruit_GFX* to ESP32PhoneDisplay*. All method names are identical so
// the function body does not change. Full native performance, no compat needed.
//
//   Before:  void drawPanel(Adafruit_GFX* gfx) { gfx->fillRect(...); }
//   After:   void drawPanel(ESP32PhoneDisplay* gfx) { gfx->fillRect(...); }
//
// For Adafruit_GFX_Button specifically: copy the source, change the pointer
// type, and you have a fully native button class. Five-minute edit.
//
// ── Option 2 — Dual-object pattern (GOOD) ────────────────────────────────────
//
// When you cannot modify the library (third-party, closed source), hold a
// compat object alongside the native object on the same transport. Pass the
// compat object where Adafruit_GFX* is required, use native for all your
// own drawing. See the example below and BLE_TouchButtons for a full demo.
//
// ── Option 3 — Compat for everything (AVOID) ─────────────────────────────────
//
// Using ESP32PhoneDisplay_Compat for all drawing works but is the slowest
// option. Text and shapes decompose to pixel-level BLE commands via
// Adafruit_GFX's non-virtual base. Only use compat where the pointer is
// actually required — use native everywhere else.

#include <ESP32PhoneDisplay.h>
#include <ESP32PhoneDisplay_Compat.h>
#include <transport/BleTransport.h>
#include <Adafruit_GFX.h>   // for Adafruit_GFX_Button

// ── Objects ───────────────────────────────────────────────────────────────────
BleTransport             transport;
ESP32PhoneDisplay_Compat tft(transport, 240, 320);   // compat — satisfies Adafruit_GFX*
ESP32PhoneDisplay        display(transport);          // native — for all our own drawing

// ── Example third-party function requiring Adafruit_GFX* ─────────────────────
// Simulates a library function you cannot modify.
// Replace with your actual library call.
void thirdPartyDrawPanel(Adafruit_GFX* gfx, uint16_t color)
{
    gfx->fillRoundRect(20, 80, 200, 60, 8, color);
    gfx->setCursor(40, 102);
    gfx->setTextColor(0xFFFF);
    gfx->setTextSize(2);
    gfx->print("Third party");
}

// ── Colours ───────────────────────────────────────────────────────────────────
#define BLACK   0x0000
#define WHITE   0xFFFF
#define BLUE    0x001F
#define GREEN   0x07E0
#define YELLOW  0xFFE0
#define DKGREY  0x4208

// ── Volatile flags ────────────────────────────────────────────────────────────
static volatile bool _drawPending = false;

// ── Forward declarations ──────────────────────────────────────────────────────
void drawDemo();
void thirdPartyDrawPanel(Adafruit_GFX* gfx, uint16_t color);

void setup()
{
    Serial.begin(115200);

    transport.onConnected([]()    { _drawPending = true; });
    transport.onDisconnected([]() { _drawPending = false; });

    transport.begin();
    Serial.println("[BLE] Advertising — waiting for iPhone...");

    while (!_drawPending) delay(100);
    _drawPending = false;

    // tft.begin() establishes the session for BOTH objects — they share the
    // same transport. display.begin() intentionally omitted; a second
    // GFX_CMD_BEGIN would reset the session.
    tft.begin();
    tft.setTitle("GFX Pointer Demo");

    drawDemo();
}

void loop()
{
    if (_drawPending) {
        _drawPending = false;
        tft.begin();
        tft.setTitle("GFX Pointer Demo");
        drawDemo();
    }
    delay(20);
}

void drawDemo()
{
    // ── Our own drawing — always use native display ───────────────────────────
    display.fillScreen(BLACK);

    display.fillRect(0, 0, 240, 50, DKGREY);
    display.setCursor(10, 15);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("GFX Pointer Demo");

    display.setCursor(10, 165);
    display.setTextColor(YELLOW);
    display.setTextSize(1);
    display.print("Above: third-party (compat)");
    display.setCursor(10, 178);
    display.print("Below: our drawing (native)");
    display.setCursor(10, 198);
    display.setTextColor(GREEN);
    display.print("Both share one transport.");
    display.setCursor(10, 211);
    display.print("tft.begin() serves both.");

    // ── Third-party call — pass &tft (Adafruit_GFX*) ─────────────────────────
    // The third-party function draws using compat — pixel-decomposed commands.
    // Our own code uses native — compact single commands.
    thirdPartyDrawPanel(&tft, BLUE);

    // ── Native drawing continues after third-party call ───────────────────────
    display.fillRoundRect(20, 230, 200, 60, 8, GREEN);
    display.setCursor(40, 253);
    display.setTextColor(BLACK);
    display.setTextSize(2);
    display.print("Native draw");

    // Both objects write to the same BLE stream — flush via either one.
    display.flush();

    Serial.println("[Display] Done");
    Serial.flush();
}
