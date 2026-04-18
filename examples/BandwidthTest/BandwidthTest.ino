// BandwidthTest — transport bandwidth and latency diagnostic
//
// Streams random triangles to measure GFX command throughput.
// Displays RTT ping stats (WiFi) and triangles/sec + KB/s.
// Tracks min/max/avg over the test run.
//
// T1 — start streaming triangles
// T2 — stop streaming triangles
//
// Canvas clears every 5 seconds. Stats update every 500ms.
// Console prints per-second min/avg/max triangle rate.
// Works with BLE, WiFi, or DualTransport.

#include <ESP32PhoneDisplay.h>
#include <transport/DualTransport.h>

// ── WiFi credentials ──────────────────────────────────────────────────────────
#define WIFI_SSID     "lotz_net1"
#define WIFI_PASSWORD "thelotznetwork1"
// ─────────────────────────────────────────────────────────────────────────────

#define DISP_W   240
#define DISP_H   320

// ── Layout ────────────────────────────────────────────────────────────────────
#define HEADER_H    45
#define FOOTER_H    46
#define CANVAS_Y1   HEADER_H
#define CANVAS_Y2   (DISP_H - FOOTER_H - 1)
#define CANVAS_H    (CANVAS_Y2 - CANVAS_Y1 + 1)
#define FOOTER_Y1   (DISP_H - FOOTER_H)

// ── Colours ───────────────────────────────────────────────────────────────────
#define BLACK    0x0000
#define WHITE    0xFFFF
#define RED      0xF800
#define GREEN    0x07E0
#define BLUE     0x001F
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0
#define ORANGE   0xFD20
#define PURPLE   0x8010
#define LIME     0x87E0
#define DKGREY   0x2104

static const uint16_t COLORS[] = {
    RED, GREEN, BLUE, CYAN, MAGENTA, YELLOW, ORANGE, PURPLE, LIME, WHITE
};
static constexpr uint8_t NUM_COLORS = sizeof(COLORS) / sizeof(COLORS[0]);

// Triangle frame: [0xA5][lenLo][lenHi][cmd] + x0y0x1y1x2y2color = 4+14 = 18
static constexpr uint32_t TRIANGLE_BYTES   = 18;
static constexpr uint32_t STATS_INTERVAL   = 500;    // ms
static constexpr uint32_t CLEAR_INTERVAL   = 5000;   // ms
static constexpr uint32_t CONSOLE_INTERVAL = 1000;   // ms

// ── Objects ───────────────────────────────────────────────────────────────────
DualTransport     transport(WIFI_SSID, WIFI_PASSWORD, "esp32-display");
ESP32PhoneDisplay display(transport);

// ── State ─────────────────────────────────────────────────────────────────────
volatile bool _running     = false;
volatile bool _drawPending = false;

uint8_t  _colorIdx = 0;

// Per-interval counters (reset every STATS_INTERVAL)
uint32_t _triCount    = 0;
uint32_t _lastStatsMs = 0;
uint32_t _lastClearMs = 0;
uint32_t _lastConsoleMs = 0;

// Current displayed stats
float _triPerSec = 0.0f;
float _kbPerSec  = 0.0f;

// Session min/max/avg (since T1 pressed)
uint32_t _sessionTriMin = UINT32_MAX;
uint32_t _sessionTriMax = 0;
float    _sessionTriSum = 0.0f;
uint32_t _sessionSamples = 0;

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawHeader()
{
    display.fillRect(0, 0, DISP_W, HEADER_H, BLACK);
    display.drawFastHLine(0, HEADER_H - 1, DISP_W, DKGREY);

    display.setCursor(4, 4);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.print("Transport: ");
    display.print(transport.activeTransportName());

    if (transport.isWifiActive()) {
        display.setCursor(4, 16);
        if (transport.rttCount() > 0) {
            display.setTextColor(CYAN);
            display.print("RTT last:");
            display.print(transport.rttLast());
            display.print("ms  min:");
            display.print(transport.rttMin());
            display.print(" avg:");
            display.print(transport.rttAvg());
            display.print(" max:");
            display.print(transport.rttMax());
            display.print("ms");
        } else {
            display.setTextColor(DKGREY);
            display.print("RTT: waiting...");
        }
    } else {
        display.setCursor(4, 16);
        display.setTextColor(DKGREY);
        display.print("RTT: N/A (BLE)");
    }

    // Session stats line
    display.setCursor(4, 30);
    if (_sessionSamples > 0) {
        uint32_t avg = (uint32_t)(_sessionTriSum / _sessionSamples);
        display.setTextColor(YELLOW);
        display.print("Tri: min:");
        display.print(_sessionTriMin);
        display.print(" avg:");
        display.print(avg);
        display.print(" max:");
        display.print(_sessionTriMax);
        display.print("/s");
    } else {
        display.setTextColor(DKGREY);
        display.print("Press T1 to start");
    }
}

