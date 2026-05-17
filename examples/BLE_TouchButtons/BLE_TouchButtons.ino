// BLE_TouchButtons — touch button demo with performance comparison
//
// Shows three colour buttons on the iPhone display. Tap a button to activate
// it. Toolbar buttons T1 and T2 update the status area when pressed.
//
// Demonstrates a simple connection model suited to apps where the display
// is self-managing — buttons are always redrawn from current state when
// touched, so no redraw is needed on reconnect. Drawing simply resumes
// when the link comes back up.
//
// Connection handling:
//   onDisplayAvailable — primary signal. Pauses interaction when display goes
//     offline. Resumes when back. No redraw triggered.
//   onConnected/onDisconnected — transport-level fallback.
//   onRedrawRequest — fires when app returns from background. Triggers a
//     full button redraw to rebuild any content iOS may have cleared.
//
// Also demonstrates the performance difference between compat and native mode:
//
//   tft (ESP32PhoneDisplay_Compat) — Adafruit_GFX subclass, required to pass
//     an Adafruit_GFX* to Adafruit_GFX_Button. Text rendering decomposes to
//     pixel-level commands. RED button uses this — visibly slower.
//
//   display (ESP32PhoneDisplay) — native, compact commands. fillRoundRect,
//     drawRoundRect and print() each send a single BLE command. GREEN and
//     BLUE buttons use this — visibly faster.
//
// Both objects share the same BleTransport. tft.begin() establishes the
// session — display.begin() is intentionally omitted to avoid resetting it.
// See docs/porting.md for the full compat vs native explanation.
//
// Porting from Adafruit_GFX: drawing calls are identical. Only setup and
// connection handling change — see #Ported comments throughout.

#include <ESP32PhoneDisplay.h>
#include <ESP32PhoneDisplay_Compat.h>
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>
#include <Adafruit_GFX.h>

// ── Colours ───────────────────────────────────────────────────────────────────
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define GREY    0x7BEF
#define DKGREEN 0x03E0

// ── Layout ────────────────────────────────────────────────────────────────────
#define DISP_W      240
#define DISP_H      320
#define BTN_X       20
#define BTN_W       180
#define BTN_H       50
#define BTN_RADIUS  8
#define BTN_RED_Y   75
#define BTN_GREEN_Y 140
#define BTN_BLUE_Y  205
#define STATUS_Y    270
#define STATUS_H    50

// ── Objects ───────────────────────────────────────────────────────────────────
BleTransport             transport;
ESP32PhoneDisplay_Compat tft(transport, DISP_W, DISP_H);  // compat — for Adafruit_GFX_Button
ESP32PhoneDisplay        display(transport);               // native — for fast GREEN/BLUE drawing
RemoteTouchScreen        ts(transport);

// Adafruit_GFX_Button used for touch state only (contains/press/justPressed).
// initButtonUL() stores geometry but sends no BLE commands.
// drawButton() is only called on RED — GREEN and BLUE are drawn via display.
Adafruit_GFX_Button btnRed, btnGreen, btnBlue;

// ── Volatile flags — set on NimBLE task (core 0), read on loop task (core 1) ─
// Never call display functions or Serial from a BLE callback — set a flag
// and act on it in loop() instead. This avoids concurrency issues and keeps BLE callbacks fast.
static volatile bool    _displayOffline = false;  // set when display is offline. Cleared on connect.
static volatile bool    _redrawPending  = false;  // set when buttons need redrawing (app foreground return).
static volatile uint8_t _keyPending     = 0;      // toolbar key press — '1'=T1  '2'=T2  0=none
static volatile bool    _displayReset   = true;   // set when new connection may have lost session state and display should be reset with begin(). Cleared in loop() after handling.

