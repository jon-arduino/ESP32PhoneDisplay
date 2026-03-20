// BLE_HelloWorld — minimal ESP32PhoneDisplay sketch over BLE
//
// 1. Flash this sketch to your ESP32
// 2. Open the ESP32PhoneDisplay iPhone app
// 3. Tap "Bluetooth" and connect to "ESP32-Display"
// 4. The display will show a filled rectangle and "Hello iPhone!"

#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>

// Colour helpers (RGB565)
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F

BleTransport        transport;
ESP32PhoneDisplay   display(transport);

void setup()
{
    Serial.begin(115200);
    transport.begin();       // starts BLE advertising as "ESP32-Display"

    // Wait for iPhone to connect and subscribe
    Serial.println("Waiting for iPhone...");
    while (!transport.canSend()) { delay(100); }

    // Initialise virtual display (width x height in pixels)
    display.begin(240, 320);
    display.clear(BLACK);

    // Draw something
    display.fillRect(20, 20, 200, 60, BLUE);
    display.setCursor(30, 40);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("Hello iPhone!");

    display.drawCircle(120, 200, 50, GREEN);
    display.fillCircle(120, 200, 30, RED);

    display.flush();
    Serial.println("Display initialised.");
}

void loop()
{
    // Nothing to do — display is static in this example
    delay(1000);
}
