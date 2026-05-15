// BLE_TouchButtons — touch button demo with performance comparison
//
// Shows three buttons on the iPhone display. Tap a button to activate it.
// Toolbar buttons T1 and T2 update the status area when pressed.
//
// Demonstrates the performance difference between two drawing approaches
// by holding two display objects against the same transport simultaneously:
//
//   tft     — ESP32PhoneDisplay_Compat (Adafruit_GFX subclass)
//               Required for Adafruit_GFX_Button, which needs an Adafruit_GFX*.
//               Text rendering routes through Adafruit_GFX's pixel-level font
//               engine. Each character at textSize 2 decomposes into ~35
//               individual BLE commands. "RED" alone = ~105 commands.
//
//   display — ESP32PhoneDisplay (native, fast)
//               fillRoundRect / drawRoundRect each send one compact command.
//               print() sends one GFX_CMD_WRITE_CHAR per character; the iPhone
//               renders each glyph. "GREEN" = 5 commands, "BLUE" = 4 commands.
//               Total for GREEN or BLUE button: ~10 commands vs ~200+ for RED.
//
// Both objects share the same BleTransport — commands from either object
// flow into the same BLE stream in call order. tft.begin() is called once;
// calling display.begin() would send a second GFX_CMD_BEGIN and reset the
// phone session, so it is intentionally omitted.
//
// Also demonstrates:
//   - setTitle() / setButton1() / setButton2() — iPhone nav bar and toolbar
//   - onRedrawRequest() — clean reconnect redraw
//   - volatile flag pattern for onKey() — safe cross-core signalling

#include <ESP32PhoneDisplay.h>           // ESP32PhoneDisplay — fast native path
#include <ESP32PhoneDisplay_Compat.h>    // ESP32PhoneDisplay_Compat — Adafruit_GFX subclass
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>
#include <Adafruit_GFX.h>               // Adafruit_GFX_Button

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
#define STATUS_Y    270     // 15px gap below BLUE button (bottom at 255)
#define STATUS_H    50

// ── Objects ───────────────────────────────────────────────────────────────────
BleTransport             transport;
ESP32PhoneDisplay_Compat tft(transport, DISP_W, DISP_H);  // compat — for Adafruit_GFX_Button
ESP32PhoneDisplay        display(transport);               // native — for fast GREEN/BLUE drawing
RemoteTouchScreen        ts(transport);

// ── Buttons — used for touch state only (contains/press/justPressed) ──────────
// initButtonUL() stores geometry and colours but renders nothing.
// drawButton() is only called on RED — GREEN and BLUE are drawn via display.
Adafruit_GFX_Button btnRed, btnGreen, btnBlue;

// ── State ─────────────────────────────────────────────────────────────────────
static volatile bool _drawPending = false;

// Key press flag — set by onKey() on core 0, read and cleared in loop() on core 1.
// onKey() runs inside the NimBLE host task. Serial.println() and any function
// that calls tft.print() or display.print() (which write to the BLE stream
// buffer) are unsafe from that context. Use a volatile flag and act in loop().
static volatile int _keyMsg = 0;   // 0=none  1=T1  2=T2

// ── Forward declarations ──────────────────────────────────────────────────────
void initDisplay();
void drawRedButton(bool active);
void drawGreenButton(bool active);
void drawBlueButton(bool active);
void drawButtons(bool redActive, bool greenActive, bool blueActive);
void updateStatus(const char *msg, uint16_t color);

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    transport.onSubscribed([](bool ready) {
        if (ready) _drawPending = true;
    });

    transport.onRedrawRequest([]() {
        _drawPending = true;
    });

    // Set flag only — do not call Serial or tft/display from here.
    // See volatile flag comment above.
    transport.onKey([](uint8_t key) {
        if      (key == '1') _keyMsg = 1;
        else if (key == '2') _keyMsg = 2;
    });

    transport.begin();
    Serial.println("[BLE] Waiting for iPhone...");

    while (!_drawPending) delay(100);
    _drawPending = false;

    initDisplay();
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop()
{
    // Reconnect redraw — rebuild full screen from scratch
    if (_drawPending) {
        _drawPending = false;
        initDisplay();
        return;
    }

    // Toolbar key press — handled here on core 1 where Serial and display are safe
    if (_keyMsg != 0) {
        int key = _keyMsg;
        _keyMsg  = 0;
        if (key == 1) {
            updateStatus("Toolbar: T1", WHITE);
            Serial.println("[Key] T1");
        } else {
            updateStatus("Toolbar: T2", WHITE);
            Serial.println("[Key] T2");
        }
        return;
    }

    // Touch — read current position and update button state
    TSPoint p        = ts.getPoint();
    bool    touching = (p.z > RemoteTouchScreen::MINPRESSURE);

    btnRed.press  (touching && btnRed.contains  (p.x, p.y));
    btnGreen.press(touching && btnGreen.contains(p.x, p.y));
    btnBlue.press (touching && btnBlue.contains (p.x, p.y));

    if (btnRed.justPressed()) {
        drawButtons(true, false, false);
        updateStatus("RED selected", RED);
        Serial.println("[Touch] RED");
    }
    if (btnGreen.justPressed()) {
        drawButtons(false, true, false);
        updateStatus("GREEN selected", GREEN);
        Serial.println("[Touch] GREEN");
    }
    if (btnBlue.justPressed()) {
        drawButtons(false, false, true);
        updateStatus("BLUE selected", BLUE);
        Serial.println("[Touch] BLUE");
    }
}

