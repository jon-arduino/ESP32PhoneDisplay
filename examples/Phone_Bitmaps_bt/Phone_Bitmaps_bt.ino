// Web_Bitmaps - test pattern for 1-bit bitmaps (drawBitmap) on a browser display.
//
// What it exercises:
//   * widths that are / are not a multiple of 8 (7,8,9,10,15,16,17,21) - each bitmap has a 1px
//     border plus a diagonal, so a wrong byte count shows up as a missing edge or a skewed diagonal
//   * foreground only (transparent background) vs foreground + background colour
//   * clipping at the left / bottom / right screen edges
//   * a large bitmap (T2): 200x100 = 2600 bytes in a single frame. (A frame bigger than 4096 bytes,
//     e.g. 200x200 = 5000, would hang ESP32PhoneDisplay's WiFiTransport - see the issue list.)
//
// T1 = redraw, T2 = toggle the large bitmap.
//
// Needs the bitmap-opcode update of ESP32PhoneDisplay (0x42-0x45: explicit-colour 1-bit, RGB565,
// masked RGB565, gray). Without it the 'colour' page below will not compile.
//
// T1 = redraw, T2 = big bitmap, and a touch anywhere switches between the 1-bit page and the colour page.
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>


BleTransport     transport;
ESP32PhoneDisplay display(transport);

static const uint16_t BLACK = 0x0000, WHITE = 0xFFFF, RED = 0xF800, GREEN = 0x07E0,
                      BLUE = 0x001F, YELLOW = 0xFFE0, CYAN = 0x07FF, GREY = 0x4208, NAVY = 0x0010;

volatile bool displayReset  = true;
volatile bool redrawPending = false;
volatile bool offline       = false;
volatile bool bigToggle     = false;
volatile bool pageToggle    = false;
bool          showBig       = false;
bool          colourPage    = false;
RemoteTouchScreen ts(transport);

// Adafruit bitmap layout: each row padded to whole bytes, MSB first.
static const uint8_t SMILEY[8] = {0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C};

static uint8_t scratch[2600];               // big enough for 200x100 (25 bytes/row * 100)

// Border + diagonal test pattern, packed row-padded into `scratch`.
static const uint8_t *makePattern(int w, int h)
{
    int stride = (w + 7) / 8;
    memset(scratch, 0, stride * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            bool on = x == 0 || y == 0 || x == w - 1 || y == h - 1 || x == y;
            if (on) scratch[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
        }
    return scratch;
}

// Draw with an explicit colour (opcode 0x42).
static void bmp(int x, int y, const uint8_t *b, int w, int h, uint16_t fg)
{
    display.drawBitmap(x, y, b, w, h, fg);
}
static void bmpBg(int x, int y, const uint8_t *b, int w, int h, uint16_t fg, uint16_t bg)
{
    display.drawBitmap(x, y, b, w, h, fg, bg);
}

static void label(int x, int y, const char *s, uint16_t c = WHITE)
{
    display.setTextSize(1);
    display.setTextColor(c);
    display.setCursor(x, y);
    display.print(s);
}

// ---- colour page: RGB565, masked RGB565, gray, X11 bitmap -------------------------------------

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
static uint16_t imgBuf[64 * 48];
static uint8_t  grayBuf[96 * 24];
static uint8_t  maskBuf[8 * 48];            // 64 wide -> 8 bytes per row

void drawColourPage()
{
    display.fillScreen(BLACK);
    label(4, 4, "colour bitmaps", YELLOW);

    // 64x48 RGB565 gradient with a white frame
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 64; x++)
            imgBuf[y * 64 + x] = (x == 0 || y == 0 || x == 63 || y == 47) ? WHITE
                                 : rgb565(x * 4, y * 5, 255 - x * 4);
    label(4, 18, "RGB 64x48", CYAN);
    display.drawRGBBitmap(4, 30, imgBuf, 64, 48);

    // the same image through a circular mask over a coloured stripe
    display.fillRect(84, 30, 72, 48, RED);
    memset(maskBuf, 0, sizeof maskBuf);
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 64; x++) {
            int dx = x - 32, dy = y - 24;
            if (dx * dx + dy * dy <= 23 * 23) maskBuf[y * 8 + (x >> 3)] |= 0x80 >> (x & 7);
        }
    label(84, 18, "RGB + mask", CYAN);
    display.drawRGBBitmap(88, 30, imgBuf, maskBuf, 64, 48);

    // 8-bit gray ramp (96x24): horizontal ramp, vertical bars every 8 rows
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 96; x++) grayBuf[y * 96 + x] = (y & 8) ? (x * 255 / 95) : (255 - x * 255 / 95);
    label(4, 88, "gray 96x24", CYAN);
    display.drawGrayscaleBitmap(4, 100, grayBuf, 96, 24);

    // X11 bitmap (LSB first): a 16x16 arrow
    static const uint8_t ARROW_XBM[] = {0x80,0x01, 0xC0,0x03, 0xE0,0x07, 0xF0,0x0F, 0xF8,0x1F, 0xFC,0x3F,
                                        0xE0,0x07, 0xE0,0x07, 0xE0,0x07, 0xE0,0x07, 0xE0,0x07, 0xE0,0x07,
                                        0xE0,0x07, 0xE0,0x07, 0xE0,0x07, 0xE0,0x07};
    label(120, 88, "XBM 16x16", CYAN);
    display.drawXBitmap(120, 100, ARROW_XBM, 16, 16, GREEN);

    // A big colour image: 200x100 gradient (40 KB, sent as strips)
    label(4, 134, "big RGB 200x100 (sent in strips)", CYAN);
    // drawRGBBitmap needs the whole image in memory, so build it in a heap buffer
    uint16_t *big = (uint16_t *)malloc(200 * 100 * 2);
    if (big) {
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 200; x++) big[y * 200 + x] = rgb565(x * 255 / 199, y * 255 / 99, (x + y) & 0x80 ? 255 : 0);
        display.drawRGBBitmap(20, 148, big, 200, 100);
        free(big);
    } else label(4, 148, "out of memory", RED);
    label(4, 260, "touch: back to 1-bit page", GREY);
    display.flush();
}

