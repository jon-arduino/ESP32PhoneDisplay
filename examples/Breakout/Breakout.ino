// Breakout — classic breakout game, minimal port to ESP32PhoneDisplay
//
// Original sketch by Enrique Albertos (public domain).
// Ported to ESP32PhoneDisplay using the native driver — no compat layer
// needed since we don't pass Adafruit_GFX pointers to any third-party lib.
//
// This is the baseline port — game logic is unchanged from the original.
// No fixed frame rate, no brick flash state machine, no session API.
// See Breakout2 for what can be achieved with those improvements.
//
// Porting changes:
//   - Adafruit_TFTLCD + hardware init → ESP32PhoneDisplay + transport.begin()
//   - TouchScreen (resistive, ADC)    → RemoteTouchScreen (back-channel)
//   - tft.width() / tft.height()      → DISP_W / DISP_H constants
//   - tft.reset() / readID() / begin() → display.begin(DISP_W, DISP_H)
//   - onSubscribed                    → onConnected / onDisconnected
//   - setConnectionInterval(15, 15)   → required for games — iOS default
//     connection interval can be 30-100ms, causing severe display lag.
//     15ms gives ~67fps maximum BLE throughput. Always set for games.
//   - Everything else unchanged

#include <Arduino.h>
#include <ESP32PhoneDisplay.h>
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>

// ── Colours ───────────────────────────────────────────────────────────────────
#define BLACK               0x0000
#define BLUE                0x001F
#define RED                 0xF800
#define GREEN               0x07E0
#define CYAN                0x07FF
#define MAGENTA             0xF81F
#define YELLOW              0xFFE0
#define WHITE               0xFFFF
#define PRIMARY_DARK_COLOR  0x4016

// ── Display ───────────────────────────────────────────────────────────────────
#define DISP_W  240
#define DISP_H  320

// ── Tuning ────────────────────────────────────────────────────────────────────
#define TOUCH_INTERVAL_MS   30    // iPhone MOVE event throttle (ms)
#define DEBUG               0     // 1 = enable loop timing diagnostics

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
    uint16_t ballx, bally, ballxold, ballyold;
    int velx, vely, playerx, playerxold;
    int wallState[8];
    int score, remainingLives, top, bottom, walltop, wallbottom, brickheight, brickwidth;
} game_state_type;

// ── Forward declarations ──────────────────────────────────────────────────────
void    newGame(game_type*, game_state_type*);
void    setupState(game_type*, game_state_type*);
void    setupStateSizes(game_type*, game_state_type*);
void    setupWall(game_type*, game_state_type*);
void    drawBrick(game_state_type*, int, int, uint16_t);
void    drawPlayer(game_type*, game_state_type*);
void    drawBall(int, int, int, int, int);
void    updateLives(int, int);
void    updateScore(int);
void    checkBallCollisions(game_type*, game_state_type*, uint16_t, uint16_t);
void    checkBrickCollision(game_type*, game_state_type*, uint16_t, uint16_t);
int     checkCornerCollision(game_type*, game_state_type*, uint16_t, uint16_t);
void    hitBrick(game_state_type*, int, int);
void    checkBorderCollision(game_type*, game_state_type*, uint16_t, uint16_t);
void    checkBallExit(game_type*, game_state_type*, uint16_t, uint16_t);
boolean noBricks(game_type*, game_state_type*);
void    drawBoxedString(uint16_t, uint16_t, const char*, uint16_t, uint16_t, uint16_t);
void    clearScreen();
void    touchToStart();
void    gameOverTouchToStart();
int     readUiSelection(game_type*, game_state_type*);
int     waitForTouch();
void    setBrick(int[], uint8_t, uint8_t);
void    unsetBrick(int[], uint8_t, uint8_t);
boolean isBrickIn(int[], uint8_t, uint8_t);

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
char            scoreFormat[]   = "%04d";
const uint8_t   BIT_MASK[]      = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
uint8_t         pointsForRow[]  = {7,7,5,5,3,3,1,1};

