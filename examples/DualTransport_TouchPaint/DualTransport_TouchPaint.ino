// DualTransport_TouchPaint — finger painting over BLE or WiFi
//
// The iPhone app connects via whichever transport it chooses — BLE or WiFi.
// Switching transport is seamless: disconnect from one, connect via the other.
// The active transport name is shown in the header so you can see it switch.
//
// WiFi setup:
//   Option A — connect to your existing network (set WIFI_SSID/PASSWORD below)
//   Option B — connect directly to the ESP32 access point "ESP32-Display"
//              using password set in SOFTAP_PASSWORD (no router needed)
//
// Demonstrates:
//   - DualTransport: BLE + WiFi simultaneous, automatic switching
//   - activeTransportName() — which transport is active
//   - `setPowerSave(false)` — note: NOT used here so BLE stays active for
//     bidirectional transport switching. Removing it means WiFi and BLE
//     share the 2.4GHz antenna but both remain available.
//   - setSoftAP() — direct iPhone→ESP32 connection without a router
//   - Touch queue draining + batched flush — same low-latency pattern as
//     BLE_TouchPaint2, works equally well over BLE or WiFi
//   - Canvas persistence — iPhone framebuffer survives app switching and
//     brief transport drops; only a full reconnect clears the canvas

#include <ESP32PhoneDisplay.h>
#include <transport/DualTransport.h>
#include <touch/RemoteTouchScreen.h>

// ── WiFi credentials ──────────────────────────────────────────────────────────
#define WIFI_SSID       "YOUR_SSID"
#define WIFI_PASSWORD   "YOUR_PASSWORD"
#define SOFTAP_PASSWORD "YOUR_AP_PASSWORD"  // iPhone connects to "ESP32-Display" AP
// ─────────────────────────────────────────────────────────────────────────────

// ── Colours (RGB565) ──────────────────────────────────────────────────────────
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define YELLOW  0xFFE0
#define GREEN   0x07E0
#define CYAN    0x07FF
#define BLUE    0x001F
#define MAGENTA 0xF81F
#define DKGREY  0x4208

// ── Layout ────────────────────────────────────────────────────────────────────
#define DISP_W       240
#define DISP_H       320
#define HEADER_H      24
#define SWATCH_H      30
#define SWATCH_W      30
#define NUM_SWATCHES   7
#define PENRADIUS      3

const uint16_t SWATCHES[] = { RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA, WHITE };

// ── Objects ───────────────────────────────────────────────────────────────────
DualTransport     transport(WIFI_SSID, WIFI_PASSWORD, "esp32-display");
ESP32PhoneDisplay display(transport);
RemoteTouchScreen ts(transport);

// ── State ─────────────────────────────────────────────────────────────────────
uint16_t currentColor = RED;
int16_t  _lastX       = -1;
int16_t  _lastY       = -1;

// ── Volatile flags — set in callbacks (core 0), read in loop() (core 1) ───────
static volatile bool _drawPending   = false;  // full redraw — new connection
static volatile bool _chromePending = false;  // chrome only — app foreground return
static volatile bool _displayOffline = false; // pause drawing when display unavailable