void drawScreen()
{
    Serial.println("[draw]");
    if (displayReset) {
        displayReset = false;
        display.begin(240, 320);
        display.setTitle("Bitmaps");
        display.setButton1("Redraw");
        display.setButton2("Big");
        ts.begin(TOUCH_MODE_RESISTIVE, 50);
    }
    if (colourPage) { drawColourPage(); return; }

    display.fillScreen(BLACK);

    label(4, 4, "1-bit bitmaps", YELLOW);

    // Row 1: smiley, foreground only on a coloured stripe, then fg+bg
    display.fillRect(0, 16, 240, 24, NAVY);
    label(4, 18, "fg only", WHITE);
    bmp(52, 20, SMILEY, 8, 8, YELLOW);
    bmp(66, 20, SMILEY, 8, 8, GREEN);
    label(100, 18, "fg+bg", WHITE);
    bmpBg(142, 20, SMILEY, 8, 8, BLACK, YELLOW);
    bmpBg(156, 20, SMILEY, 8, 8, WHITE, RED);

    // Row 2: widths around the byte boundary. Each has border + diagonal.
    label(4, 46, "widths 7 8 9 10 15 16 17 21 (h=12)", CYAN);
    static const int W[] = {7, 8, 9, 10, 15, 16, 17, 21};
    int x = 4;
    for (int i = 0; i < 8; i++) {
        bmp(x, 58, makePattern(W[i], 12), W[i], 12, WHITE);
        x += W[i] + 6;
    }

    // Row 3: same sizes with a background colour (opaque)
    label(4, 76, "same, fg+bg", CYAN);
    x = 4;
    for (int i = 0; i < 8; i++) {
        bmpBg(x, 88, makePattern(W[i], 12), W[i], 12, BLACK, GREEN);
        x += W[i] + 6;
    }

    // Row 4: medium bitmaps
    label(4, 108, "32x32 and 64x40", CYAN);
    bmp(4, 120, makePattern(32, 32), 32, 32, YELLOW);
    bmpBg(44, 120, makePattern(64, 40), 64, 40, WHITE, BLUE);

    // Row 5: clipping against the screen edges (should be cut off, not wrap)
    label(4, 168, "clipping", CYAN);
    bmp(-5, 180, makePattern(20, 20), 20, 20, RED);        // off the left edge
    bmp(226, 180, makePattern(20, 20), 20, 20, RED);       // off the right edge
    bmp(170, 312, makePattern(20, 20), 20, 20, RED);       // off the bottom edge

    // Row 6: big bitmaps (T2)
    if (showBig) {
        bmpBg(20, 204, makePattern(200, 100), 200, 100, YELLOW, GREY);   // 2600 bytes
    } else {
        label(4, 204, "T2: draw 200x100 (2600 B)", GREY);
    }
    label(4, 306, "touch: colour page", GREY);
    display.flush();
}

void setup()
{
    Serial.begin(115200);
    transport.setDeviceName("Bitmaps_ESP32");
    transport.onDisplayAvailable([](bool a) { offline = !a; if (a) redrawPending = true; });
    transport.onConnected([]()      { offline = false; redrawPending = true; });
    transport.onDisconnected([]()   { offline = true; displayReset = true; });
    transport.onRedrawRequest([]()  { redrawPending = true; });
    transport.onKey([](uint8_t key) { if (key == '2') bigToggle = true; redrawPending = true; });
    transport.begin();
}

void loop()
{
    if (!offline) {
        TSPoint p = ts.getPoint();
        static bool wasDown = false;
        bool down = p.z > RemoteTouchScreen::MINPRESSURE;
        if (down && !wasDown) { colourPage = !colourPage; redrawPending = true; }
        wasDown = down;
    }
    if (bigToggle) { bigToggle = false; showBig = !showBig; }
    if (redrawPending) { redrawPending = false; drawScreen(); }
    delay(10);
}

