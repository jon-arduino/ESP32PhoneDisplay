// BLE_HelloWorld — minimal ESP32PhoneDisplay example
//
// Draws a simple screen on the iPhone over BLE. Demonstrates the three
// connection callbacks and when each fires:
//
//   onDisplayAvailable(true)  — display is ready. Primary signal for starting
//     or resuming drawing. Sent ~100ms after connect/reconnect AND when app
//     returns to foreground. Use this to trigger initial draw and exit any
//     wait loops. Sets _drawPending and clears _paused.
//
//   onDisplayAvailable(false) — display is offline. Sent when app is
//     backgrounded, phone locked, call takes over, or clean disconnect.
//     Pause drawing here — no point sending commands.
//
//   onRedrawRequest — display state is unknown/stale. Sent when app returns
//     to foreground after being backgrounded (in addition to
//     onDisplayAvailable(true)). On fresh connect, only onDisplayAvailable
//     fires — not onRedrawRequest.
//
//   onSubscribed — GATT-level BLE connect/disconnect. Kept as fallback for
//     older app versions that don't send BC_CMD_DISPLAY_AVAILABLE. A
//     double-draw if both fire is harmless.
//
// The green info box shows which callbacks fired on the last draw — making
// it easy to test and verify each scenario. Press T1 or T2 to do a clean
// redraw and reset the badges for the next test.
//
// Test scenarios:
//   Fresh connect              → onDisplayAvailable only
//   Disconnect + reconnect     → onDisplayAvailable only
//   Switch app + return        → onDisplayAvailable + onRedrawRequest
//   Phone locked + unlock      → onDisplayAvailable + onRedrawRequest
//   T1 / T2                    → clean redraw, no badges

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
#define CYAN        0x07FF
#define DARK_GREY   0x4208

// ── Display dimensions ────────────────────────────────────────────────────────
#define DISP_W  240
#define DISP_H  320

// #Ported: Adafruit_ST7735 display(TFT_CS, TFT_DC, TFT_RST);
BleTransport      transport;
ESP32PhoneDisplay display(transport);

// ── Volatile flags — set on NimBLE task (core 0), read on loop task (core 1) ─
static volatile bool    _drawPending      = false;
static volatile bool    _displayAvailable = false;  // onDisplayAvailable(true) fired
static volatile bool    _redrawRequested  = false;  // onRedrawRequest fired
static volatile bool    _paused           = false;
static volatile uint8_t _keyPending       = 0;      // '1'=T1  '2'=T2  0=none

