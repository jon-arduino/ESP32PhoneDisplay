// WiFi_HelloWorld — minimal ESP32PhoneDisplay example over WiFi
//
// The WiFi equivalent of BLE_HelloWorld — identical except for the transport.
// Connection handling, callbacks, session management, and drawing are the same.
// Reading both examples side by side shows exactly what differs between transports.
//
// WiFi setup:
//   Option A — connect to your existing network (set WIFI_SSID/PASSWORD below)
//   Option B — connect directly to the ESP32 access point "Hello_ESP32-AP"
//              using SOFTAP_PASSWORD (no router needed)
//
// The device advertises on mDNS as "Hello_ESP32.local" — set via setDeviceName().
// Change to a unique name for multi-device deployments.
// See docs/transport.md for device naming guidance.
//
// Porting from Adafruit_GFX: all drawing calls are identical. Only setup
// and connection handling change — see #Ported comments throughout and
// docs/porting.md for the full porting guide.

#include <ESP32PhoneDisplay.h>
#include <transport/WiFiTransport.h>

// ── WiFi credentials ──────────────────────────────────────────────────────────
#define WIFI_SSID       "YOUR_SSID"
#define WIFI_PASSWORD   "YOUR_PASSWORD"
#define SOFTAP_PASSWORD "YOUR_AP_PASSWORD"
// ─────────────────────────────────────────────────────────────────────────────

// ── Colours (RGB565) ──────────────────────────────────────────────────────────
#define BLACK       0x0000
#define WHITE       0xFFFF
#define RED         0xF800
#define GREEN       0x07E0
#define BLUE        0x001F
#define YELLOW      0xFFE0
#define LIGHT_GREY  0xC618

// ── Display dimensions ────────────────────────────────────────────────────────
#define DISP_W  240
#define DISP_H  320

// #Ported: Adafruit_ST7735 display(TFT_CS, TFT_DC, TFT_RST);
WiFiTransport     transport(WIFI_SSID, WIFI_PASSWORD);
ESP32PhoneDisplay display(transport);

// ── Volatile flags — set in transport callbacks, read in loop() ───────────────
// Never call display functions from a callback — set flags and act in loop().
static volatile bool _redrawPending  = false;  // set when screen needs redrawing. Cleared in loop() after handling.
static volatile bool _displayOffline = false;  // set when display is offline. Cleared on connect.
static volatile bool _displayReset   = true;   // set when session state is lost — begin() needed on next draw.

// ── Forward declarations ──────────────────────────────────────────────────────
void drawScreen();

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // Custom device name — appears in the iPhone app's WiFi device list
    // and as the mDNS hostname (Hello_ESP32.local).
    // Change to a unique name for multi-device deployments.
    transport.setDeviceName("Hello_ESP32");

    // SoftAP — ESP32 creates its own WiFi network so no router is needed.
    // iPhone connects to "Hello_ESP32-AP" using SOFTAP_PASSWORD.
    transport.setSoftAP("Hello_ESP32-AP", SOFTAP_PASSWORD);

    // ── Register transport callbacks ──────────────────────────────────────────
    // Callbacks fire on transport tasks — only set flags here.

    // onDisplayAvailable — primary signal for display ready/offline.
    //   available=true:  iPhone is ready — start or resume drawing.
    //                    Fires ~100ms after connect and when app returns to foreground.
    //   available=false: iPhone display is offline — stop drawing.
    //                    Fires when app is backgrounded, phone is locked, or on disconnect.
    transport.onDisplayAvailable([](bool available) {
        if (available) { _displayOffline = false; _redrawPending = true; }
        else             _displayOffline = true;
    });

    // onRedrawRequest — fires when the iPhone needs the full display rebuilt.
    // Happens when the app returns to foreground — screen may be stale.
    transport.onRedrawRequest([]() { _redrawPending = true; });

    // onConnected / onDisconnected — TCP connection established or lost.
    // onConnected serves as fallback draw trigger if onDisplayAvailable doesn't fire.
    // onDisconnected sets _displayReset so begin() is re-sent on next connection.
    transport.onConnected([]() {
        _displayOffline = false; _redrawPending = true;
    });
    transport.onDisconnected([]() {
        _displayOffline = true;
        _displayReset   = true;   // session lost — begin() needed on next draw
    });

    // #Ported: display.initR(INITR_BLACKTAB); — replaced by transport setup
    transport.begin();
    Serial.println("[WiFi] Starting — connect via WiFi or AP 'Hello_ESP32-AP'");
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    if (_displayOffline) { delay(100); return; }

    if (_redrawPending) {
        _redrawPending = false;
        Serial.println("[Display] Drawing screen");
        Serial.flush();
        drawScreen();
    }

    delay(20);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawScreen()
{
    if (_displayReset) {
        _displayReset = false;
        display.begin(DISP_W, DISP_H);
        display.setTitle("Hello World");
    }

    // All drawing calls below are identical to Adafruit_GFX — no changes needed.
    display.fillScreen(BLACK);      // #Ported: display.fillScreen(ST77XX_BLACK); — identical

    // Header bar
    display.fillRect(0, 0, DISP_W, 50, BLUE);
    display.setCursor(20, 15);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("Hello iPhone!");

    // Info box
    display.drawRoundRect(20, 70, DISP_W - 40, 70, 8, GREEN);
    display.setTextSize(1);
    display.setTextColor(GREEN);
    display.setCursor(35, 88);
    display.print("ESP32PhoneDisplay");
    display.setCursor(35, 103);
    display.print("WiFi transport active");
    display.setCursor(35, 118);
    display.print("mDNS: Hello_ESP32.local");

    // Shapes — identical to any Adafruit_GFX sketch
    display.fillCircle( 70, 230, 35, RED);
    display.fillCircle(170, 230, 35, BLUE);
    display.drawCircle(120, 230, 50, YELLOW);

    display.setCursor(20, 295);
    display.setTextColor(LIGHT_GREY);
    display.setTextSize(1);
    display.print("Switch apps or disconnect to test redraw");

    // flush() sends a sync marker and wakes the WiFi drain task.
    // Auto-flush also delivers commands, but explicit flush ensures
    // deterministic frame delivery.
    // #Ported: no equivalent — local displays render immediately.
    display.flush();

    Serial.println("[Display] Done");
    Serial.flush();
}