// ── Volatile flags — set on NimBLE task (core 0), read on loop task (core 1) ─
static volatile bool _drawPending = false;
static volatile bool _autoPlay    = false;
static volatile int  _autoPlayMsg = 0;   // 1=ON, 2=OFF — printed from loop()

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("[Breakout] Booting...");

    // onKey — T1=autoplay, T2=player. Serial deferred to loop() — unsafe on core 0.
    transport.onKey([](uint8_t key) {
        if      (key == '1') { _autoPlay = true;  _autoPlayMsg = 1; }
        else if (key == '2') { _autoPlay = false; _autoPlayMsg = 2; }
    });

    transport.onConnected([]() {
        _drawPending = true;
    });

    transport.onDisconnected([]() {
        _drawPending = false;
    });

    // Request 15ms BLE connection interval — essential for games.
    // iOS default can be 30-100ms, causing severe display lag.
    // Must be called before transport.begin().
    transport.setConnectionInterval(15, 15);

    transport.begin();
    Serial.println("[BLE] Advertising — waiting for iPhone...");

    while (!_drawPending) delay(100);
    _drawPending = false;

    level = 0;
    display.begin(DISP_W, DISP_H);
    ts.begin(TOUCH_MODE_RESISTIVE, TOUCH_INTERVAL_MS);
    newGame(&games[0], &state);
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    // Print deferred messages — safe here on core 1
    if (_autoPlayMsg == 1) { Serial.println("[Game] Auto-play ON");  Serial.flush(); _autoPlayMsg = 0; }
    if (_autoPlayMsg == 2) { Serial.println("[Game] Player mode ON"); Serial.flush(); _autoPlayMsg = 0; }

    // Reconnect — restart game from beginning
    if (_drawPending) {
        _drawPending = false;
        Serial.println("[Breakout] Reconnected — restarting");
        Serial.flush();
        level = 0;
        state.score = 0;
        display.begin(DISP_W, DISP_H);
        ts.begin(TOUCH_MODE_RESISTIVE, TOUCH_INTERVAL_MS);
        newGame(&games[0], &state);
        return;
    }

    state.playerxold = state.playerx;
    readUiSelection(game, &state);

    int maxV = (1 << game->exponent) - 1;
    if (abs(state.vely) > maxV) state.vely = maxV * ((state.vely > 0) - (state.vely < 0));
    if (abs(state.velx) > maxV) state.velx = maxV * ((state.velx > 0) - (state.velx < 0));

    state.ballx += state.velx;
    state.bally += state.vely;

    checkBallCollisions(game, &state,
                        state.ballx >> game->exponent,
                        state.bally >> game->exponent);
    checkBallExit(game, &state,
                  state.ballx >> game->exponent,
                  state.bally >> game->exponent);

    drawBall(state.ballx >> game->exponent, state.bally >> game->exponent,
             state.ballxold >> game->exponent, state.ballyold >> game->exponent,
             game->ballsize);
    drawPlayer(game, &state);
    state.playerxold = state.playerx;
    state.ballxold   = state.ballx;
    state.ballyold   = state.bally;

    state.velx = (20 + (state.score >> 3)) * ((state.velx > 0) - (state.velx < 0));
    state.vely = (20 + (state.score >> 3)) * ((state.vely > 0) - (state.vely < 0));

    if (noBricks(game, &state) && level < GAMES_NUMBER - 1) {
        level++;
        newGame(&games[level], &state);
    } else if (state.remainingLives <= 0) {
        gameOverTouchToStart();
        state.score = 0;
        level = 0;
        newGame(&games[0], &state);
    }

    delay(5);
    display.flush();
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
}

void updateLives(int lives, int remaining)
{
    for (int i = 0; i < lives;     i++) display.fillCircle((1+i)*15, 15, 5, BLACK);
    for (int i = 0; i < remaining; i++) display.fillCircle((1+i)*15, 15, 5, YELLOW);
}

