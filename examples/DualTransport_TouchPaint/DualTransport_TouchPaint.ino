// DualTransport_TouchPaint — finger painting over BLE or WiFi
//
// Whichever transport the iPhone connects via becomes active automatically.
// Disconnect and reconnect via the other transport to switch seamlessly.
//
// Configure WiFi credentials below. BLE always advertises as "ESP32-Display".
// iPhone app: connect via Bluetooth OR Wi-Fi — your choice.
//
// Demonstrates:
//   - DualTransport: BLE + WiFi simultaneous, automatic switching
//   - RemoteTouchScreen touch input
//   - Colour swatches + CLEAR button
//   - Transport name shown in header

#include <ESP32PhoneDisplay.h>
#include <transport/DualTransport.h>
#include <touch/RemoteTouchScreen.h>

// ── WiFi credentials ──────────────────────────────────────────────────────────
#define WIFI_SSID     "lotz_net1"
#define WIFI_PASSWORD "thelotznetwork1"
// ─────────────────────────────────────────────────────────────────────────────

// ── Colours (RGB565) ──────────────────────────────────────────────────────────
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define DKGREY  0x4208

// ── Display layout ────────────────────────────────────────────────────────────
#define DISP_W    240
#define DISP_H    320
#define SWATCH_H   30   // colour swatch row height
#define SWATCH_W   30   // each swatch width
#define PENRADIUS   3

// ── Objects ───────────────────────────────────────────────────────────────────
DualTransport     transport(WIFI_SSID, WIFI_PASSWORD, "esp32-display");
ESP32PhoneDisplay display(transport);
RemoteTouchScreen ts(transport);

// ── State ─────────────────────────────────────────────────────────────────────
uint16_t          currentColor = RED;
volatile bool     drawPending  = false;
int16_t           _lastX       = -1;
int16_t           _lastY       = -1;

// ── UI ───────────────────────────────────────────────────────────────────────

void drawUI()
{
    display.begin(DISP_W, DISP_H);
    display.clear(BLACK);

    // Header — shows active transport
    display.fillRect(0, 0, DISP_W, 24, DKGREY);
    display.setCursor(4, 7);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.print("ESP32PhoneDisplay  via: ");
    display.print(transport.activeTransportName());

    // Colour swatches
    uint16_t colors[] = { RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA, WHITE };
    for (int i = 0; i < 7; i++)
        display.fillRect(i * SWATCH_W, DISP_H - SWATCH_H,
                         SWATCH_W, SWATCH_H, colors[i]);

    // CLEAR button
    display.fillRect(7 * SWATCH_W, DISP_H - SWATCH_H,
                     DISP_W - 7 * SWATCH_W, SWATCH_H, WHITE);
    display.setCursor(7 * SWATCH_W + 4, DISP_H - SWATCH_H + 10);
    display.setTextColor(BLACK);
    display.print("CLR");

    // Selection indicator
    display.drawRect(0, DISP_H - SWATCH_H - 4, SWATCH_W, 4, currentColor);

    display.flush();
}

void selectColor(int16_t x)
{
    uint16_t colors[] = { RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA, WHITE };
    int idx = x / SWATCH_W;
    if (idx < 7) {
        currentColor = colors[idx];
        // Update selection indicator
        display.fillRect(0, DISP_H - SWATCH_H - 4, DISP_W, 4, BLACK);
        display.drawRect(idx * SWATCH_W, DISP_H - SWATCH_H - 4,
                         SWATCH_W, 4, currentColor);
        display.flush();
    } else {
        // CLEAR
        drawUI();
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    transport.setSoftAP("ESP32-Display", "display123");
    transport.setPowerSave(false);   // shut down BLE on WiFi connect for smooth touch

    transport.onConnected([]() {
        Serial.printf("[App] Connected via %s\n",
                      transport.activeTransportName());
        _lastX    = -1;
        _lastY    = -1;
        ts.begin(TOUCH_MODE_RESISTIVE, 16);   // send TOUCH_BEGIN on every connect
        drawPending = true;
    });

    transport.onDisconnected([]() {
        Serial.println("[App] Disconnected");
    });

    transport.begin();

    Serial.println("[App] Ready — connect via BLE or WiFi");
}

// ── Loop ─────────────────────────────────────────────────────────────────────

void loop()
{
    if (drawPending && transport.canSend()) {
        drawPending = false;
        drawUI();
        Serial.println("[App] UI drawn");
    }

    if (!transport.canSend()) {
        delay(20);
        return;
    }

    TSPoint p = ts.getPoint();

    // if touch is released — reset last position so next touch is always fresh
    if (p.z <= RemoteTouchScreen::MINPRESSURE) {
        _lastX = -1;   // reset on touch up so next touch is always fresh
        _lastY = -1;
        return;
    }

    // Only act if position changed — prevents redundant draws and
    // selectColor() being called repeatedly while finger is held down
    if (p.x == _lastX && p.y == _lastY) return;
    _lastX = p.x;
    _lastY = p.y;

    // Bottom swatch/clear row
    if (p.y >= DISP_H - SWATCH_H) {
        selectColor(p.x);
        return;
    }

    // Drawing area (below header, above swatches)
    if (p.y > 24) {
        display.fillCircle(p.x, p.y, PENRADIUS, currentColor);
        display.flush();
    }
}
