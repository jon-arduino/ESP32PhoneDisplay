// BLE_HelloWorld — ESP32PhoneDisplay BLE example
//
// Demonstrates:
//   - Initial draw on connect
//   - Automatic redraw on reconnect (disconnect and reconnect iPhone app)
//   - T1/T2 back-channel key events shown as a banner
//
// 1. Flash this sketch to your ESP32
// 2. Open the ESP32PhoneDisplay iPhone app
// 3. Tap "Bluetooth" and connect to "ESP32-Display"

#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>

// Colour helpers (RGB565)
#define BLACK       0x0000
#define WHITE       0xFFFF
#define RED         0xF800
#define GREEN       0x07E0
#define BLUE        0x001F
#define YELLOW      0xFFE0
#define DARK_GREY   0x4208

#define DISP_W  240
#define DISP_H  320

BleTransport        transport;
ESP32PhoneDisplay   display(transport);

// ── Flags set from callbacks, consumed in loop() ─────────────────────────────
// Callbacks fire on the NimBLE task — never call display functions directly
// from a callback, only set flags here and act on them in loop().
static volatile bool    drawPending = false;  // set by onSubscribed
static volatile uint8_t keyPending  = 0;      // set by onKey

// ── Drawing functions ─────────────────────────────────────────────────────────

void drawScreen()
{
    display.begin(DISP_W, DISP_H);
    display.clear(BLACK);

    // Header bar
    display.fillRect(0, 0, DISP_W, 50, BLUE);
    display.setCursor(20, 15);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("Hello iPhone!");

    // Info box
    display.drawRoundRect(20, 70, DISP_W - 40, 80, 8, GREEN);
    display.setCursor(35, 100);
    display.setTextColor(GREEN);
    display.setTextSize(1);
    display.print("ESP32PhoneDisplay");
    display.setCursor(35, 115);
    display.print("BLE transport active");

    // Shapes
    display.fillCircle(80,  230, 40, RED);
    display.fillCircle(160, 230, 40, BLUE);
    display.drawCircle(120, 230, 50, YELLOW);

    // Key hint
    display.setCursor(30, 295);
    display.setTextColor(DARK_GREY);
    display.setTextSize(1);
    display.print("Press T1 or T2 on the app");

    display.flush();
    Serial.println("[App] Screen drawn");
}

void drawKeyBanner(uint8_t key)
{
    display.fillRect(0, 280, DISP_W, 40, (key == '1') ? RED : BLUE);
    display.setCursor(60, 293);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("T");
    display.print((char)key);
    display.print(" pressed");
    display.flush();
    Serial.printf("[App] Key %c pressed\n", key);
}

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // onSubscribed fires when iPhone subscribes to the BLE TX characteristic.
    // ready=true  → connected and ready to receive — safe to draw.
    // ready=false → unsubscribed (disconnecting) — stop drawing.
    // Fires on NimBLE task: set flag only, draw in loop().
    transport.onSubscribed([](bool ready) {
        if (ready) {
            Serial.println("[BLE] Subscribed — queuing draw");
            drawPending = true;
        } else {
            Serial.println("[BLE] Unsubscribed");
            drawPending = false;
        }
    });

    // onKey fires when T1 ('1') or T2 ('2') is pressed on the iPhone app.
    // Fires on NimBLE task: set flag only, handle in loop().
    transport.onKey([](uint8_t key) {
        keyPending = key;
    });

    transport.begin();
    Serial.println("[BLE] Advertising as \"ESP32-Display\" — waiting for iPhone...");
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    // Redraw on initial connect or any reconnect
    if (drawPending) {
        drawPending = false;
        drawScreen();
    }

    // Handle key press from iPhone
    if (keyPending) {
        uint8_t key = keyPending;
        keyPending  = 0;
        if (transport.canSend()) {
            drawKeyBanner(key);
        }
    }

    delay(20);
}

