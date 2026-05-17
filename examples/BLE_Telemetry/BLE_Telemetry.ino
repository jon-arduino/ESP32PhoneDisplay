// BLE_Telemetry — live sensor data display over BLE
//
// Demonstrates an efficient partial-redraw pattern for live data:
//
//   drawLabelFrame() — draws the static layout once on connect.
//     Title bar and row labels never change and are not redrawn each update.
//
//   updateValues() — called every second, redraws only the value column.
//     Each update clears a small fillRect next to each label and reprints
//     the value. Static label areas are untouched.
//
// This keeps per-update BLE traffic very low — 4 × (fillRect + setCursor +
// setTextColor + 2× print) + flush — regardless of display size.
//
// Porting from Adafruit_GFX: all drawing calls are identical. Only setup
// and connection handling change — see #Ported comments throughout and
// docs/porting.md for the full porting guide.
//
// Replace the four readXxx() stub functions with your actual sensor reads.

// #Ported: #include <Adafruit_ST7735.h>
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>

// ── Colours (RGB565) ──────────────────────────────────────────────────────────
#define BLACK       0x0000
#define WHITE       0xFFFF
#define CYAN        0x07FF
#define YELLOW      0xFFE0
#define GREEN       0x07E0
#define RED         0xF800
#define DARK_GREY   0x4208

// ── Layout ────────────────────────────────────────────────────────────────────
#define DISP_W      240
#define DISP_H      320
#define LABEL_X     10
#define VALUE_X     130
#define ROW_H       50
#define ROW1_Y      90
#define ROW2_Y      (ROW1_Y + ROW_H)
#define ROW3_Y      (ROW2_Y + ROW_H)
#define ROW4_Y      (ROW3_Y + ROW_H)
#define VALUE_H     18      // clear rect height for value area (textSize 2 = 16px + 2px margin)

// #Ported: Adafruit_ST7735 display(TFT_CS, TFT_DC, TFT_RST);
BleTransport      transport;           // set up bluetooth transport
ESP32PhoneDisplay display(transport);  // create display instance, passing transport reference

// ── Volatile flags — set on NimBLE task (core 0), read on loop task (core 1) ─
// Never call display functions or Serial from a BLE callback — set a flag
// and act on it in loop() instead. This avoids concurrency issues and keeps BLE callbacks fast.
static volatile bool _redrawPending  = false;  // set when the full label frame needs redrawing. Cleared in loop() after handling.
static volatile bool _displayOffline = false;  // set when display is offline (app backgrounded, phone locked, or disconnect). Cleared on connect.
static volatile bool _displayReset   = true;   // set when a new BLE connection requires begin() to re-establish the phone session. Cleared in drawLabelFrame().

static uint32_t _lastUpdate = 0;

// ── Simulated sensor reads — replace with your actual sensor code ─────────────
float readTemperature() { return 22.5f + (float)(millis() % 110) / 100.0f; }
float readHumidity()    { return 55.0f + (float)(millis() % 230) / 100.0f; }
float readPressure()    { return 1013.2f + (float)(millis() % 55) / 10.0f; }
int   readBattery()     { return 85 - (int)(millis() / 60000) % 26; }

