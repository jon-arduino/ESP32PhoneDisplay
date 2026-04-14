// BLE_TouchButtons — Adafruit_GFX_Button compatible touch demo
//
// Shows three buttons on the iPhone display. Tap a button to
// activate it. Demonstrates the standard Adafruit_GFX_Button
// pattern that works unchanged with RemoteTouchScreen.
//
// Uses ESP32PhoneDisplay_Compat (Adafruit_GFX subclass) because
// Adafruit_GFX_Button::initButtonUL() requires an Adafruit_GFX*
// pointer. For display-only sketches without GFX_Button, the
// native ESP32PhoneDisplay class is more efficient.

#include <ESP32PhoneDisplay_Compat.h>
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>
#include <Adafruit_GFX.h>   // for Adafruit_GFX_Button

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
// initButtonUL(gfx, x, y, w, h, outline, fill, textcolor, label, textsize)
// x,y is the UPPER-LEFT corner of the button
Adafruit_GFX_Button btnRed, btnGreen, btnBlue;

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

void setup()
{
    Serial.begin(115200);
    transport.begin();

    Serial.println("Waiting for iPhone...");
    while (!tft.isConnected()) { delay(100); }

    tft.begin();
    tft.fillScreen(BLACK);

    // Title
    tft.setCursor(20, 20);
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.print("Touch Buttons");

    // Init buttons — tft is Adafruit_GFX* compatible
    btnRed.initButtonUL(&tft,
                        20, 100,           // x, y upper-left
                        180, 50,           // w, h
                        WHITE, RED, WHITE,
                        (char*)"RED", 2);

    btnGreen.initButtonUL(&tft,
                          20, 170,
                          180, 50,
                          WHITE, DKGREEN, WHITE,
                          (char*)"GREEN", 2);

    btnBlue.initButtonUL(&tft,
                         20, 240,
                         180, 50,
                         WHITE, BLUE, WHITE,
                         (char*)"BLUE", 2);

    drawButtons(false, false, false);
    updateStatus("Tap a button", GREY);

    // Start touch — 50ms throttle is fine for buttons
    ts.begin();

    Serial.println("Ready!");
}

void loop()
{
    TSPoint p = ts.getPoint();
    bool touching = (p.z > RemoteTouchScreen::MINPRESSURE);

    // Feed touch state to each button
    btnRed.press(touching   && btnRed.contains(p.x, p.y));
    btnGreen.press(touching && btnGreen.contains(p.x, p.y));
    btnBlue.press(touching  && btnBlue.contains(p.x, p.y));

    // React to button events
    if (btnRed.justPressed()) {
        drawButtons(true, false, false);
        updateStatus("RED selected", RED);
        Serial.println("RED");
    }
    if (btnGreen.justPressed()) {
        drawButtons(false, true, false);
        updateStatus("GREEN selected", GREEN);
        Serial.println("GREEN");
    }
    if (btnBlue.justPressed()) {
        drawButtons(false, false, true);
        updateStatus("BLUE selected", BLUE);
        Serial.println("BLUE");
    }
}
