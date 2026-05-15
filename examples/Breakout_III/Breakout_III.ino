// Breakout_III — replaces Breakout_II
//
// Fully native ESP32PhoneDisplay (no compat layer). Every GFX operation
// sends a single compact BLE command — fillRect, fillCircle, print() etc.
// are all single commands regardless of size or content.
//
// Improvements over Breakout_II:
//   - Native ESP32PhoneDisplay throughout — no Adafruit_GFX decomposition
//   - setTitle / setButton1 / setButton2 via session API
//   - onRedrawRequest for clean reconnect
//   - BC diagnostic stats behind #define DEBUG
//
// Game features (carried from Breakout_II):
//   - Fixed 30fps frame rate via millis()-based frame budget
//   - Ball speed in pixels-per-frame (consistent regardless of BLE jitter)
//   - Brick flash state machine — no blocking delays
//   - Ball pauses during brick flash (2 frames = 66ms)
//   - T1 = autoplay, T2 = player control
//
// Original game by Enrique Albertos (public domain)

#include <Arduino.h>
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>

// ── Tuning ────────────────────────────────────────────────────────────────────
#define FRAME_MS             33    // target frame time (~30fps)
#define SPEED_MULTIPLIER     6.0f  // ball speed scale — tune to taste
#define TOUCH_INTERVAL_MS    30    // iPhone MOVE event throttle (ms)
#define BLE_INTERVAL_MIN_MS  15    // BLE connection interval min (ms)
#define BLE_INTERVAL_MAX_MS  30    // BLE connection interval max (ms)
#define DEBUG                0     // 1 = frame overrun prints + BC stats

// ── Colours ───────────────────────────────────────────────────────────────────
#define BLACK           0x0000
#define BLUE            0x001F
#define RED             0xF800
#define GREEN           0x07E0
#define CYAN            0x07FF
#define MAGENTA         0xF81F
#define YELLOW          0xFFE0
#define WHITE           0xFFFF
#define PRIMARY_DARK    0x4016

// ── Display ───────────────────────────────────────────────────────────────────
#define DISP_W  240
#define DISP_H  320

// ── Game constants ────────────────────────────────────────────────────────────
#define SCORE_SIZE   30
#define GAMES_NUMBER 16

// ── Types ─────────────────────────────────────────────────────────────────────
typedef struct {
    int ballsize, playerwidth, playerheight, exponent, top, rows, columns, brickGap, lives;
    int wall[GAMES_NUMBER];
    int initVelx, initVely;
} game_type;

typedef struct {
    int32_t  ballx, bally, ballxold, ballyold;  // fixed-point
    int velx, vely, playerx, playerxold;
    int wallState[8];
    int score, remainingLives, top, bottom, walltop, wallbottom, brickheight, brickwidth;
} game_state_type;

// ── Brick flash state machine ─────────────────────────────────────────────────
enum FlashState { FLASH_NONE, FLASH_WHITE, FLASH_BLUE };

struct BrickFlash {
    FlashState state = FLASH_NONE;
    int        x     = 0;
    int        y     = 0;
    int        score = 0;
};

// ── Forward declarations ──────────────────────────────────────────────────────
void newGame(game_type*, game_state_type*);
void setupState(game_type*, game_state_type*);
void setupStateSizes(game_type*, game_state_type*);
void setupWall(game_type*, game_state_type*);
void drawBrick(game_state_type*, int, int, uint16_t);
void drawPlayer(game_type*, game_state_type*);
void drawBall(int, int, int, int, int);
void updateLives(int, int);
void updateScore(int);
void checkBallCollisions(game_type*, game_state_type*, int16_t, int16_t);
void checkBrickCollision(game_type*, game_state_type*, int16_t, int16_t);
int  checkCornerCollision(game_type*, game_state_type*, int16_t, int16_t);
void startBrickFlash(game_state_type*, int, int);
void checkBorderCollision(game_type*, game_state_type*, int16_t, int16_t);
void checkBallExit(game_type*, game_state_type*, int16_t, int16_t);
boolean noBricks(game_type*, game_state_type*);
void drawBoxedString(int16_t, int16_t, const char*, uint8_t, uint16_t, uint16_t);
void clearScreen();
void touchToStart();
void gameOverTouchToStart();
void readUiSelection(game_type*, game_state_type*);
int  waitForTouch();
void redrawFullScreen();
void setBrick(int[], uint8_t, uint8_t);
void unsetBrick(int[], uint8_t, uint8_t);
boolean isBrickIn(int[], uint8_t, uint8_t);
void initDisplay();