// ── Forward declarations ──────────────────────────────────────────────────────
void drawLabelFrame();
void updateValues();

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // ── Register transport callbacks ──────────────────────────────────────────
    // Callbacks are functions that fire automatically when specific events
    // occur on the BLE connection. They run on the BLE task (core 0), so we
    // only set flags here — all drawing and Serial output happens safely in
    // loop() on core 1 based on those flags.

    // onDisplayAvailable — fires when the iPhone display becomes available
    // or unavailable. This is the primary signal for tracking whether we can draw or not.
    //   available=true:  iPhone is ready — start or resume drawing.
    //                    Fires ~100ms after connect and when app returns to foreground.
    //   available=false: iPhone display is offline — stop drawing.
    //                    Fires when app is backgrounded, phone is locked,
    //                    or on a BT disconnect.
    transport.onDisplayAvailable([](bool available) {
        if (available) { _displayOffline = false; _redrawPending = true; }
        else             _displayOffline = true;
    });

    // onRedrawRequest — fires when the iPhone needs the full display rebuilt.
    // Happens when the app returns to the foreground — the screen may be
    // stale from commands missed while backgrounded. Always fires alongside
    // onDisplayAvailable(true) in that case.
    transport.onRedrawRequest([]() {
        _redrawPending = true;
    });

    // onConnected / onDisconnected — fires when the BLE connection is
    // established or lost at the transport level. onConnected serves as a
    // fallback draw trigger if onDisplayAvailable doesn't fire first.
    // onDisconnected ensures _displayOffline is set if the connection drops —
    // on a BT link loss, the display-unavailable signal won't arrive from the app.
    transport.onConnected([]() {
        _displayOffline = false; _redrawPending = true;
    });
    transport.onDisconnected([]() {
        _displayOffline = true;
        _displayReset   = true;   // session state is lost on disconnect — begin() needed on next draw
    });

    transport.begin();
    Serial.println("[BLE] Advertising — waiting for iPhone...");
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    if (_displayOffline) { delay(100); return; }

    // Draw full label frame on connect or when display needs rebuilding
    if (_redrawPending) {
        _redrawPending = false;
        _lastUpdate    = 0;     // force immediate value update after frame draw
        Serial.println("[Display] Drawing label frame");
        Serial.flush();
        drawLabelFrame();
    }

    // Update live values once per second
    uint32_t now = millis();
    if (now - _lastUpdate >= 1000) {
        _lastUpdate = now;
        updateValues();
    }
}

// ── Static layout — drawn on connect and when display needs rebuilding ────────
//
// All drawing calls below are identical to Adafruit_GFX.

void drawLabelFrame()
{
    // begin() and setTitle() only needed when the BLE session is new or reset.
    // On an app switch/return, the session is intact — just redraw the content.
    if (_displayReset) {
        _displayReset = false;
        display.begin(DISP_W, DISP_H);
        display.setTitle("ESP32 Telemetry");
    }

    display.fillScreen(BLACK);  // #Ported: display.fillScreen(ST77XX_BLACK); — identical

    // Title bar
    display.fillRect(0, 0, DISP_W, 70, DARK_GREY);
    display.setCursor(10, 18);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("ESP32 Telemetry");
    display.setCursor(10, 42);
    display.setTextSize(1);
    display.setTextColor(CYAN);
    display.print("Showing \"live\" sensor data"); 

    // Static row labels
    display.setTextSize(2);
    display.setTextColor(CYAN);
    display.setCursor(LABEL_X, ROW1_Y);  display.print("Temp:");
    display.setCursor(LABEL_X, ROW2_Y);  display.print("Humi:");
    display.setCursor(LABEL_X, ROW3_Y);  display.print("Pres:");
    display.setCursor(LABEL_X, ROW4_Y);  display.print("Batt:");

    display.flush();    // #Ported: no equivalent
    Serial.println("[Display] Label frame done");
    Serial.flush();
}

// ── Live values — redrawn every second ───────────────────────────────────────
//
// Only the value column is redrawn. Static labels are untouched.
// fillRect clears the previous value; print() draws the new one.

void updateValues()
{
    float temp = readTemperature();
    float humi = readHumidity();
    float pres = readPressure();
    int   batt = readBattery();

    display.setTextSize(2);

    // Temperature
    display.fillRect(VALUE_X, ROW1_Y, DISP_W - VALUE_X - 5, VALUE_H, BLACK);
    display.setCursor(VALUE_X, ROW1_Y);
    display.setTextColor(temp > 30.0f ? RED : GREEN);
    display.print(temp, 1);
    display.print(" C");

    // Humidity
    display.fillRect(VALUE_X, ROW2_Y, DISP_W - VALUE_X - 5, VALUE_H, BLACK);
    display.setCursor(VALUE_X, ROW2_Y);
    display.setTextColor(YELLOW);
    display.print(humi, 1);
    display.print(" %");

    // Pressure
    display.fillRect(VALUE_X, ROW3_Y, DISP_W - VALUE_X - 5, VALUE_H, BLACK);
    display.setCursor(VALUE_X, ROW3_Y);
    display.setTextColor(WHITE);
    display.print(pres, 1);
    display.print(" hPa");

    // Battery — red below 20%
    display.fillRect(VALUE_X, ROW4_Y, DISP_W - VALUE_X - 5, VALUE_H, BLACK);
    display.setCursor(VALUE_X, ROW4_Y);
    display.setTextColor(batt < 20 ? RED : GREEN);
    display.print(batt);
    display.print(" %");

    display.flush();    // #Ported: no equivalent
}
