// BLE_TouchButtons — Adafruit_GFX_Button compatible touch demo
//
// Shows three buttons on the iPhone display. Tap a button to
// activate it. Demonstrates the standard Adafruit_GFX_Button
// pattern that works unchanged with RemoteTouchScreen.
//
// Also demonstrates:
//   - setTitle() — nav bar title
//   - setButton1/2() — toolbar buttons (T1/T2)
//   - onRedrawRequest() — clean reconnect behavior
//
// Uses ESP32PhoneDisplay_Compat (Adafruit_GFX subclass) because
// Adafruit_GFX_Button::initButtonUL() requires an Adafruit_GFX*
// pointer.

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

// ── Display ───────────────────────────────────────────────────────────────────
#define DISP_W  240
#define DISP_H  320

BleTransport             transport;
ESP32PhoneDisplay_Compat tft(transport, DISP_W, DISP_H);
RemoteTouchScreen        ts(transport);

// ── Buttons ───────────────────────────────────────────────────────────────────
Adafruit_GFX_Button btnRed, btnGreen, btnBlue;

static volatile bool _drawPending = false;

// ── Forward declarations ──────────────────────────────────────────────────────
void initDisplay();
void drawButtons(bool redActive, bool greenActive, bool blueActive);
void updateStatus(const char *msg, uint16_t color);

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // Set nav bar title and toolbar buttons
    // Called before begin() — sent to phone after connection
    transport.onSubscribed([](bool ready) {
        if (ready)  _drawPending = true;
    });

    transport.onRedrawRequest([]() {
        _drawPending = true;
    });

    transport.onKey([](uint8_t key) {
        // T1/T2 key events from toolbar buttons
        if      (key == '1') Serial.println("[Key] T1 pressed");
        else if (key == '2') Serial.println("[Key] T2 pressed");
    });

    transport.begin();
    Serial.println("[BLE] Waiting for iPhone...");

    while (!_drawPending) delay(100);
    _drawPending = false;

    initDisplay();
}

// ── loop() ────────────────────────────────────────────────────────────────────

void loop()
{
    // Handle reconnect redraw
    if (_drawPending) {
        _drawPending = false;
        initDisplay();
        return;
    }

    TSPoint p = ts.getPoint();
    bool touching = (p.z > RemoteTouchScreen::MINPRESSURE);

    btnRed.press(touching   && btnRed.contains(p.x, p.y));
    btnGreen.press(touching && btnGreen.contains(p.x, p.y));
    btnBlue.press(touching  && btnBlue.contains(p.x, p.y));

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
    tft.begin();

    // Nav bar title and toolbar buttons
    tft.setTitle("Touch Buttons");
    tft.setButton1("T1");    // sends BC_CMD_KEY1 when pressed
    tft.setButton2("T2");    // sends BC_CMD_KEY2 when pressed

    tft.fillScreen(BLACK);

    // Screen title
    tft.setCursor(20, 20);
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.print("Touch Buttons");

    // Log what the phone received
    Serial.println("[Display] begin + setTitle + setButton1/2 sent");

    // Init GFX buttons
    btnRed.initButtonUL(&tft,
                        20, 100, 180, 50,
                        WHITE, RED, WHITE,
                        (char*)"RED", 2);

    btnGreen.initButtonUL(&tft,
                          20, 170, 180, 50,
                          WHITE, DKGREEN, WHITE,
                          (char*)"GREEN", 2);

    btnBlue.initButtonUL(&tft,
                         20, 240, 180, 50,
                         WHITE, BLUE, WHITE,
                         (char*)"BLUE", 2);

    drawButtons(false, false, false);
    updateStatus("Tap a button", GREY);

    ts.begin();

    Serial.println("[Display] Ready");
}

void drawButtons(bool redActive, bool greenActive, bool blueActive)
{
    btnRed.drawButton(redActive);
    btnGreen.drawButton(greenActive);
    btnBlue.drawButton(blueActive);
    tft.flush();
}

void updateStatus(const char *msg, uint16_t color)
{
    tft.fillRect(0, DISP_H - 50, DISP_W, 50, BLACK);
    tft.setCursor(10, DISP_H - 35);
    tft.setTextColor(color);
    tft.setTextSize(2);
    tft.print(msg);
    tft.flush();
}