// ── Forward declarations ──────────────────────────────────────────────────────
void initSession();
void drawRedButton(bool active);
void drawGreenButton(bool active);
void drawBlueButton(bool active);
void drawButtons(bool redActive, bool greenActive, bool blueActive);
void updateStatus(const char *msg, uint16_t color);

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // ── Register transport callbacks ──────────────────────────────────────────
    // Callbacks fire automatically on the BLE task (core 0). Only set flags
    // here — drawing and Serial happen safely in loop() on core 1.

    // onDisplayAvailable — primary signal for display ready/offline.
    //   available=true:  resume drawing — fires ~100ms after connect and
    //                    when app returns to foreground.
    //   available=false: pause drawing — app backgrounded, phone locked,
    //                    or BT disconnect.
    transport.onDisplayAvailable([](bool available) {
        _displayOffline = !available;
    });

    // onRedrawRequest — fires when app returns to foreground after backgrounding.
    // iOS may have cleared the framebuffer so buttons are redrawn from current state.
    transport.onRedrawRequest([]() {
        _redrawPending = true;
    });

    // onConnected/onDisconnected — transport-level fallback for
    // display available/offline if onDisplayAvailable doesn't fire.
    transport.onConnected([]() {
        _displayOffline = false;
    });
    transport.onDisconnected([]() {
        _displayOffline = true;
        _displayReset   = true;   // session state is lost on disconnect — set flag to reset with begin() on next connect
    });

    // onKey — fires when toolbar button pressed in iPhone app.
    // T1 sends '1', T2 sends '2'.
    transport.onKey([](uint8_t key) {
        _keyPending = key;
    });

    transport.begin();
    Serial.println("[BLE] Advertising — waiting for iPhone...");

    // Wait for first connection before initialising display and touch.
    while (_displayOffline || !transport.canSend()) delay(100);
    initSession();
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    // Pause while display is offline — no point sending commands.
    if (_displayOffline) { delay(100); return; }

    if (_displayReset) { // Bt disconnect or new connection may have lost session state — reset display.
        _displayReset = false;
        Serial.println("[Display] Resetting display with initSession()");
        Serial.flush();
        initSession();
        return;
    }   

    // Redraw buttons from current state when app returns from background.
    if (_redrawPending) {
        _redrawPending = false;
        Serial.println("[Display] Redrawing buttons");
        Serial.flush();
        drawButtons(false, false, false);
        updateStatus("Tap a button", GREY);
        return;
    }

    // Toolbar key press
    if (_keyPending) {
        uint8_t key = _keyPending;
        _keyPending  = 0;
        if (key == '1') {
            updateStatus("Toolbar: T1", WHITE);
            Serial.println("[Key] T1");
        } else {
            updateStatus("Toolbar: T2", WHITE);
            Serial.println("[Key] T2");
        }
        Serial.flush();
        return;
    }

    // Touch — read position and update button state
    TSPoint p        = ts.getPoint();
    bool    touching = (p.z > RemoteTouchScreen::MINPRESSURE);

    btnRed.press  (touching && btnRed.contains  (p.x, p.y));
    btnGreen.press(touching && btnGreen.contains(p.x, p.y));
    btnBlue.press (touching && btnBlue.contains (p.x, p.y));

    if (btnRed.justPressed()) {
        drawButtons(true, false, false);
        updateStatus("RED selected", RED);
        Serial.println("[Touch] RED");
        Serial.flush();
    }
    if (btnGreen.justPressed()) {
        drawButtons(false, true, false);
        updateStatus("GREEN selected", GREEN);
        Serial.println("[Touch] GREEN");
        Serial.flush();
    }
    if (btnBlue.justPressed()) {
        drawButtons(false, false, true);
        updateStatus("BLUE selected", BLUE);
        Serial.println("[Touch] BLUE");
        Serial.flush();
    }
}

// ── Session init — called once on first connect ───────────────────────────────

