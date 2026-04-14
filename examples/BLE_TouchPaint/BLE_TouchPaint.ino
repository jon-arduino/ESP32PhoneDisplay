// BLE_TouchPaint — finger painting demo using RemoteTouchScreen
//
// Draw on the iPhone screen with your finger. Tap the color
// swatches at the top to change color. Tap CLEAR to erase.
//
// Demonstrates:
//   - RemoteTouchScreen with BLE transport
//   - Adafruit_GFX_Button compatible touch handling
//   - getPoint() polling pattern identical to Adafruit_TouchScreen
//
// Ported from the Adafruit CapTouchPaint example with minimal changes:
//   - Replace Adafruit_FT6206 with RemoteTouchScreen
//   - Remove ts.begin() pin arguments
//   - No map() or calibration needed

#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>

// ── Colour palette (RGB565) ───────────────────────────────────────────────────
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define ORANGE  0xFC00

// ── Display layout ────────────────────────────────────────────────────────────
#define DISP_W      240
#define DISP_H      320
#define BOXSIZE      40   // colour swatch size
#define PENRADIUS     3   // drawing dot radius
#define CLEAR_X     200   // CLEAR button x centre
#define CLEAR_Y      20   // CLEAR button y centre
#define CLEAR_W      60
#define CLEAR_H      30

// ── Transport and display ─────────────────────────────────────────────────────
BleTransport      transport;
ESP32PhoneDisplay display(transport);
RemoteTouchScreen ts(transport);

uint16_t currentColor = RED;

// ── Draw the UI chrome ────────────────────────────────────────────────────────
void drawUI()
{
    display.clear(BLACK);

    // Colour swatches
    display.fillRect(0,           0, BOXSIZE, BOXSIZE, RED);
    display.fillRect(BOXSIZE,     0, BOXSIZE, BOXSIZE, YELLOW);
    display.fillRect(BOXSIZE * 2, 0, BOXSIZE, BOXSIZE, GREEN);
    display.fillRect(BOXSIZE * 3, 0, BOXSIZE, BOXSIZE, CYAN);
    display.fillRect(BOXSIZE * 4, 0, BOXSIZE, BOXSIZE, BLUE);
    display.fillRect(BOXSIZE * 5, 0, BOXSIZE, BOXSIZE, MAGENTA);

    // Outline the selected colour
    display.drawRect(0, 0, BOXSIZE, BOXSIZE, WHITE);

    // CLEAR button
    display.fillRoundRect(CLEAR_X - CLEAR_W / 2, CLEAR_Y - CLEAR_H / 2,
                          CLEAR_W, CLEAR_H, 6, WHITE);
    display.setCursor(CLEAR_X - 20, CLEAR_Y - 6);
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.print("CLEAR");

    display.flush();
}

void setup()
{
    Serial.begin(115200);
    transport.begin();

    Serial.println("Waiting for iPhone...");
    while (!transport.canSend()) { delay(100); }

    display.begin(DISP_W, DISP_H);
    drawUI();

    // Start touch with 16ms move throttle for smooth drawing
    ts.begin(TOUCH_MODE_RESISTIVE, 16);

    Serial.println("Ready — draw on the iPhone screen!");
}

void loop()
{
    TSPoint p = ts.getPoint();

    if (p.z <= RemoteTouchScreen::MINPRESSURE) return;  // no touch

    // ── Colour swatch row (top BOXSIZE pixels) ────────────────────────────────
    if (p.y < BOXSIZE) {

        // Remove old selection outline
        if (currentColor == RED)     display.drawRect(0,           0, BOXSIZE, BOXSIZE, RED);
        if (currentColor == YELLOW)  display.drawRect(BOXSIZE,     0, BOXSIZE, BOXSIZE, YELLOW);
        if (currentColor == GREEN)   display.drawRect(BOXSIZE * 2, 0, BOXSIZE, BOXSIZE, GREEN);
        if (currentColor == CYAN)    display.drawRect(BOXSIZE * 3, 0, BOXSIZE, BOXSIZE, CYAN);
        if (currentColor == BLUE)    display.drawRect(BOXSIZE * 4, 0, BOXSIZE, BOXSIZE, BLUE);
        if (currentColor == MAGENTA) display.drawRect(BOXSIZE * 5, 0, BOXSIZE, BOXSIZE, MAGENTA);

        // Pick new colour
        if      (p.x < BOXSIZE)         currentColor = RED;
        else if (p.x < BOXSIZE * 2)     currentColor = YELLOW;
        else if (p.x < BOXSIZE * 3)     currentColor = GREEN;
        else if (p.x < BOXSIZE * 4)     currentColor = CYAN;
        else if (p.x < BOXSIZE * 5)     currentColor = BLUE;
        else                            currentColor = MAGENTA;

        // Draw new selection outline
        if (currentColor == RED)     display.drawRect(0,           0, BOXSIZE, BOXSIZE, WHITE);
        if (currentColor == YELLOW)  display.drawRect(BOXSIZE,     0, BOXSIZE, BOXSIZE, WHITE);
        if (currentColor == GREEN)   display.drawRect(BOXSIZE * 2, 0, BOXSIZE, BOXSIZE, WHITE);
        if (currentColor == CYAN)    display.drawRect(BOXSIZE * 3, 0, BOXSIZE, BOXSIZE, WHITE);
        if (currentColor == BLUE)    display.drawRect(BOXSIZE * 4, 0, BOXSIZE, BOXSIZE, WHITE);
        if (currentColor == MAGENTA) display.drawRect(BOXSIZE * 5, 0, BOXSIZE, BOXSIZE, WHITE);

        display.flush();
        return;
    }

    // ── CLEAR button ──────────────────────────────────────────────────────────
    if (p.x > CLEAR_X - CLEAR_W / 2 && p.x < CLEAR_X + CLEAR_W / 2 &&
        p.y > CLEAR_Y - CLEAR_H / 2 && p.y < CLEAR_Y + CLEAR_H / 2) {
        drawUI();
        return;
    }

    // ── Drawing area ──────────────────────────────────────────────────────────
    display.fillCircle(p.x, p.y, PENRADIUS, currentColor);
    display.flush();
}