void setupWall(game_type* g, game_state_type* s)
{
    int colors[] = {RED,RED,BLUE,BLUE,YELLOW,YELLOW,GREEN,GREEN};
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
    else
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

void hitBrick(game_state_type* s, int xBrick, int yRow)
{
    s->score += pointsForRow[yRow];
    drawBrick(s, xBrick, yRow, WHITE);
    drawBrick(s, xBrick, yRow, BLUE);
    drawBrick(s, xBrick, yRow, backgroundColor);
    unsetBrick(s->wallState, xBrick, yRow);
    updateScore(s->score);
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

void updateScore(int score)
{
    char buffer[5];
    snprintf(buffer, sizeof(buffer), scoreFormat, score);
    drawBoxedString(DISP_W - 50, 6, buffer, 2, YELLOW, PRIMARY_DARK_COLOR);
}

void checkBrickCollision(game_type* g, game_state_type* s, uint16_t x, uint16_t y)
{
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

int checkCornerCollision(game_type* g, game_state_type* s, uint16_t x, uint16_t y)
{
    if (y > (uint16_t)s->walltop && y < (uint16_t)s->wallbottom) {
        int yRow = (y - s->walltop) / s->brickheight;
        int xCol = x / s->brickwidth;
        if (isBrickIn(s->wallState, xCol, yRow)) { hitBrick(s, xCol, yRow); return 1; }
    }
    return 0;
}

void checkBorderCollision(game_type* g, game_state_type* s, uint16_t x, uint16_t y)
{
    if (x + g->ballsize >= DISP_W)   s->velx = -abs(s->velx);
    if (x == 0)                       s->velx =  abs(s->velx);
    if (y <= SCORE_SIZE)              s->vely =  abs(s->vely);
    if ((y + g->ballsize) >= (uint16_t)s->bottom
        && (y + g->ballsize) <= (uint16_t)(s->bottom + g->playerheight)
        && x >= (uint16_t)s->playerx
        && x <= (uint16_t)(s->playerx + g->playerwidth)) {
        if      (x > (uint16_t)(s->playerx + g->playerwidth - 6)) s->velx--;
        else if (x < (uint16_t)(s->playerx + 6))                  s->velx++;
        s->vely = -abs(s->vely);
    }
}

void checkBallCollisions(game_type* g, game_state_type* s, uint16_t x, uint16_t y)
{
    checkBrickCollision(g, s, x, y);
    checkBorderCollision(g, s, x, y);
}

void checkBallExit(game_type* g, game_state_type* s, uint16_t x, uint16_t y)
{
    if ((y + g->ballsize) >= DISP_H) {
        s->remainingLives--;
        updateLives(g->lives, s->remainingLives);
        display.flush();
        delay(500);
        s->vely = -abs(s->vely);
    }
}

// ── Brick helpers ─────────────────────────────────────────────────────────────
void    setBrick(int wall[], uint8_t x, uint8_t y)   { wall[y] =  wall[y] |  BIT_MASK[x]; }
void    unsetBrick(int wall[], uint8_t x, uint8_t y) { wall[y] =  wall[y] & ~BIT_MASK[x]; }
boolean isBrickIn(int wall[], uint8_t x, uint8_t y)  { return wall[y] & BIT_MASK[x]; }

// ── Screen helpers ────────────────────────────────────────────────────────────

// drawBoxedString — clears a background rect then draws text.
// Uses display.getTextBounds() for accurate sizing — pure local math,
// no BLE commands sent.
void drawBoxedString(uint16_t x, uint16_t y, const char* str,
                     uint16_t fontsize, uint16_t foreColor, uint16_t bgColor)
{
    display.setTextSize(fontsize);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(str, x, y, &x1, &y1, &w, &h);
    display.fillRect(x, y, w, h, bgColor);
    display.setCursor(x, y);
    display.setTextColor(foreColor);
    display.print(str);
}

void clearScreen()
{
    display.fillScreen(backgroundColor);
    display.fillRect(0, 0, DISP_W, SCORE_SIZE, PRIMARY_DARK_COLOR);
}

// ── Touch input ───────────────────────────────────────────────────────────────

int readUiSelection(game_type* g, game_state_type* s)
{
    if (_autoPlay) {
        s->playerx = (s->ballx >> g->exponent) - g->playerwidth / 2;
        if (s->playerx >= DISP_W - g->playerwidth) s->playerx = DISP_W - g->playerwidth;
        if (s->playerx < 0) s->playerx = 0;
        return 1;
    }
    TSPoint tp = ts.getPoint();
    if (tp.z > RemoteTouchScreen::MINPRESSURE) {
        int16_t newX = tp.x - g->playerwidth / 2;
        if (newX < 0) newX = 0;
        if (newX >= DISP_W - g->playerwidth) newX = DISP_W - g->playerwidth;
        s->playerx = newX;
        return 1;
    }
    return -1;
}

int waitForTouch()
{
    if (_drawPending) return 1;
    if (_autoPlay) {
        static uint32_t autoWaitStart = 0;
        if (autoWaitStart == 0) autoWaitStart = millis();
        if (millis() - autoWaitStart >= 2000) { autoWaitStart = 0; return 1; }
        return -1;
    }
    while (ts.available()) {
        TSPoint p = ts.getQueuedPoint();
        if (p.z > RemoteTouchScreen::MINPRESSURE) return 1;
    }
    return -1;
}
