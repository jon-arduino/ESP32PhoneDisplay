// WiFi_HelloWorld — minimal ESP32PhoneDisplay sketch over WiFi
//
// 1. Set WIFI_SSID and WIFI_PASSWORD below
// 2. Flash this sketch to your ESP32
// 3. Open the ESP32PhoneDisplay iPhone app
// 4. Tap "Wi-Fi" — the ESP32 appears as "esp32-display"
// 5. Connect and the display will show the hello world screen
//
// No calls needed in loop() — the transport manages heartbeat and
// auto-flush automatically via a background FreeRTOS task.

#include <ESP32PhoneDisplay.h>
#include <transport/WiFiTransport.h>

// ── Configure your network ────────────────────────────────────────────────────
#define WIFI_SSID     "lotz_net1"
#define WIFI_PASSWORD "thelotznetwork1"
// ─────────────────────────────────────────────────────────────────────────────

#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0

WiFiTransport     transport(WIFI_SSID, WIFI_PASSWORD, "esp32-display");
ESP32PhoneDisplay display(transport);
bool              displayReady = false;

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Serial.println("BOOT");

    // Optional: fall back to ESP32-hosted network if home WiFi unavailable
    transport.setSoftAP("ESP32-Display", "display123");

    transport.onConnected([]() {
        displayReady = false;   // reinit display on reconnect
    });

    transport.begin();
    Serial.println("Waiting for iPhone...");
}

void drawScreen()
{
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
}

void loop()
{
    if (!transport.canSend()) {
        Serial.println("Waiting for iPhone...");
        delay(2000);
        return;
    }

    if (transport.canSend() && !displayReady) {
        drawScreen();
        displayReady = true;
    }
}
