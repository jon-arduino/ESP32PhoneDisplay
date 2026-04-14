// SerialTest — minimal serial output test
//
// No WiFi, no BLE, no library dependencies.
// Proves that run_example, PIO flash, and Serial monitor work correctly.
//
// Expected output: counter incrementing every second

#include <Arduino.h>

uint32_t counter = 0;

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== SerialTest START ===");
}

void loop()
{
    Serial.printf("count: %lu\n", counter++);
    Serial.println("--------------------------");
    delay(1000);
}