// ── Game data ─────────────────────────────────────────────────────────────────
game_type games[GAMES_NUMBER] = {
    {10,60,8,6,40,8,8,3,3,{0x18,0x66,0xFF,0xDB,0xFF,0x7E,0x24,0x3C},28,-28},
    {10,50,8,6,40,8,8,3,3,{0xFF,0x99,0xFF,0xE7,0xBD,0xDB,0xE7,0xFF},28,-28},
    {10,50,8,6,40,8,8,3,3,{0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55},28,-28},
    { 8,50,8,6,40,8,8,3,3,{0xFF,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xFF},34,-34},
    {10,40,8,6,40,8,8,3,3,{0xFF,0xAA,0xAA,0xFF,0xFF,0xAA,0xAA,0xFF},28,-28},
    {10,40,8,6,40,8,8,3,3,{0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA},28,-28},
    {12,64,8,6,60,4,2,3,4,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},20,-20},
    {12,60,8,6,60,5,3,3,4,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},22,-22},
    {10,56,8,6,30,6,4,3,4,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},24,-24},
    {10,52,8,6,30,7,5,3,4,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},26,-26},
    { 8,48,8,6,30,8,6,3,3,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},28,-28},
    { 8,44,8,6,30,8,7,3,3,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},30,-30},
    { 8,40,8,6,30,8,8,3,3,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},32,-32},
    { 8,36,8,6,40,8,8,3,3,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},34,-34},
    { 8,36,8,6,40,8,8,3,3,{0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA},34,-34},
    { 8,36,8,6,40,8,8,3,3,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},36,-36},
};

// ── Objects ───────────────────────────────────────────────────────────────────
BleTransport      transport;
ESP32PhoneDisplay display(transport);
RemoteTouchScreen ts(transport);

// ── Game state ────────────────────────────────────────────────────────────────
game_type*      game;
game_state_type state;
uint16_t        backgroundColor = BLACK;
int             level;
BrickFlash      flash;

const uint8_t BIT_MASK[]     = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
uint8_t       pointsForRow[] = {7,7,5,5,3,3,1,1};
char          scoreFormat[]  = "%04d";

