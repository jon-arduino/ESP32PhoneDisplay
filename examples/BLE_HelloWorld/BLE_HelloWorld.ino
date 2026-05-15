// BLE_HelloWorld — minimal ESP32PhoneDisplay example
//
// A simple useful BLE display sketch. Draws a screen on the iPhone,
// handles reconnect cleanly, and responds to toolbar button presses.
//
// This is a good starting point for new sketches. For a detailed
// walkthrough of all connection callbacks and protocol diagnostics,
// see BLE_protocol_diagnostics.
//
// Porting from Adafruit_GFX: all drawing calls are identical. Only setup
// and connection handling change — see "#Ported;" comments throughout to see exactly 
// what changed and why. For complete porting instructions, see docs/porting.md.

// #Ported: #include <Adafruit_ST7735.h>
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>

// ── Colours (RGB565) ──────────────────────────────────────────────────────────
#define BLACK       0x0000
#define WHITE       0xFFFF
#define RED         0xF800
#define GREEN       0x07E0
#define BLUE        0x001F
#define YELLOW      0xFFE0
#define LIGHT_GREY  0xC618

// ── Display dimensions ────────────────────────────────────────────────────────
#define DISP_W  240
#define DISP_H  320

// #Ported: Adafruit_ST7735 display(TFT_CS, TFT_DC, TFT_RST);
BleTransport      transport;    //setup bluetooth transport
ESP32PhoneDisplay display(transport);  //create display instance, passing transport reference

// ── Volatile flags — set on NimBLE task (core 0), read on loop task (core 1) ─
// Never call display functions or Serial from a BLE callback — set a flag
// and act on it in loop() instead.  This avoids concurrency issues and keeps BLE callbacks fast.
static volatile bool    _redrawPending = false;   // Set when we need to redraw the screen due to a connect, reconnect, or redraw request event. Cleared in loop() after handling.
static volatile bool    _displayOffline      = false; // Set when the display is offline (app backgrounded, phone locked, or clean disconnect). Cleared on connect.
static volatile uint8_t _keyPending  = 0;   // tracks button pushes'1'=T1  '2'=T2  0=none

// ── Forward declarations ──────────────────────────────────────────────────────
void drawScreen();
void drawKeyBanner(uint8_t key);

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // onDisplayAvailable — primary signal for display ready/offline.
    // available=true:  display ready — sent ~100ms after connect/reconnect
    //                  and when app returns to foreground.
    // available=false: display offline — app backgrounded, phone locked,
    //                  or clean disconnect. Pause drawing here.
    // #Ported: no equivalent — local displays are always ready.
    transport.onDisplayAvailable([](bool available) {
        if (available) { _displayOffline = false; _redrawPending = true; }
        else             _displayOffline = true;
    });

    // onRedrawRequest — display state may be stale, rebuild current display.
    // Fired when app returns from background (in addition to onDisplayAvailable).
    // Not fired on fresh connect — onDisplayAvailable covers that.
    // #Ported: no equivalent.
    transport.onRedrawRequest([]() {
        _redrawPending = true;
    });

    // onSubscribed — fallback for older app versions that don't send
    // BC_CMD_DISPLAY_AVAILABLE. A double-draw if both fire is harmless.
    // Also catches the disconnect case (ready=false).
    // #Ported: no equivalent.
    transport.onSubscribed([](bool ready) {
        if (ready) { _displayOffline = false; _redrawPending = true; }
        else         _displayOffline = true;
    });

    // onKey — T1 ('1') or T2 ('2') toolbar button pressed.
    // #Ported: no equivalent — local displays have no back-channel.
    transport.onKey([](uint8_t key) {
        _keyPending = key;
    });

    // #Ported: display.initR(INITR_BLACKTAB); display.setRotation(0);
    transport.begin();
    Serial.println("[BLE] Advertising — waiting for iPhone...");
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    if (_displayOffline) { delay(100); return; }

    if (_keyPending) {
        uint8_t key = _keyPending;
        _keyPending  = 0;
        Serial.printf("[Key] T%c pressed\n", key);
        Serial.flush();
        drawKeyBanner(key);
        return;
    }

    if (_redrawPending) {
        _redrawPending = false;
        Serial.println("[Display] Drawing screen");
        Serial.flush();
        drawScreen();
    }

    delay(20);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawScreen()
{
    // begin() establishes the phone session — re-call on every connect.
    // setTitle/setButton re-sent here since phone may have lost session state.
    // #Ported: display.initR(INITR_BLACKTAB); — local init is one-time only.
    display.begin(DISP_W, DISP_H);
    display.setTitle("Hello World");
    display.setButton1("T1");

    // All drawing calls below are identical to Adafruit_GFX — no changes needed.
    display.fillScreen(BLACK);      // #Ported: display.fillScreen(ST77XX_BLACK); — identical

    // Header bar
    display.fillRect(0, 0, DISP_W, 50, BLUE);
    display.setCursor(20, 15);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("Hello iPhone!");

    // Info box
    display.drawRoundRect(20, 70, DISP_W - 40, 70, 8, GREEN);
    display.setTextSize(1);
    display.setTextColor(GREEN);
    display.setCursor(35, 88);
    display.print("ESP32PhoneDisplay");
    display.setCursor(35, 103);
    display.print("BLE transport active");
    display.setCursor(35, 118);
    display.print("Press T1 for a banner");

    // Shapes — identical to any Adafruit_GFX sketch
    display.fillCircle( 70, 230, 35, RED);
    display.fillCircle(170, 230, 35, BLUE);
    display.drawCircle(120, 230, 50, YELLOW);

    display.setCursor(20, 295);
    display.setTextColor(LIGHT_GREY);
    display.setTextSize(1);
    display.print("Switch apps or power off/on to test display redraw"); 

    // flush() sends a sync marker and wakes the BLE drain task.
    // Auto-flush also delivers commands, but explicit flush ensures
    // deterministic frame delivery.
    // #Ported: no equivalent — local displays render immediately.
    display.flush();

    Serial.println("[Display] Done");
    Serial.flush();
}

void drawKeyBanner(uint8_t key)
{
    display.fillRect(0, 275, DISP_W, 45, (key == '1') ? RED : BLUE);
    display.setCursor(55, 288);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("T");
    display.print((char)key);
    display.print(" pressed");
    display.flush();    // #Ported: no equivalent
}
