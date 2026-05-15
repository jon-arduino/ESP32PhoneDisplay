// BLE_Telemetry — live sensor data display over BLE
//
// Demonstrates an efficient partial-redraw pattern for live data:
//
//   drawLabelFrame() — draws the static layout once on connect.
//     Title bar, row labels ("Temperature:", "Humidity:", etc.) never change
//     and are not redrawn on each update.
//
//   updateValues() — called every second, redraws only the value column.
//     Each update clears a small fillRect next to each label and reprints
//     the value. The static label areas are untouched.
//
// This keeps per-update BLE traffic very low — 4 × (fillRect + setCursor +
// setTextColor + 2× print) + flush — regardless of display size.
//
// Porting from Adafruit_GFX:
//   All drawing calls are identical. Only setup and connection handling change.
//   Lines that differ are marked // #Ported: with the original shown.
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
BleTransport      transport;
ESP32PhoneDisplay display(transport);

// ── Volatile flags — set on NimBLE task (core 0), read on loop task (core 1) ─
// Never call display functions from a BLE callback — set flags and act in loop().
static volatile bool _drawPending = false;
static volatile bool _paused      = false;

static uint32_t _lastUpdate = 0;

// ── Simulated sensor reads — replace with your actual sensor code ─────────────
float readTemperature() { return 22.5f + (float)(millis() % 100) / 100.0f; }
float readHumidity()    { return 55.0f + (float)(millis() % 200) / 100.0f; }
float readPressure()    { return 1013.2f + (float)(millis() % 50) / 10.0f; }
int   readBattery()     { return 85 - (int)(millis() / 60000) % 20; }

// ── Forward declarations ──────────────────────────────────────────────────────
void drawLabelFrame();
void updateValues();

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // onRedrawRequest — sent by the iPhone app ~100ms after connect/reconnect.
    // Primary trigger for rebuilding the screen.
    // #Ported: no equivalent — local display is always ready.
    transport.onRedrawRequest([]() {
        _paused      = false;
        _drawPending = true;
    });

    // onSubscribed — fallback for older app versions; handles disconnect too.
    // #Ported: no equivalent.
    transport.onSubscribed([](bool ready) {
        if (ready) { _paused = false; _drawPending = true; }
        else        { _paused = true; }
    });

    // #Ported: display.initR(INITR_BLACKTAB);
    transport.begin();
    Serial.println("[BLE] Advertising — waiting for iPhone...");
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    if (_paused) { delay(100); return; }

    // Draw static layout on connect or reconnect
    if (_drawPending) {
        _drawPending = false;
        _lastUpdate  = 0;       // force immediate value update after layout draw
        drawLabelFrame();
    }

    // Update live values once per second
    uint32_t now = millis();
    if (now - _lastUpdate >= 1000) {
        _lastUpdate = now;
        updateValues();
    }
}

// ── Static layout — drawn once on connect ────────────────────────────────────
//
// All drawing calls below are identical to Adafruit_GFX.

void drawLabelFrame()
{
    // begin() establishes the phone session. Re-call on every reconnect.
    // setTitle re-sent here since phone may have lost session state.
    // #Ported: display.initR(INITR_BLACKTAB); — local init is one-time only.
    display.begin(DISP_W, DISP_H);
    display.setTitle("ESP32 Telemetry");

    display.clear(BLACK);       // #Ported: display.fillScreen(ST77XX_BLACK);

    // Title bar
    display.fillRect(0, 0, DISP_W, 70, DARK_GREY);
    display.setCursor(10, 18);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("ESP32 Telemetry");
    display.setCursor(10, 42);
    display.setTextSize(1);
    display.setTextColor(CYAN);
    display.print("Live sensor data — 1s update");

    // Static row labels
    display.setTextSize(2);
    display.setTextColor(CYAN);
    display.setCursor(LABEL_X, ROW1_Y);  display.print("Temp:");
    display.setCursor(LABEL_X, ROW2_Y);  display.print("Humi:");
    display.setCursor(LABEL_X, ROW3_Y);  display.print("Pres:");
    display.setCursor(LABEL_X, ROW4_Y);  display.print("Batt:");

    display.flush();    // #Ported: no equivalent
    Serial.println("[Display] Label frame drawn");
}

// ── Live values — redrawn every second ───────────────────────────────────────
//
// Only the value column is redrawn. Static labels are untouched.
// fillRect clears the previous value; print() draws the new one.
// Each call sends one GFX_CMD_FILL_RECT + one GFX_CMD_WRITE_CHAR per character.

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
