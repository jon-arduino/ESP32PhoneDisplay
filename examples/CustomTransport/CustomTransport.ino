// CustomTransport — implement your own transport channel
//
// Shows how to use ESP32PhoneDisplay with any communication channel
// by implementing the GraphicsTransport interface.
//
// This example uses Serial2 (UART) as a demo transport.
// The same pattern works for LoRa, RS485, USB CDC, SPI, or anything else.

#include <ESP32PhoneDisplay.h>
#include <GraphicsTransport.h>

// ── Custom transport implementation ──────────────────────────────────────────
//
// Subclass GraphicsTransport and implement:
//   send()    — deliver bytes to your channel
//   canSend() — return true when channel is ready
//   flush()   — optional, flush any internal buffer
//   reset()   — optional, discard buffered bytes on reconnect

class SerialTransport : public GraphicsTransport
{
public:
    void begin(uint32_t baud = 115200)
    {
        Serial2.begin(baud);
        _ready = true;
    }

    void send(const uint8_t *data, uint16_t len) override
    {
        Serial2.write(data, len);
    }

    bool canSend() const override
    {
        return _ready;
    }

    void flush() override
    {
        Serial2.flush();
    }

private:
    bool _ready = false;
};

// ── Sketch ────────────────────────────────────────────────────────────────────

SerialTransport     transport;
ESP32PhoneDisplay   display(transport);

#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREEN   0x07E0

void setup()
{
    Serial.begin(115200);
    transport.begin(115200);

    display.begin(240, 320);
    display.clear(BLACK);
    display.setCursor(10, 10);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.print("Custom transport!");
    display.drawCircle(120, 200, 80, GREEN);
    display.flush();
}

void loop()
{
    delay(1000);
}