// ── Volatile flags — set on core 0 (BLE task), read on core 1 (loop) ─────────
// Never call Serial or display from BLE callbacks — use flags instead.
static volatile bool     _drawPending    = false;
static volatile bool     _autoPlay       = false;
static volatile bool     _paused         = false;
static volatile int      _autoPlayMsg    = 0;    // 1=ON 2=OFF — printed from loop()
static volatile float    _connIntervalMs = 0;    // >0 = new interval to print

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);
    Serial.println("[Breakout_III] Booting...");

    transport.setConnectionInterval(BLE_INTERVAL_MIN_MS, BLE_INTERVAL_MAX_MS);

    transport.onConnInterval([](float ms) {
        _connIntervalMs = ms;
    });

    transport.onKey([](uint8_t key) {
        if      (key == '1') { _autoPlay = true;  _autoPlayMsg = 1; }
        else if (key == '2') { _autoPlay = false; _autoPlayMsg = 2; }
    });

    // onSubscribed: fallback for older app versions that don't send redraw request
    transport.onSubscribed([](bool ready) {
        if (ready) { _paused = false; _drawPending = true; }
        else       { _paused = true; }
    });

    // onRedrawRequest: sent by current app ~100ms after connect/reconnect
    transport.onRedrawRequest([]() {
        _paused = false; _drawPending = true;
    });

    transport.begin();
    Serial.println("[Game] Waiting for iPhone...");

    while (!_drawPending) delay(100);
    _drawPending = false;

    level = 0;
    initDisplay();
    newGame(&games[0], &state);
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop()
{
    uint32_t frameStart = millis();

    // Print deferred messages — safe here on core 1
    if (_autoPlayMsg == 1) { Serial.println("[Game] Auto-play ON");  _autoPlayMsg = 0; }
    if (_autoPlayMsg == 2) { Serial.println("[Game] Player mode ON"); _autoPlayMsg = 0; }
    if (_connIntervalMs > 0) {
        Serial.printf("[BLE] Interval: %.1fms\n", _connIntervalMs);
        _connIntervalMs = 0;
    }

#if DEBUG
    // Back-channel diagnostic counters — useful during testing, off in release.
    // syncErrors: bytes discarded before a valid frame start was found.
    // overruns:   frame longer than parser buffer (should never happen).
    // invalidFrames: frame with bad length field.
    // unknownCmds: unrecognised command byte from iPhone.
    static uint32_t lastKey1 = 0, lastKey2 = 0;
    auto s = transport.bcStats();
    if (s.key1 != lastKey1 || s.key2 != lastKey2) {
        Serial.printf("[BC] K1=%u K2=%u touch=%u sync=%u overrun=%u invalid=%u unknown=%u\n",
                      s.key1, s.key2, s.touch,
                      s.syncErrors, s.overruns, s.invalidFrames, s.unknownCmds);
        lastKey1 = s.key1;
        lastKey2 = s.key2;
    }
#endif

    // Paused — disconnected, spin quietly until reconnect
    if (_paused) { delay(100); return; }

    // Reconnect — rebuild full display from current game state
    if (_drawPending) {
        _drawPending = false;
        Serial.println("[Game] Reconnected — redrawing");
        flash.state = FLASH_NONE;
        initDisplay();
        redrawFullScreen();
        return;
    }

    // 1. Read touch — always before any BLE sends
    state.playerxold = state.playerx;
    readUiSelection(game, &state);

    // 2. Advance brick flash state machine
    if (flash.state != FLASH_NONE) {
        if (flash.state == FLASH_WHITE) {
            drawBrick(&state, flash.x, flash.y, BLUE);
            flash.state = FLASH_BLUE;
        } else {
            // FLASH_BLUE — remove brick and award points
            drawBrick(&state, flash.x, flash.y, backgroundColor);
            unsetBrick(state.wallState, flash.x, flash.y);
            state.score += flash.score;
            updateScore(state.score);
            flash.state = FLASH_NONE;
        }
    } else {
        // 3. Physics — only when no flash active
        int maxV = (int)(((1 << game->exponent) - 1) * SPEED_MULTIPLIER);
        if (abs(state.vely) > maxV) state.vely = maxV * ((state.vely > 0) - (state.vely < 0));
        if (abs(state.velx) > maxV) state.velx = maxV * ((state.velx > 0) - (state.velx < 0));

        state.ballx += state.velx;
        state.bally += state.vely;

        checkBallCollisions(game, &state,
                            (int16_t)(state.ballx >> game->exponent),
                            (int16_t)(state.bally >> game->exponent));
        checkBallExit(game, &state,
                      (int16_t)(state.ballx >> game->exponent),
                      (int16_t)(state.bally >> game->exponent));

        state.velx = (int)((20 + (state.score >> 3)) * SPEED_MULTIPLIER) * ((state.velx > 0) - (state.velx < 0));
        state.vely = (int)((20 + (state.score >> 3)) * SPEED_MULTIPLIER) * ((state.vely > 0) - (state.vely < 0));
    }

    // 4. Draw ball and player — batched before flush
    drawBall((int16_t)(state.ballx >> game->exponent), (int16_t)(state.bally >> game->exponent),
             (int16_t)(state.ballxold >> game->exponent), (int16_t)(state.ballyold >> game->exponent),
             game->ballsize);
    drawPlayer(game, &state);
    state.playerxold = state.playerx;
    state.ballxold   = state.ballx;
    state.ballyold   = state.bally;

    // 5. Flush frame to iPhone
    display.flush();

    // 6. Level / game-over transitions
    if (flash.state == FLASH_NONE) {
        if (noBricks(game, &state) && level < GAMES_NUMBER - 1) {
            level++;
            newGame(&games[level], &state);
            return;
        } else if (state.remainingLives <= 0) {
            gameOverTouchToStart();
            state.score = 0;
            level = 0;
            newGame(&games[0], &state);
            return;
        }
    }

    // 7. Frame budget — spin-wait for remainder of FRAME_MS
    // delay(1) yields to FreeRTOS so BLE drain task and IDLE get CPU time
#if DEBUG
    {
        uint32_t elapsed = millis() - frameStart;
        if (elapsed > FRAME_MS)
            Serial.printf("[Frame] overrun: %ums\n", elapsed);
    }
#endif
    while (millis() - frameStart < FRAME_MS) delay(1);
}

