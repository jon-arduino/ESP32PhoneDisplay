// CompatLayer — drop-in replacement for Adafruit_GFX sketches
//
// Shows how to port an existing Adafruit_GFX sketch to ESP32PhoneDisplay
// with minimal changes. ESP32PhoneDisplay_Compat subclasses Adafruit_GFX
// so it works with any library that accepts an Adafruit_GFX* pointer.
//
// Original sketch:
//   #include <Adafruit_ST7735.h>
//   Adafruit_ST7735 tft(CS_PIN, DC_PIN, RST_PIN);
//   tft.initR(INITR_BLACKTAB);
//
// Ported sketch (changes marked with <--):
//   #include <ESP32PhoneDisplay_Compat.h>   <--
//   #include <transport/BleTransport.h>     <--
//   BleTransport transport;                 <--
//   ESP32PhoneDisplay_Compat tft(transport);<--
//   transport.begin();                      <-- replaces pin/SPI setup
//   tft.begin();                            <-- replaces initR()
//
// Everything else — tft.print(), tft.drawCircle(), tft.fillRect() — unchanged.

#include <ESP32PhoneDisplay_Compat.h>
#include <transport/BleTransport.h>

// Forward declarations — required for C++ (Arduino IDE adds these automatically)
void testFillScreen();
void testText();
void testLines(uint16_t color);
void testRects(uint16_t color);
void testCircles(uint8_t radius, uint16_t color);

BleTransport              transport;
ESP32PhoneDisplay_Compat  tft(transport);   // 240x320 default

// Colour defines — identical to Adafruit_GFX examples
#define BLACK       0x0000
#define WHITE       0xFFFF
#define RED         0xF800
#define GREEN       0x07E0
#define BLUE        0x001F
#define CYAN        0x07FF
#define MAGENTA     0xF81F
#define YELLOW      0xFFE0
#define ORANGE      0xFC00

void setup()
{
    Serial.begin(115200);
    transport.begin();

    Serial.println("Waiting for iPhone...");
    while (!tft.isConnected()) { delay(100); }

    tft.begin();   // sends BEGIN command — tells iPhone the display size

    // From here — identical to any Adafruit_GFX example sketch
    testFillScreen();
    delay(500);
    testText();
    delay(500);
    testLines(CYAN);
    delay(500);
    testRects(GREEN);
    delay(500);
    testCircles(10, MAGENTA);
    delay(500);

    Serial.println("Done.");
}

void loop() { delay(1000); }

// ── Test functions — identical to Adafruit graphicstest example ──────────────

void testFillScreen()
{
    tft.fillScreen(BLACK);
    tft.fillScreen(RED);
    tft.fillScreen(GREEN);
    tft.fillScreen(BLUE);
    tft.fillScreen(BLACK);
}

void testText()
{
    tft.fillScreen(BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(WHITE);  tft.setTextSize(1);
    tft.println("Hello World!");
    tft.setTextColor(YELLOW); tft.setTextSize(2);
    tft.println(1234.56);
    tft.setTextColor(RED);    tft.setTextSize(3);
    tft.println(0xDEAD, HEX);
    tft.setTextColor(GREEN);  tft.setTextSize(5);
    tft.println("Groop");
    tft.setTextSize(2);
    tft.println("I implore thee,");
}

void testLines(uint16_t color)
{
    tft.fillScreen(BLACK);
    for (int16_t x = 0; x < tft.width(); x += 6)
        tft.drawLine(0, 0, x, tft.height() - 1, color);
    for (int16_t y = 0; y < tft.height(); y += 6)
        tft.drawLine(0, 0, tft.width() - 1, y, color);
}

void testRects(uint16_t color)
{
    tft.fillScreen(BLACK);
    for (int16_t x = 0; x < tft.width() / 2; x += 6)
        tft.drawRect(x, x, tft.width() - 2*x, tft.height() - 2*x, color);
}

void testCircles(uint8_t radius, uint16_t color)
{
    tft.fillScreen(BLACK);
    for (int16_t x = radius; x < tft.width(); x += radius * 2)
        for (int16_t y = radius; y < tft.height(); y += radius * 2)
            tft.drawCircle(x, y, radius, color);
}

