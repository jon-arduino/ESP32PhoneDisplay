// WiFi_HelloWorld — minimal ESP32PhoneDisplay sketch over WiFi
//
// 1. Set WIFI_SSID and WIFI_PASSWORD below
// 2. Flash this sketch to your ESP32
// 3. Open the ESP32PhoneDisplay iPhone app
// 4. Tap "Wi-Fi" — the ESP32 appears as "esp32-display"
// 5. Connect and the display will show the hello world screen

#include <ESP32PhoneDisplay.h>
#include <transport/WiFiTransport.h>

// ── Configure your network ────────────────────────────────────────────────────
#define WIFI_SSID     "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
// ─────────────────────────────────────────────────────────────────────────────

// Colour helpers (RGB565)
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0

WiFiTransport       transport(WIFI_SSID, WIFI_PASSWORD, "esp32-display");
ESP32PhoneDisplay   display(transport);

void setup()
{
    Serial.begin(115200);

    // Optional: fall back to ESP32-hosted network if home WiFi unavailable
    // (useful for demos away from home — join "ESP32-Display" on iPhone)
    transport.setSoftAP("ESP32-Display", "display123");

    transport.begin();       // connects to WiFi (or starts SoftAP on timeout)

    Serial.println("Waiting for iPhone...");
    while (!transport.canSend()) { delay(100); }

    display.begin(240, 320);
    display.clear(BLACK);

    display.fillRect(10, 10, 220, 50, BLUE);
    display.setCursor(20, 25);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("Hello iPhone!");

    display.setCursor(10, 80);
    display.setTextColor(YELLOW);
    display.setTextSize(1);
    display.print("Connected via WiFi");

    display.drawRoundRect(10, 110, 220, 80, 10, GREEN);
    display.fillCircle(120, 230, 60, RED);

    display.flush();
    Serial.println("Display initialised.");
}

void loop()
{
    delay(1000);
}