// ── Forward declarations ──────────────────────────────────────────────────────
void drawUI();
void drawChrome();
void updateSelectionIndicator();
void selectColor(int16_t x);

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // Direct iPhone→ESP32 WiFi connection — no router needed.
    // iPhone connects to "ESP32-Display" AP using SOFTAP_PASSWORD.
    transport.setSoftAP("ESP32-Display", SOFTAP_PASSWORD);

    // Request 15ms BLE connection interval — essential for responsive touch.
    transport.setConnectionInterval(15, 15);

    // ── Register transport callbacks ──────────────────────────────────────────
    // Callbacks fire on transport tasks — only set flags here.

    // onDisplayAvailable — primary signal for display ready/offline.
    // Fires on both BLE and WiFi transport events.
    transport.onDisplayAvailable([](bool available) {
        if (available) { _displayOffline = false; _drawPending = true; }
        else             _displayOffline = true;
    });

    // onRedrawRequest — app returned from background, redraw chrome.
    // Canvas persists on iPhone framebuffer — no need to clear it.
    transport.onRedrawRequest([]() { _chromePending = true; });

    // onConnected/onDisconnected — transport-level fallback.
    transport.onConnected([]() {
        _displayOffline = false;
        _drawPending    = true;
        _lastX = -1;
        _lastY = -1;
    });
    transport.onDisconnected([]() { _displayOffline = true; });

    transport.begin();
    Serial.println("[App] Ready — connect via BLE or WiFi");
    Serial.println("[App] BLE: look for 'ESP32-Display' in Bluetooth");
    Serial.printf ("[App] WiFi: join '%s' or AP 'ESP32-Display'\n", WIFI_SSID);
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    if (_displayOffline) { delay(20); return; }

    // Full redraw — new connection or transport switch
    if (_drawPending && transport.canSend()) {
        _drawPending   = false;
        _chromePending = false;
        Serial.printf("[App] Connected via %s\n", transport.activeTransportName());
        Serial.flush();
        drawUI();
        return;
    }

    // Chrome-only redraw — app returned from background, canvas preserved
    if (_chromePending && transport.canSend()) {
        _chromePending = false;
        drawChrome();
        return;
    }

    if (!transport.canSend()) { delay(20); return; }

    // ── Drain entire touch queue ──────────────────────────────────────────────
    // All touch points accumulated since last loop() drawn in one batch.
    // Single flush after drain = efficient over both BLE and WiFi.
    // Works identically regardless of which transport is active.
    bool drew = false;

    while (ts.available()) {
        TSPoint p = ts.getQueuedPoint();

        if (p.z <= RemoteTouchScreen::MINPRESSURE) {
            _lastX = -1; _lastY = -1;
            continue;
        }

        if (p.x == _lastX && p.y == _lastY) continue;
        _lastX = p.x; _lastY = p.y;

        if (p.y >= DISP_H - SWATCH_H) { selectColor(p.x); continue; }
        if (p.y > HEADER_H)           { display.fillCircle(p.x, p.y, PENRADIUS, currentColor); drew = true; }
    }

    // Check overflow once after draining — not inside the hot loop
    if (ts.queueOverflows() > 0) {
        Serial.printf("[Touch] queue overflow: %u\n", ts.queueOverflows());
        Serial.flush();
        ts.resetOverflows();
    }

    if (drew) display.flush();
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawUI()
{
    // Full session init + clear canvas — called on every new connection.
    display.begin(DISP_W, DISP_H);
    display.setTitle("Dual Paint");
    display.fillScreen(BLACK);
    drawChrome();

    // ts.begin() re-registers touch on every new connection
    ts.begin(TOUCH_MODE_RESISTIVE, 16);
}

void drawChrome()
{
    // Redraws chrome (header + swatches) without touching the canvas.
    // Called on full UI draw and when app returns from background.

    // Header — shows active transport so you can see BLE vs WiFi switch
    display.fillRect(0, 0, DISP_W, HEADER_H, DKGREY);
    display.setCursor(4, 7);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.print("Touch Paint  via: ");
    display.print(transport.activeTransportName());

    // Colour swatches
    for (int i = 0; i < NUM_SWATCHES; i++)
        display.fillRect(i * SWATCH_W, DISP_H - SWATCH_H,
                         SWATCH_W, SWATCH_H, SWATCHES[i]);

    // CLEAR button
    display.fillRect(NUM_SWATCHES * SWATCH_W, DISP_H - SWATCH_H,
                     DISP_W - NUM_SWATCHES * SWATCH_W, SWATCH_H, WHITE);
    display.setCursor(NUM_SWATCHES * SWATCH_W + 4, DISP_H - SWATCH_H + 10);
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.print("CLR");

    updateSelectionIndicator();
    display.flush();
}

void updateSelectionIndicator()
{
    display.fillRect(0, DISP_H - SWATCH_H - 4, DISP_W, 4, BLACK);
    for (int i = 0; i < NUM_SWATCHES; i++) {
        if (SWATCHES[i] == currentColor) {
            display.drawRect(i * SWATCH_W, DISP_H - SWATCH_H - 4,
                             SWATCH_W, 4, currentColor);
            break;
        }
    }
}

void selectColor(int16_t x)
{
    int idx = x / SWATCH_W;
    if (idx < NUM_SWATCHES) {
        currentColor = SWATCHES[idx];
        updateSelectionIndicator();
        display.flush();
    } else {
        // CLEAR — erase canvas only, session intact so no begin() needed
        display.fillRect(0, HEADER_H, DISP_W, DISP_H - HEADER_H - SWATCH_H, BLACK);
        updateSelectionIndicator();
        display.flush();
    }
}