void initSession()
{
    // tft.begin() establishes the phone session for both tft and display —
    // they share the same transport. display.begin() intentionally omitted;
    // a second GFX_CMD_BEGIN would reset the session.
    // On reconnect, session state is intact — begin() not needed.
    tft.begin();
    tft.setTitle("Touch Buttons");
    tft.setButton1("T1");
    tft.setButton2("T2");

    tft.fillScreen(BLACK);

    // Title text via display.print() for single-command-per-character rendering
    display.setCursor(20, 20);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("Touch Buttons");

    // Register button geometry for touch hit-testing — no BLE commands sent
    btnRed.initButtonUL  (&tft, BTN_X, BTN_RED_Y,   BTN_W, BTN_H, WHITE, RED,     WHITE, (char*)"RED",   2);
    btnGreen.initButtonUL(&tft, BTN_X, BTN_GREEN_Y,  BTN_W, BTN_H, WHITE, DKGREEN, WHITE, (char*)"GREEN", 2);
    btnBlue.initButtonUL (&tft, BTN_X, BTN_BLUE_Y,   BTN_W, BTN_H, WHITE, BLUE,   WHITE, (char*)"BLUE",  2);

    drawButtons(false, false, false);
    updateStatus("Tap a button", GREY);

    ts.begin();

    Serial.println("[Display] Ready");
    Serial.flush();
}

// ── Button drawing ────────────────────────────────────────────────────────────

// RED — slow path via Adafruit_GFX_Button::drawButton().
// Text rendering decomposes each glyph into ~35 pixel-level BLE commands.
void drawRedButton(bool active)
{
    btnRed.drawButton(active);
}

// GREEN — fast path via ESP32PhoneDisplay (display).
// fillRoundRect(1) + drawRoundRect(1) + setTextSize(1) + setCursor(1) +
// setTextColor(1) + print chars(5) = ~10 commands vs ~200+ for RED.
// getTextBounds() on tft is pure local math — sends no BLE commands.
void drawGreenButton(bool active)
{
    uint16_t fill = active ? WHITE   : DKGREEN;
    uint16_t text = active ? DKGREEN : WHITE;

    display.fillRoundRect(BTN_X, BTN_GREEN_Y, BTN_W, BTN_H, BTN_RADIUS, fill);
    display.drawRoundRect(BTN_X, BTN_GREEN_Y, BTN_W, BTN_H, BTN_RADIUS, WHITE);

    int16_t tx, ty; uint16_t tw, th;
    tft.setTextSize(2);
    tft.getTextBounds("GREEN", 0, 0, &tx, &ty, &tw, &th);
    display.setTextSize(2);
    display.setCursor(BTN_X + (BTN_W - tw) / 2, BTN_GREEN_Y + (BTN_H - th) / 2);
    display.setTextColor(text);
    display.print("GREEN");
}

// BLUE — same fast path as GREEN.
void drawBlueButton(bool active)
{
    uint16_t fill = active ? WHITE : BLUE;
    uint16_t text = active ? BLUE  : WHITE;

    display.fillRoundRect(BTN_X, BTN_BLUE_Y, BTN_W, BTN_H, BTN_RADIUS, fill);
    display.drawRoundRect(BTN_X, BTN_BLUE_Y, BTN_W, BTN_H, BTN_RADIUS, WHITE);

    int16_t tx, ty; uint16_t tw, th;
    tft.setTextSize(2);
    tft.getTextBounds("BLUE", 0, 0, &tx, &ty, &tw, &th);
    display.setTextSize(2);
    display.setCursor(BTN_X + (BTN_W - tw) / 2, BTN_BLUE_Y + (BTN_H - th) / 2);
    display.setTextColor(text);
    display.print("BLUE");
}

// All three buttons flush together in a single batch.
// See docs/porting.md for the full compat vs native packet analysis.
void drawButtons(bool redActive, bool greenActive, bool blueActive)
{
    drawRedButton(redActive);
    drawGreenButton(greenActive);
    drawBlueButton(blueActive);
    tft.flush();
}

// ── Status bar ────────────────────────────────────────────────────────────────

void updateStatus(const char *msg, uint16_t color)
{
    tft.fillRect(0, STATUS_Y, DISP_W, STATUS_H, BLACK);
    display.setCursor(10, STATUS_Y + (STATUS_H - 16) / 2);
    display.setTextColor(color);
    display.setTextSize(2);
    display.print(msg);
    tft.flush();
}
