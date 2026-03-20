// BLE_Telemetry — sensor data display over BLE
//
// Demonstrates a realistic use case: displaying live sensor readings
// that update every second. Uses efficient text rendering — each
// print() call sends a single compact command, not pixel streams.
//
// The display shows:
//   - A static label frame (drawn once on connect)
//   - Live values that update in place

#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>

// Colour helpers (RGB565)
#define BLACK       0x0000
#define WHITE       0xFFFF
#define CYAN        0x07FF
#define YELLOW      0xFFE0
#define GREEN       0x07E0
#define RED         0xF800
#define DARK_GREY   0x4208

// Display layout constants
#define DISP_W      240
#define DISP_H      320
#define LABEL_X     10
#define VALUE_X     130
#define ROW_H       40
#define ROW1_Y      80
#define ROW2_Y      (ROW1_Y + ROW_H)
#define ROW3_Y      (ROW2_Y + ROW_H)
#define ROW4_Y      (ROW3_Y + ROW_H)

BleTransport        transport;
ESP32PhoneDisplay   display(transport);

bool     displayReady = false;
uint32_t lastUpdate   = 0;

// ── Simulated sensor readings (replace with your actual sensors) ──────────────
float readTemperature() { return 22.5f + (float)(millis() % 100) / 100.0f; }
float readHumidity()    { return 55.0f + (float)(millis() % 200) / 100.0f; }
float readPressure()    { return 1013.2f + (float)(millis() % 50) / 10.0f; }
int   readBattery()     { return 85 - (int)(millis() / 60000) % 20; }

void drawLabelFrame()
{
    display.clear(BLACK);

    // Title bar
    display.fillRect(0, 0, DISP_W, 60, DARK_GREY);
    display.setCursor(10, 18);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("ESP32 Telemetry");

    // Labels (static — drawn once)
    display.setTextSize(1);
    display.setTextColor(CYAN);

    display.setCursor(LABEL_X, ROW1_Y);  display.print("Temperature:");
    display.setCursor(LABEL_X, ROW2_Y);  display.print("Humidity:");
    display.setCursor(LABEL_X, ROW3_Y);  display.print("Pressure:");
    display.setCursor(LABEL_X, ROW4_Y);  display.print("Battery:");

    display.flush();
}

void updateValues()
{
    float temp = readTemperature();
    float humi = readHumidity();
    float pres = readPressure();
    int   batt = readBattery();

    display.setTextSize(2);

    // Clear and redraw each value in place
    display.fillRect(VALUE_X, ROW1_Y - 2, DISP_W - VALUE_X - 5, 18, BLACK);
    display.setCursor(VALUE_X, ROW1_Y);
    display.setTextColor(temp > 30.0f ? RED : GREEN);
    display.print(temp, 1);
    display.print(" C");

    display.fillRect(VALUE_X, ROW2_Y - 2, DISP_W - VALUE_X - 5, 18, BLACK);
    display.setCursor(VALUE_X, ROW2_Y);
    display.setTextColor(YELLOW);
    display.print(humi, 1);
    display.print(" %");

    display.fillRect(VALUE_X, ROW3_Y - 2, DISP_W - VALUE_X - 5, 18, BLACK);
    display.setCursor(VALUE_X, ROW3_Y);
    display.setTextColor(WHITE);
    display.print(pres, 1);
    display.print(" hPa");

    display.fillRect(VALUE_X, ROW4_Y - 2, DISP_W - VALUE_X - 5, 18, BLACK);
    display.setCursor(VALUE_X, ROW4_Y);
    display.setTextColor(batt < 20 ? RED : GREEN);
    display.print(batt);
    display.print(" %");

    display.flush();
}

void setup()
{
    Serial.begin(115200);

    transport.onSubscribed([](bool ready) {
        if (ready) {
            drawLabelFrame();
            displayReady = true;
        } else {
            displayReady = false;
        }
    });

    transport.begin();
    Serial.println("Waiting for iPhone...");
}

void loop()
{
    if (!displayReady) { delay(100); return; }

    uint32_t now = millis();
    if (now - lastUpdate >= 1000) {
        lastUpdate = now;
        updateValues();
    }
}