// ── Display session ───────────────────────────────────────────────────────────

void initDisplay()
{
    display.begin(DISP_W, DISP_H);
    display.setTitle("Breakout III");
    display.setButton1("Auto");    // T1 — autoplay mode
    display.setButton2("Player");  // T2 — player control
    ts.begin(TOUCH_MODE_RESISTIVE, TOUCH_INTERVAL_MS);
}

// ── Game functions ────────────────────────────────────────────────────────────

void newGame(game_type* g, game_state_type* s)
{
    game = g;
    setupState(game, s);
    clearScreen();
    updateLives(game->lives, s->remainingLives);
    updateScore(s->score);
    setupWall(game, s);
    display.flush();
    touchToStart();
    clearScreen();
    updateLives(game->lives, s->remainingLives);
    updateScore(s->score);
    setupWall(game, s);
    display.flush();
}

void setupStateSizes(game_type* g, game_state_type* s)
{
    s->bottom      = DISP_H - 30;
    s->brickwidth  = DISP_W / g->columns;
    s->brickheight = DISP_H / 24;
}

void setupState(game_type* g, game_state_type* s)
{
    setupStateSizes(g, s);
    for (int i = 0; i < g->rows; i++) s->wallState[i] = 0;
    s->playerx        = DISP_W / 2 - g->playerwidth / 2;
    s->remainingLives = g->lives;
    s->bally          = s->bottom << g->exponent;
    s->ballyold       = s->bottom << g->exponent;
    s->velx           = g->initVelx;
    s->vely           = g->initVely;
    flash.state       = FLASH_NONE;
}

void updateLives(int lives, int remaining)
{
    for (int i = 0; i < lives;     i++) display.fillCircle((1+i)*15, 15, 5, BLACK);
    for (int i = 0; i < remaining; i++) display.fillCircle((1+i)*15, 15, 5, YELLOW);
}

void setupWall(game_type* g, game_state_type* s)
{
    int colors[] = {RED, RED, BLUE, BLUE, YELLOW, YELLOW, GREEN, GREEN};
    s->walltop    = g->top + 40;
    s->wallbottom = s->walltop + g->rows * s->brickheight;
    for (int i = 0; i < g->rows; i++)
        for (int j = 0; j < g->columns; j++)
            if (isBrickIn(g->wall, j, i)) {
                setBrick(s->wallState, j, i);
                drawBrick(s, j, i, colors[i]);
            }
}

void drawBrick(game_state_type* s, int xBrick, int yRow, uint16_t color)
{
    display.fillRect((s->brickwidth * xBrick) + game->brickGap,
                      s->walltop + (s->brickheight * yRow) + game->brickGap,
                      s->brickwidth  - game->brickGap * 2,
                      s->brickheight - game->brickGap * 2,
                      color);
}

boolean noBricks(game_type* g, game_state_type* s)
{
    for (int i = 0; i < g->rows; i++) if (s->wallState[i]) return false;
    return true;
}

void drawPlayer(game_type* g, game_state_type* s)
{
    display.fillRect(s->playerx, s->bottom, g->playerwidth, g->playerheight, YELLOW);
    if (s->playerx < s->playerxold)
        display.fillRect(s->playerx + g->playerwidth, s->bottom,
                         abs(s->playerx - s->playerxold), g->playerheight, backgroundColor);
    else if (s->playerx > s->playerxold)
        display.fillRect(s->playerxold, s->bottom,
                         abs(s->playerx - s->playerxold), g->playerheight, backgroundColor);
}

void drawBall(int x, int y, int xold, int yold, int ballsize)
{
    if      (xold<=x && yold<=y) { display.fillRect(xold,yold,ballsize,y-yold,BLACK); display.fillRect(xold,yold,x-xold,ballsize,BLACK); }
    else if (xold>=x && yold>=y) { display.fillRect(x+ballsize,yold,xold-x,ballsize,BLACK); display.fillRect(xold,y+ballsize,ballsize,yold-y,BLACK); }
    else if (xold<=x && yold>=y) { display.fillRect(xold,yold,x-xold,ballsize,BLACK); display.fillRect(xold,y+ballsize,ballsize,yold-y,BLACK); }
    else                         { display.fillRect(xold,yold,ballsize,y-yold,BLACK); display.fillRect(x+ballsize,yold,xold-x,ballsize,BLACK); }
    display.fillRect(x, y, ballsize, ballsize, YELLOW);
}