void drawFooter()
{
    display.fillRect(0, FOOTER_Y1, DISP_W, FOOTER_H, BLACK);
    display.drawFastHLine(0, FOOTER_Y1, DISP_W, DKGREY);

    display.setCursor(4, FOOTER_Y1 + 6);
    display.setTextColor(WHITE);
    display.setTextSize(1);

    // Always show last stats, add STOPPED indicator if not running
    display.print("Tri/s:");
    display.print((int)_triPerSec);
    display.print("  KB/s:");
    display.print((int)_kbPerSec);
    display.print(".");
    display.print((int)(_kbPerSec * 10) % 10);
    if (!_running) {
        display.setTextColor(RED);
        display.print(" [STOP]");
    }

    display.setCursor(4, FOOTER_Y1 + 20);
    display.setTextColor(DKGREY);
    display.print("T1=start  T2=stop");
}

void drawInitialScreen()
{
    display.begin(DISP_W, DISP_H);
    display.clear(BLACK);
    drawHeader();
    drawFooter();
    display.flush();
}

void clearCanvas()
{
    display.fillRect(0, CANVAS_Y1, DISP_W, CANVAS_H, BLACK);
    display.flush();
}

void drawNextTriangle()
{
    int16_t x0 = random(DISP_W);
    int16_t y0 = random(CANVAS_Y1, CANVAS_Y2);
    int16_t x1 = random(DISP_W);
    int16_t y1 = random(CANVAS_Y1, CANVAS_Y2);
    int16_t x2 = random(DISP_W);
    int16_t y2 = random(CANVAS_Y1, CANVAS_Y2);

    display.drawTriangle(x0, y0, x1, y1, x2, y2, COLORS[_colorIdx]);
    _colorIdx = (_colorIdx + 1) % NUM_COLORS;
    _triCount++;
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    transport.setSoftAP("ESP32-Display", "display123");
    transport.setPowerSave(false);   // stop BLE, disable modem sleep for best WiFi performance

    transport.onConnected([]() {
        Serial.printf("[App] Connected via %s\n",
                      transport.activeTransportName());
        _running       = false;
        _drawPending   = true;
        _triCount      = 0;
        _triPerSec     = 0.0f;
        _kbPerSec      = 0.0f;
        _sessionTriMin = UINT32_MAX;
        _sessionTriMax = 0;
        _sessionTriSum = 0.0f;
        _sessionSamples = 0;
    });

    transport.onDisconnected([]() {
        Serial.println("[App] Disconnected");
        _running = false;
    });

    transport.onKey([](uint8_t key) {
        if (key == '1' && !_running) {
            _running        = true;
            _triCount       = 0;
            _lastStatsMs    = millis();
            _lastClearMs    = millis();
            _lastConsoleMs  = millis();
            _sessionTriMin  = UINT32_MAX;
            _sessionTriMax  = 0;
            _sessionTriSum  = 0.0f;
            _sessionSamples = 0;
            Serial.println("[BW] Started");
        } else if (key == '2' && _running) {
            _running = false;
            Serial.println("[BW] Stopped");
        }
    });

    transport.begin();
    Serial.println("[App] Ready — connect via BLE or WiFi");
}

// ── Loop ─────────────────────────────────────────────────────────────────────

void loop()
{
    if (!transport.canSend()) {
        delay(20);
        return;
    }

    if (_drawPending) {
        _drawPending = false;
        drawInitialScreen();
        _lastStatsMs   = millis();
        _lastClearMs   = millis();
        _lastConsoleMs = millis();
        return;
    }

    uint32_t now = millis();

    // Update display stats every 500ms
    if (now - _lastStatsMs >= STATS_INTERVAL) {
        uint32_t elapsed = now - _lastStatsMs;
        _triPerSec = (_triCount * 1000.0f) / elapsed;
        _kbPerSec  = (_triCount * TRIANGLE_BYTES * 1000.0f) / elapsed / 1024.0f;

        // Update session min/max/avg
        if (_running) {
            uint32_t tps = (uint32_t)_triPerSec;
            if (tps < _sessionTriMin) _sessionTriMin = tps;
            if (tps > _sessionTriMax) _sessionTriMax = tps;
            _sessionTriSum += _triPerSec;
            _sessionSamples++;
        }

        _triCount    = 0;
        _lastStatsMs = now;

        drawHeader();
        drawFooter();
        display.flush();
        return;
    }

    // Console output every 1 second
    if (now - _lastConsoleMs >= CONSOLE_INTERVAL) {
        _lastConsoleMs = now;
        const char *tname = transport.activeTransportName();
        if (_running) {
            Serial.printf("[%s] Tri/s:%.0f  KB/s:%.1f",
                          tname, _triPerSec, _kbPerSec);
            if (transport.isWifiActive() && transport.rttCount() > 0) {
                Serial.printf("  RTT min:%u avg:%u max:%u ms",
                              transport.rttMin(),
                              transport.rttAvg(),
                              transport.rttMax());
            }
            Serial.println();
        } else {
            // Print RTT even when stopped
            if (transport.isWifiActive() && transport.rttCount() > 0) {
                Serial.printf("[%s] RTT min:%u avg:%u max:%u ms\n",
                              tname,
                              transport.rttMin(),
                              transport.rttAvg(),
                              transport.rttMax());
            }
        }
    }

    // Clear canvas every 5 seconds while running
    if (_running && (now - _lastClearMs >= CLEAR_INTERVAL)) {
        _lastClearMs = now;
        clearCanvas();
        return;
    }

    // Draw triangle if running
    if (_running) {
        drawNextTriangle();
        // No explicit flush — auto-flush handles it for max throughput
    }
}