// ── Display init ──────────────────────────────────────────────────────────────

void initDisplay()
{
    // tft.begin() sends GFX_CMD_BEGIN — establishes the phone display session.
    // display.begin() is intentionally not called; a second BEGIN would reset
    // the session and clear the screen mid-draw.
    tft.begin();
    tft.setTitle("Touch Buttons");
    tft.setButton1("T1");
    tft.setButton2("T2");

    tft.fillScreen(BLACK);

    // Title — use display.print() for single-command-per-character rendering
    display.setCursor(20, 20);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("Touch Buttons");

    // Register button geometry for touch hit-testing.
    // initButtonUL() stores x/y/w/h/colours but sends no BLE commands.
    btnRed.initButtonUL  (&tft, BTN_X, BTN_RED_Y,   BTN_W, BTN_H, WHITE, RED,     WHITE, (char*)"RED",   2);
    btnGreen.initButtonUL(&tft, BTN_X, BTN_GREEN_Y,  BTN_W, BTN_H, WHITE, DKGREEN, WHITE, (char*)"GREEN", 2);
    btnBlue.initButtonUL (&tft, BTN_X, BTN_BLUE_Y,   BTN_W, BTN_H, WHITE, BLUE,   WHITE, (char*)"BLUE",  2);

    drawButtons(false, false, false);
    updateStatus("Tap a button", GREY);

    ts.begin();

    Serial.println("[Display] Ready");
}

// ── Button drawing ────────────────────────────────────────────────────────────

// RED — slow path via Adafruit_GFX_Button::drawButton().
// Text rendering decomposes each glyph into ~35 pixel-level BLE commands via
// Adafruit_GFX's built-in font engine. Visibly slower than GREEN/BLUE.
void drawRedButton(bool active)
{
    btnRed.drawButton(active);
}

// GREEN — fast path via ESP32PhoneDisplay (display).
// fillRoundRect → 1 command  (GFX_CMD_FILL_ROUNDRECT)
// drawRoundRect → 1 command  (GFX_CMD_DRAW_ROUNDRECT)
// setTextSize   → 1 command
// setCursor     → 1 command
// setTextColor  → 1 command
// print()       → 1 GFX_CMD_WRITE_CHAR per character; iPhone renders the glyph
// Total: ~10 commands vs ~200+ for the RED button.
//
// getTextBounds() is called on tft (Adafruit_GFX) for text centering — it is
// pure local math and sends no BLE commands. ESP32PhoneDisplay has no equivalent.
void drawGreenButton(bool active)
{
    uint16_t fill = active ? WHITE   : DKGREEN;
    uint16_t text = active ? DKGREEN : WHITE;

    display.fillRoundRect(BTN_X, BTN_GREEN_Y, BTN_W, BTN_H, BTN_RADIUS, fill);
    display.drawRoundRect(BTN_X, BTN_GREEN_Y, BTN_W, BTN_H, BTN_RADIUS, WHITE);

    // getTextBounds on tft — local Adafruit_GFX math, zero BLE commands
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

// Draw all three buttons then flush once — all commands accumulate in the BLE
// stream buffer and drain together in a single batch.
//
// BLE packet capacity: 252 bytes per notification (MTU 255 - 3 ATT header).
// Each GFX command has 4 bytes of framing overhead + payload.
//
// RED via Adafruit_GFX_Button::drawButton() — compat mode:
//   fillRoundRect and drawRoundRect decompose to ~130 drawFastHLine/drawPixel
//   commands. Text "RED" at textSize 2 adds ~60 commands. Total ~8-10 packets.
//
// GREEN + BLUE via ESP32PhoneDisplay — native mode:
//   fillRoundRect(1) + drawRoundRect(1) + setTextSize(1) + setCursor(1) +
//   setTextColor(1) + print chars(4-5) = ~10 commands per button, ~80 bytes.
//   Both buttons together fit comfortably within one packet.
//
// Full drawButtons() total: ~9-11 packets. At a 15ms BLE connection interval
// this drains in 1-2 intervals (~15-30ms) — imperceptible.
//
// Original all-compat version (three drawButton() calls): ~25-30 packets,
// draining over 3-4 intervals (~375-450ms). Visibly slow.
//
// Takeaway: when total frame commands fit in 1-2 packets, compat mode overhead
// is negligible — it gets carried along in the batch. As compat command count
// grows, each additional packet adds one full connection interval to frame time.
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