void startBrickFlash(game_state_type* s, int xBrick, int yRow)
{
    flash.state = FLASH_WHITE;
    flash.x     = xBrick;
    flash.y     = yRow;
    flash.score = pointsForRow[yRow];
    drawBrick(s, xBrick, yRow, WHITE);
}

void checkBrickCollision(game_type* g, game_state_type* s, int16_t x, int16_t y)
{
    if (flash.state != FLASH_NONE) return;
    int x1 = x + g->ballsize, y1 = y + g->ballsize;
    int hits = checkCornerCollision(g,s,x, y)
             + checkCornerCollision(g,s,x1,y1)
             + checkCornerCollision(g,s,x, y1)
             + checkCornerCollision(g,s,x1,y);
    if (hits > 0) {
        s->vely = -s->vely;
        if (((x % s->brickwidth == 0) && s->velx < 0) ||
            (((x + g->ballsize) % s->brickwidth == 0) && s->velx > 0))
            s->velx = -s->velx;
    }
}

int checkCornerCollision(game_type* g, game_state_type* s, int16_t x, int16_t y)
{
    if (flash.state != FLASH_NONE) return 0;
    if (x < 0 || x >= DISP_W) return 0;
    if (y > s->walltop && y < s->wallbottom) {
        int yRow = (y - s->walltop) / s->brickheight;
        int xCol = x / s->brickwidth;
        if (xCol >= 0 && xCol < g->columns && yRow >= 0 && yRow < g->rows) {
            if (isBrickIn(s->wallState, xCol, yRow)) {
                startBrickFlash(s, xCol, yRow);
                return 1;
            }
        }
    }
    return 0;
}

void checkBorderCollision(game_type* g, game_state_type* s, int16_t x, int16_t y)
{
    // Right wall
    if (x + g->ballsize >= DISP_W) {
        s->velx = -abs(s->velx);
        s->ballx = (int32_t)(DISP_W - g->ballsize - 1) << g->exponent;
    }
    // Left wall
    if (x < 0) {
        s->velx = abs(s->velx);
        s->ballx = 0;
    }
    // Top wall — ball enters score area
    if (y <= SCORE_SIZE) {
        s->vely = abs(s->vely);
        s->bally = (int32_t)(SCORE_SIZE + 1) << g->exponent;
    }
    // Paddle
    if ((y + g->ballsize) >= s->bottom
        && (y + g->ballsize) <= (s->bottom + g->playerheight)
        && x + g->ballsize >= s->playerx
        && x <= (s->playerx + g->playerwidth)) {
        if      (x > (s->playerx + g->playerwidth - 6)) s->velx--;
        else if (x < (s->playerx + 6))                  s->velx++;
        s->vely = -abs(s->vely);
        s->bally = (int32_t)(s->bottom - g->ballsize - 1) << g->exponent;
    }
}

void checkBallCollisions(game_type* g, game_state_type* s, int16_t x, int16_t y)
{
    checkBrickCollision(g, s, x, y);
    checkBorderCollision(g, s, x, y);
}

void checkBallExit(game_type* g, game_state_type* s, int16_t x, int16_t y)
{
    if (y + g->ballsize >= DISP_H) {
        s->remainingLives--;
        updateLives(g->lives, s->remainingLives);
        display.flush();
        s->vely = -abs(s->vely);
        s->bally = (int32_t)(DISP_H - g->ballsize - 1) << g->exponent;
    }
}

// ── Text helpers ──────────────────────────────────────────────────────────────

// drawBoxedString — clears a background rect then draws text using native print().
//
// ESP32PhoneDisplay has no getTextBounds() — it does not subclass Adafruit_GFX.
// Background rect dimensions use the fixed default font metrics:
//   width  = strlen × 6 × textSize  (5px glyph + 1px gap, per character)
//   height = 8 × textSize
// This is accurate for the built-in font and sufficient for clearing backgrounds.
void drawBoxedString(int16_t x, int16_t y, const char* str,
                     uint8_t textSize, uint16_t foreColor, uint16_t bgColor)
{
    uint16_t w = (uint16_t)(strlen(str) * 6 * textSize);
    uint16_t h = (uint16_t)(8 * textSize);
    display.fillRect(x, y, w, h, bgColor);
    display.setCursor(x, y);
    display.setTextColor(foreColor);
    display.setTextSize(textSize);
    display.print(str);
}