// ── Forward declarations ──────────────────────────────────────────────────────
void drawScreen(bool showDisplayAvail, bool showRedraw);
void drawKeyBanner(uint8_t key);

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // onDisplayAvailable — primary signal for display ready/offline.
    // available=true:  display ready — resume drawing, trigger redraw.
    //   Fires ~100ms after connect/reconnect AND on app foreground.
    // available=false: display offline — pause drawing.
    //   Fires on app background, phone lock, app switch, clean disconnect.
    // #Ported: no equivalent — local displays are always ready.
    transport.onDisplayAvailable([](bool available) {
        if (available) {
            _paused           = false;
            _displayAvailable = true;
            _drawPending      = true;
        } else {
            _paused = true;
        }
    });

    // onRedrawRequest — display state is stale, rebuild current display.
    // Fires when app returns to foreground after being backgrounded.
    // NOT fired on fresh connect — onDisplayAvailable handles that.
    // #Ported: no equivalent.
    transport.onRedrawRequest([]() {
        _redrawRequested = true;
        _drawPending     = true;
    });

    // onSubscribed — GATT-level BLE connect (true) / disconnect (false).
    // Kept as fallback for older app versions that don't send
    // BC_CMD_DISPLAY_AVAILABLE. Double-draw if both fire is harmless.
    // #Ported: no equivalent.
    transport.onSubscribed([](bool ready) {
        if (ready) { _paused = false; _drawPending = true; }
        else         _paused = true;
    });

    // onKey — T1 ('1') or T2 ('2') toolbar button pressed.
    // Triggers clean full redraw — clears all badges for next test.
    // #Ported: no equivalent — local displays have no back-channel.
    transport.onKey([](uint8_t key) {
        _displayAvailable = false;
        _redrawRequested  = false;
        _keyPending       = key;
    });

    // #Ported: display.initR(INITR_BLACKTAB); display.setRotation(0);
    transport.begin();
    Serial.println("[BLE] Advertising — waiting for iPhone...");
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    // Diagnostic — prints when BC_CMD_DISPLAY_AVAILABLE is received.
    // If this never prints, the iOS app version does not support this command.
    static uint32_t lastDisplayAvail = 0;
    uint32_t displayAvailCount = transport.bcStats().displayAvailable;
    if (displayAvailCount != lastDisplayAvail) {
        Serial.printf("[BC] displayAvailable events=%u\n", displayAvailCount);
        Serial.flush();
        lastDisplayAvail = displayAvailCount;
    }

    if (_paused) { delay(100); return; }

    // T1 or T2 — clean full redraw then static key banner
    if (_keyPending) {
        uint8_t key  = _keyPending;
        _keyPending  = 0;
        _drawPending = false;
        Serial.printf("[Key] T%c — clean redraw\n", key);
        Serial.flush();
        drawScreen(false, false);
        drawKeyBanner(key);
        return;
    }

    // Draw on connect, display-available, or redraw request
    if (_drawPending) {
        bool dispAvail = _displayAvailable;
        bool redraw    = _redrawRequested;
        _drawPending      = false;
        _displayAvailable = false;
        _redrawRequested  = false;

        if (dispAvail && redraw)
            Serial.println("[Display] onDisplayAvailable + onRedrawRequest");
        else if (dispAvail)
            Serial.println("[Display] onDisplayAvailable");
        else if (redraw)
            Serial.println("[Display] onRedrawRequest");
        else
            Serial.println("[Display] onSubscribed fallback");
        Serial.flush();

        drawScreen(dispAvail, redraw);
    }

    delay(20);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawScreen(bool showDisplayAvail, bool showRedraw)
{
    // begin() establishes the phone session — re-call on every connect.
    // #Ported: display.initR(INITR_BLACKTAB); — local init is one-time only.
    display.begin(DISP_W, DISP_H);
    display.setTitle("Hello World");
    display.setButton1("T1");
    display.setButton2("T2");

    display.fillScreen(BLACK);  // #Ported: identical

    // Header bar
    display.fillRect(0, 0, DISP_W, 50, BLUE);
    display.setCursor(20, 15);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("Hello iPhone!");

    // Info box
    display.drawRoundRect(20, 70, DISP_W - 40, 80, 8, GREEN);
    display.setTextSize(1);
    display.setTextColor(GREEN);
    display.setCursor(35, 85);
    display.print("ESP32PhoneDisplay BLE");
    display.setCursor(35, 97);
    display.print("T1/T2: clean redraw");
    display.setCursor(35, 109);
    display.print("Lock/unlock: both badges");
    display.setCursor(35, 121);
    display.print("Reconnect: avail only");
    display.setCursor(35, 133);
    display.print("App switch: both badges");

    // Callback badges — show which events triggered this draw.
    // onDisplayAvailable fires on connect/reconnect and app foreground.
    // onRedrawRequest fires only on app foreground (not fresh connect).
    // Seeing both means the app returned from background.
    int badgeY = 165;
    if (showDisplayAvail) {
        display.fillRect(20, badgeY, DISP_W - 40, 18, GREEN);
        display.setCursor(28, badgeY + 4);
        display.setTextColor(BLACK);
        display.setTextSize(1);
        display.print("onDisplayAvailable received");
        badgeY += 22;
    }
    if (showRedraw) {
        display.fillRect(20, badgeY, DISP_W - 40, 18, CYAN);
        display.setCursor(28, badgeY + 4);
        display.setTextColor(BLACK);
        display.setTextSize(1);
        display.print("onRedrawRequest received");
    }

    // Shapes
    display.fillCircle( 70, 270, 28, RED);
    display.fillCircle(170, 270, 28, BLUE);
    display.drawCircle(120, 270, 38, YELLOW);

    // flush() sends sync marker and wakes BLE drain task.
    // #Ported: no equivalent — local displays render immediately.
    display.flush();
}

void drawKeyBanner(uint8_t key)
{
    display.fillRect(0, 290, DISP_W, 30, (key == '1') ? RED : BLUE);
    display.setCursor(60, 299);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("T");
    display.print((char)key);
    display.print(" pressed");
    display.flush();
}
