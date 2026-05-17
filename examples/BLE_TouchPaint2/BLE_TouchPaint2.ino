// BLE_TouchPaint2 — low-latency finger painting over BLE
//
// Demonstrates the key technique for responsive touch drawing:
//
//   Queue touchpoints — all touch points are accumulated in a buffer.  
//   this enables all points accumulated since the last loop()
//   pass to be drawn in a single batch.  
//
//   Batched Drawing — App performance is not limited by drawing.  It is limited
//   by throughput over BLE.  Throughput is maximized by putting more points in a single packet.
//   Flush() triggers sending a packet.  Do it after a "batch" of circles, not one per
//   circle. Multiple strokes per BLE notification = much lower latency
//   than flushing after every point.
//
//   Duplicate filtering — skips identical touch positions, reducing
//   unnecessary commands and keeping strokes clean.
//
// Why this feels instant vs BLE_TouchPaint:
//
//   BLE_TouchPaint: 1 fillCircle + 1 flush per loop pass.
//     At a 15ms BLE connection interval, max throughput = ~67 strokes/sec.
//     Fast finger movement generates ~120 points/sec — lag accumulates
//     and grows continuously the faster you draw.
//
//   BLE_TouchPaint2: drain ALL queued points, ONE flush per loop pass.
//     All points accumulated during one connection interval go out in
//     the next, regardless of how many. Throughput scales with finger
//     speed — lag cannot accumulate.
//
//   Queue draining and batched flush are inseparable — neither works
//   without the other. Duplicate filtering is a minor refinement.
//
// Connection handling is intentionally minimal — reconnect restarts
// the session and clears the canvas. If the phone disconnects cleanly
// (user action or app close), the canvas is lost. Brief BT interruptions
// within NimBLE's supervision timeout (~20s) recover transparently.

#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>

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
#define HEADER_H      24   // title bar height
#define SWATCH_H      30   // colour swatch row height
#define SWATCH_W      30   // each swatch width
#define NUM_SWATCHES   7
#define PENRADIUS      3

// Swatch colours in order
const uint16_t SWATCHES[] = { RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA, WHITE };

// ── Objects ───────────────────────────────────────────────────────────────────
BleTransport      transport;
ESP32PhoneDisplay display(transport);
RemoteTouchScreen ts(transport);

// ── State ─────────────────────────────────────────────────────────────────────
uint16_t      currentColor = RED;
volatile bool _drawPending = false;
int16_t       _lastX       = -1;
int16_t       _lastY       = -1;

// ── Forward declarations ──────────────────────────────────────────────────────
void drawUI();
void updateSelectionIndicator();
void selectColor(int16_t x);

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);

    // onConnected callback — restart session and canvas on every new BLE connection.
    // _lastX/_lastY reset so first stroke starts clean.
    transport.onConnected([]() {
        Serial.println("[BLE] Connected");
        _lastX       = -1;
        _lastY       = -1;
        _drawPending = true;
    });

    transport.onDisconnected([]() {
        Serial.println("[BLE] Disconnected");
    });

    transport.begin();
    Serial.println("[BLE] Advertising — waiting for iPhone...");
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    // Draw UI on connect or reconnect
    if (_drawPending && transport.canSend()) {
        _drawPending = false;
        drawUI();
        Serial.println("[Display] Ready");
        Serial.flush();
    }

    if (!transport.canSend()) { delay(20); return; }

    // ── Drain entire touch queue ──────────────────────────────────────────────
    // All touch points accumulated since last loop() are drawn in order.
    // A single flush() after the full drain sends all circles in one or
    // two BLE notifications — much lower latency than flushing per point.
    bool drew = false;

    while (ts.available()) {
        TSPoint p = ts.getQueuedPoint();

        if (p.z <= RemoteTouchScreen::MINPRESSURE) {
            // Touch up — reset so next touch starts fresh
            _lastX = -1;
            _lastY = -1;
            continue;
        }

        // Skip duplicate positions — Fewer commands means less BLE congestion.
        if (p.x == _lastX && p.y == _lastY) continue;
        _lastX = p.x;
        _lastY = p.y;

        // Bottom swatch/clear row
        if (p.y >= DISP_H - SWATCH_H) {
            selectColor(p.x);
            continue;
        }

        // Drawing area — below header
        if (p.y > HEADER_H) {
            display.fillCircle(p.x, p.y, PENRADIUS, currentColor);
            drew = true;
        }
    }

    // Single flush after draining entire queued points — batch them into as few BLE notifications as possible.
    if (drew) display.flush();
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawUI()
{
    display.begin(DISP_W, DISP_H);
    display.setTitle("Touch Paint 2");

    display.fillScreen(BLACK);

    // Header bar
    display.fillRect(0, 0, DISP_W, HEADER_H, DKGREY);
    display.setCursor(8, 7);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.print("Touch Paint  |  select colour below");

    // Colour swatches along bottom
    for (int i = 0; i < NUM_SWATCHES; i++)
        display.fillRect(i * SWATCH_W, DISP_H - SWATCH_H, SWATCH_W, SWATCH_H, SWATCHES[i]);

    // CLEAR button — fills remaining width to right of swatches
    display.fillRect(NUM_SWATCHES * SWATCH_W, DISP_H - SWATCH_H,
                     DISP_W - NUM_SWATCHES * SWATCH_W, SWATCH_H, WHITE);
    display.setCursor(NUM_SWATCHES * SWATCH_W + 4, DISP_H - SWATCH_H + 10);
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.print("CLR");

    // Selection indicator bar above active swatch
    updateSelectionIndicator();

    // Start touch with 16ms move throttle for smooth painting
    ts.begin(TOUCH_MODE_RESISTIVE, 16);

    display.flush();
}

void updateSelectionIndicator()
{
    // Clear indicator row then mark active swatch
    display.fillRect(0, DISP_H - SWATCH_H - 4, DISP_W, 4, BLACK);
    for (int i = 0; i < NUM_SWATCHES; i++) {
        if (SWATCHES[i] == currentColor) {
            display.drawRect(i * SWATCH_W, DISP_H - SWATCH_H - 4, SWATCH_W, 4, currentColor);
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