void updateScore(int score)
{
    char buffer[5];
    snprintf(buffer, sizeof(buffer), scoreFormat, score);
    drawBoxedString(DISP_W - 50, 6, buffer, 2, YELLOW, PRIMARY_DARK);
}

// ── Screen helpers ────────────────────────────────────────────────────────────

void clearScreen()
{
    display.fillRect(0, 0, DISP_W, DISP_H, backgroundColor);
    display.fillRect(0, 0, DISP_W, SCORE_SIZE, PRIMARY_DARK);
}

void touchToStart()
{
    drawBoxedString(0, 200, "   BREAKOUT",      3, YELLOW, BLACK);
    drawBoxedString(0, 240, "  TOUCH TO START", 2, RED,    BLACK);
    display.flush();
    while (waitForTouch() < 0) delay(20);
}

void gameOverTouchToStart()
{
    drawBoxedString(0, 180, "  GAME OVER",      3, YELLOW, BLACK);
    drawBoxedString(0, 220, "  TOUCH TO START", 2, RED,    BLACK);
    display.flush();
    while (waitForTouch() < 0) delay(20);
}

// ── Redraw ────────────────────────────────────────────────────────────────────
// Reconstructs full display from current game state after reconnect.

void redrawFullScreen()
{
    clearScreen();
    updateLives(game->lives, state.remainingLives);
    updateScore(state.score);

    int colors[] = {RED, RED, BLUE, BLUE, YELLOW, YELLOW, GREEN, GREEN};
    for (int i = 0; i < game->rows; i++)
        for (int j = 0; j < game->columns; j++)
            if (isBrickIn(state.wallState, j, i))
                drawBrick(&state, j, i, colors[i]);

    drawBall((int16_t)(state.ballx >> game->exponent),
             (int16_t)(state.bally >> game->exponent),
             (int16_t)(state.ballx >> game->exponent),
             (int16_t)(state.bally >> game->exponent),
             game->ballsize);
    drawPlayer(game, &state);
    display.flush();
}

// ── Brick helpers ─────────────────────────────────────────────────────────────
void    setBrick(int wall[], uint8_t x, uint8_t y)   { wall[y] |=  BIT_MASK[x]; }
void    unsetBrick(int wall[], uint8_t x, uint8_t y) { wall[y] &= ~BIT_MASK[x]; }
boolean isBrickIn(int wall[], uint8_t x, uint8_t y)  { return wall[y] & BIT_MASK[x]; }

// ── Touch ─────────────────────────────────────────────────────────────────────

void readUiSelection(game_type* g, game_state_type* s)
{
    if (_autoPlay) {
        s->playerx = (s->ballx >> g->exponent) - g->playerwidth / 2;
        if (s->playerx >= DISP_W - g->playerwidth) s->playerx = DISP_W - g->playerwidth;
        if (s->playerx < 0) s->playerx = 0;
        return;
    }

    // Drain touch queue — use last (newest) point for paddle position
    TSPoint tp;
    bool    touched = false;
    while (ts.available()) {
        TSPoint p = ts.getQueuedPoint();
        if (p.z > RemoteTouchScreen::MINPRESSURE) { tp = p; touched = true; }
    }
    if (touched) {
        int16_t newX = tp.x - g->playerwidth / 2;
        if (newX < 0) newX = 0;
        if (newX >= DISP_W - g->playerwidth) newX = DISP_W - g->playerwidth;
        s->playerx = newX;
    }
}

int waitForTouch()
{
    if (_paused)     return -1;   // disconnected — keep waiting
    if (_drawPending) return 1;   // reconnected — exit wait loop
    if (_autoPlay) {
        static uint32_t autoStart = 0;
        if (autoStart == 0) autoStart = millis();
        if (millis() - autoStart >= 2000) { autoStart = 0; return 1; }
        return -1;
    }
    while (ts.available()) {
        TSPoint p = ts.getQueuedPoint();
        if (p.z > RemoteTouchScreen::MINPRESSURE) return 1;
    }
    return -1;
}
