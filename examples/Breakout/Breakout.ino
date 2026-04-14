// Breakout — classic breakout game ported to ESP32PhoneDisplay
//
// Original sketch by Enrique Albertos (public domain)
// Ported to ESP32PhoneDisplay using the Compat layer and iPhoneTouchScreen.
//
// Porting changes summary:
//   - Adafruit_TFTLCD + hardware init  → ESP32PhoneDisplay_Compat + transport.begin()
//   - TouchScreen (resistive, ADC)     → iPhoneTouchScreen (back-channel, virtual coords)
//   - ts.getPoint() coordinate mapping → removed (iPhone sends display-space coords directly)
//   - tft.reset() / readID() / begin() → transport.begin() + tft.begin()
//   - Everything else unchanged

#include <Arduino.h>
#include <ESP32PhoneDisplay_Compat.h>
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
#define PRIMARY_COLOR       0x4A11
#define PRIMARY_LIGHT_COLOR 0x7A17
#define PRIMARY_DARK_COLOR  0x4016
#define PRIMARY_TEXT_COLOR  0x7FFF

// ── Display ───────────────────────────────────────────────────────────────────
#define DISP_W  240
#define DISP_H  320

// ── Demo mode ─────────────────────────────────────────────────────────────────
// When DEMO_MODE is defined the paddle tracks the ball automatically.
// Useful for testing rendering without needing working touch input.
// Comment out to play with touch control.
#define DEMO_MODE

BleTransport              transport;
ESP32PhoneDisplay_Compat  tft(transport, DISP_W, DISP_H);
iPhoneTouchScreen         ts;

// ── Game types — must be defined before forward declarations ──────────────────
#define SCORE_SIZE   30
#define GAMES_NUMBER 16

char scoreFormat[] = "%04d";

typedef struct {
  int16_t x, y, width, height;
} gameSize_type;

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
void    newGame(game_type* newGame, game_state_type* state);
void    setupState(game_type* game, game_state_type* state);
void    setupStateSizes(game_type* game, game_state_type* state);
void    setupWall(game_type* game, game_state_type* state);
void    drawBrick(game_state_type* state, int xBrick, int yBrickRow, uint16_t color);
void    drawPlayer(game_type* game, game_state_type* state);
void    drawBall(int x, int y, int xold, int yold, int ballsize);
void    updateLives(int lives, int remainingLives);
void    updateScore(int score);
void    checkBallCollisions(game_type* game, game_state_type* state, uint16_t x, uint16_t y);
void    checkBrickCollision(game_type* game, game_state_type* state, uint16_t x, uint16_t y);
int     checkCornerCollision(game_type* game, game_state_type* state, uint16_t x, uint16_t y);
void    hitBrick(game_state_type* state, int xBrick, int yBrickRow);
void    checkBorderCollision(game_type* game, game_state_type* state, uint16_t x, uint16_t y);
void    checkBallExit(game_type* game, game_state_type* state, uint16_t x, uint16_t y);
boolean noBricks(game_type* game, game_state_type* state);
void    drawBoxedString(uint16_t x, uint16_t y, const char* string,
                        uint16_t fontsize, uint16_t foreColor, uint16_t backgroundColor);
void    clearDialog(void);
void    touchToStart(void);
void    gameOverTouchToStart(void);
int     readUiSelection(game_type* game, game_state_type* state, int16_t lastSelected);
int     waitForTouch(void);
void    setBrick(int wall[], uint8_t x, uint8_t y);
void    unsetBrick(int wall[], uint8_t x, uint8_t y);
boolean isBrickIn(int wall[], uint8_t x, uint8_t y);

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

game_type*      game;
game_state_type state;
gameSize_type   gameSize;
uint16_t        backgroundColor = BLACK;
int             level;
const uint8_t   BIT_MASK[]      = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
uint8_t         pointsForRow[]  = {7,7,5,5,3,3,1,1};

// ── Reconnect flag (set from NimBLE task, consumed in loop) ───────────────────
static volatile bool drawPending = false;

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
uint32_t t = millis();
    while (!Serial && (millis() - t) < 3000) { delay(10); }
    Serial.println("[Breakout] Booting...");

    transport.onTouch([](uint8_t cmd, int16_t x, int16_t y) {
        ts.handleTouch(cmd, x, y);
    });

    transport.onSubscribed([](bool ready) {
        if (ready) drawPending = true;
    });

    transport.begin();
    Serial.println("[BLE] Advertising as \"ESP32-Display\" — waiting for iPhone...");

    while (!drawPending) { delay(100); }
    drawPending = false;

    gameSize = {0, 0, DISP_W, DISP_H};
    level = 0;
    tft.begin();    // send BEGIN command — tells iPhone the virtual display size
    newGame(&games[0], &state);
}

// ── loop() ───────────────────────────────────────────────────────────────────

int selection = -1;

void loop()
{
    if (drawPending) {
        drawPending = false;
        Serial.println("[Breakout] Reconnected — restarting game");
        level = 0;
        state.score = 0;
        tft.begin();    // re-send BEGIN so iPhone re-syncs display dimensions
        newGame(&games[0], &state);
        return;
    }

    selection = readUiSelection(game, &state, selection);
    drawPlayer(game, &state);
    state.playerxold = state.playerx;

    int maxV = (1 << game->exponent) - 1;
    if (abs(state.vely) > maxV) state.vely = maxV * ((state.vely > 0) - (state.vely < 0));
    if (abs(state.velx) > maxV) state.velx = maxV * ((state.velx > 0) - (state.velx < 0));

    state.ballx += state.velx;
    state.bally += state.vely;

    checkBallCollisions(game, &state, state.ballx >> game->exponent, state.bally >> game->exponent);
    checkBallExit(game,      &state, state.ballx >> game->exponent, state.bally >> game->exponent);

    drawBall(state.ballx >> game->exponent, state.bally >> game->exponent,
             state.ballxold >> game->exponent, state.ballyold >> game->exponent,
             game->ballsize);

    state.ballxold = state.ballx;
    state.ballyold = state.bally;

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
    delay(1);  // make sure you don't hit idle task watchdog
    tft.flush();
}

// ── Game functions ────────────────────────────────────────────────────────────

void newGame(game_type* newGame, game_state_type* state)
{
    game = newGame;
    setupState(game, state);
    clearDialog();
    updateLives(game->lives, state->remainingLives);
    updateScore(state->score);
    setupWall(game, state);
    tft.flush();        // send initial board to iPhone before blocking in touchToStart
    touchToStart();
    clearDialog();
    updateLives(game->lives, state->remainingLives);
    updateScore(state->score);
    setupWall(game, state);
    tft.flush();        // send final board state after "touch to start" is dismissed
}

void setupStateSizes(game_type* game, game_state_type* state)
{
    state->bottom      = tft.height() - 30;
    state->brickwidth  = tft.width()  / game->columns;
    state->brickheight = tft.height() / 24;
}

void setupState(game_type* game, game_state_type* state)
{
    setupStateSizes(game, state);
    for (int i = 0; i < game->rows; i++) state->wallState[i] = 0;
    state->playerx        = tft.width() / 2 - game->playerwidth / 2;
    state->remainingLives = game->lives;
    state->bally          = state->bottom << game->exponent;
    state->ballyold       = state->bottom << game->exponent;
    state->velx           = game->initVelx;
    state->vely           = game->initVely;
}

void updateLives(int lives, int remainingLives)
{
    for (int i = 0; i < lives; i++)          tft.fillCircle((1+i)*15, 15, 5, BLACK);
    for (int i = 0; i < remainingLives; i++) tft.fillCircle((1+i)*15, 15, 5, YELLOW);
}

void setupWall(game_type* game, game_state_type* state)
{
    int colors[] = {RED,RED,BLUE,BLUE,YELLOW,YELLOW,GREEN,GREEN};
    state->walltop    = game->top + 40;
    state->wallbottom = state->walltop + game->rows * state->brickheight;
    for (int i = 0; i < game->rows; i++)
        for (int j = 0; j < game->columns; j++)
            if (isBrickIn(game->wall, j, i)) {
                setBrick(state->wallState, j, i);
                drawBrick(state, j, i, colors[i]);
            }
}

void drawBrick(game_state_type* state, int xBrick, int yRow, uint16_t color)
{
    tft.fillRect((state->brickwidth * xBrick) + game->brickGap,
                  state->walltop + (state->brickheight * yRow) + game->brickGap,
                  state->brickwidth  - game->brickGap * 2,
                  state->brickheight - game->brickGap * 2,
                  color);
}

boolean noBricks(game_type* game, game_state_type* state)
{
    for (int i = 0; i < game->rows; i++) if (state->wallState[i]) return false;
    return true;
}

void drawPlayer(game_type* game, game_state_type* state)
{
    tft.fillRect(state->playerx, state->bottom, game->playerwidth, game->playerheight, YELLOW);
    if (state->playerx != state->playerxold) {
        if (state->playerx < state->playerxold)
            tft.fillRect(state->playerx + game->playerwidth, state->bottom,
                         abs(state->playerx - state->playerxold), game->playerheight, backgroundColor);
        else
            tft.fillRect(state->playerxold, state->bottom,
                         abs(state->playerx - state->playerxold), game->playerheight, backgroundColor);
    }
}

void drawBall(int x, int y, int xold, int yold, int ballsize)
{
    if      (xold<=x && yold<=y) { tft.fillRect(xold,yold,ballsize,y-yold,BLACK); tft.fillRect(xold,yold,x-xold,ballsize,BLACK); }
    else if (xold>=x && yold>=y) { tft.fillRect(x+ballsize,yold,xold-x,ballsize,BLACK); tft.fillRect(xold,y+ballsize,ballsize,yold-y,BLACK); }
    else if (xold<=x && yold>=y) { tft.fillRect(xold,yold,x-xold,ballsize,BLACK); tft.fillRect(xold,y+ballsize,ballsize,yold-y,BLACK); }
    else                         { tft.fillRect(xold,yold,ballsize,y-yold,BLACK); tft.fillRect(x+ballsize,yold,xold-x,ballsize,BLACK); }
    tft.fillRect(x, y, ballsize, ballsize, YELLOW);
}

void touchToStart()
{
    drawBoxedString(0, 200, "   BREAKOUT",      3, YELLOW, BLACK);
    drawBoxedString(0, 240, "  TOUCH TO START", 2, RED,    BLACK);
    tft.flush();
    while (waitForTouch() < 0) { delay(20); }
}

void gameOverTouchToStart()
{
    drawBoxedString(0, 180, "  GAME OVER",      3, YELLOW, BLACK);
    drawBoxedString(0, 220, "  TOUCH TO START", 2, RED,    BLACK);
    tft.flush();
    while (waitForTouch() < 0) { delay(20); }
}

void updateScore(int score)
{
    char buffer[5];
    snprintf(buffer, sizeof(buffer), scoreFormat, score);
    drawBoxedString(tft.width() - 50, 6, buffer, 2, YELLOW, PRIMARY_DARK_COLOR);
}

void checkBrickCollision(game_type* game, game_state_type* state, uint16_t x, uint16_t y)
{
    int x1 = x + game->ballsize, y1 = y + game->ballsize;
    int hits = checkCornerCollision(game,state,x, y)
             + checkCornerCollision(game,state,x1,y1)
             + checkCornerCollision(game,state,x, y1)
             + checkCornerCollision(game,state,x1,y);
    if (hits > 0) {
        state->vely = -state->vely;
        if (((x % state->brickwidth == 0) && state->velx < 0) ||
            (((x + game->ballsize) % state->brickwidth == 0) && state->velx > 0))
            state->velx = -state->velx;
    }
}

int checkCornerCollision(game_type* game, game_state_type* state, uint16_t x, uint16_t y)
{
    if (y > state->walltop && y < state->wallbottom) {
        int yRow = (y - state->walltop) / state->brickheight;
        int xCol = x / state->brickwidth;
        if (isBrickIn(state->wallState, xCol, yRow)) { hitBrick(state, xCol, yRow); return 1; }
    }
    return 0;
}

void hitBrick(game_state_type* state, int xBrick, int yRow)
{
    state->score += pointsForRow[yRow];
    drawBrick(state, xBrick, yRow, WHITE); tft.flush(); delay(16);
    drawBrick(state, xBrick, yRow, BLUE);  tft.flush(); delay(8);
    drawBrick(state, xBrick, yRow, backgroundColor);
    unsetBrick(state->wallState, xBrick, yRow);
    updateScore(state->score);
}

void checkBorderCollision(game_type* game, game_state_type* state, uint16_t x, uint16_t y)
{
    if (x + game->ballsize >= (uint16_t)tft.width())  state->velx = -abs(state->velx);
    if (x == 0)                                        state->velx =  abs(state->velx);
    if (y <= SCORE_SIZE)                               state->vely =  abs(state->vely);
    if ((y + game->ballsize) >= (uint16_t)state->bottom
        && (y + game->ballsize) <= (uint16_t)(state->bottom + game->playerheight)
        && x >= (uint16_t)state->playerx
        && x <= (uint16_t)(state->playerx + game->playerwidth)) {
        if      (x > (uint16_t)(state->playerx + game->playerwidth - 6)) state->velx--;
        else if (x < (uint16_t)(state->playerx + 6))                     state->velx++;
        state->vely = -abs(state->vely);
    }
}

void checkBallCollisions(game_type* game, game_state_type* state, uint16_t x, uint16_t y)
{
    checkBrickCollision(game, state, x, y);
    checkBorderCollision(game, state, x, y);
}

void checkBallExit(game_type* game, game_state_type* state, uint16_t x, uint16_t y)
{
    if ((y + game->ballsize) >= (uint16_t)tft.height()) {
        state->remainingLives--;
        updateLives(game->lives, state->remainingLives);
        tft.flush();
        delay(500);
        state->vely = -abs(state->vely);
    }
}

// ── Brick helpers ─────────────────────────────────────────────────────────────
void    setBrick(int wall[], uint8_t x, uint8_t y)   { wall[y] =  wall[y] |  BIT_MASK[x]; }
void    unsetBrick(int wall[], uint8_t x, uint8_t y) { wall[y] =  wall[y] & ~BIT_MASK[x]; }
boolean isBrickIn(int wall[], uint8_t x, uint8_t y)  { return wall[y] & BIT_MASK[x]; }

// ── Screen helpers ────────────────────────────────────────────────────────────

void drawBoxedString(uint16_t x, uint16_t y, const char* string,
                     uint16_t fontsize, uint16_t foreColor, uint16_t bgColor)
{
    tft.setTextSize(fontsize);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(string, x, y, &x1, &y1, &w, &h);
    tft.fillRect(x, y, w, h, bgColor);
    tft.setCursor(x, y);
    tft.setTextColor(foreColor);
    tft.print(string);
}

void clearDialog()
{
    tft.fillRect(gameSize.x, gameSize.y, gameSize.width, gameSize.height, backgroundColor);
    tft.fillRect(gameSize.x, gameSize.y, gameSize.width, SCORE_SIZE, PRIMARY_DARK_COLOR);
}

// ── Touch input ───────────────────────────────────────────────────────────────
// iPhone sends coordinates already mapped to virtual display space.
// No ADC calibration needed — use p.x and p.y directly.

int readUiSelection(game_type* game, game_state_type* state, int16_t lastSelected)
{
    TSPoint tp = ts.getPoint();
    if (tp.z > iPhoneTouchScreen::MINPRESSURE) {
        state->playerx += (tp.x > tft.width() / 2) ? 4 : -4;
        if (state->playerx >= tft.width() - game->playerwidth)
            state->playerx = tft.width() - game->playerwidth;
        if (state->playerx < 0) state->playerx = 0;
        return 1;
    }
#ifdef DEMO_MODE
    // No touch — auto-track the ball for demo/rendering testing
    state->playerx = (state->ballx >> game->exponent) - game->playerwidth / 2;
    if (state->playerx >= tft.width() - game->playerwidth)
        state->playerx = tft.width() - game->playerwidth;
    if (state->playerx < 0) state->playerx = 0;
#endif
    return -1;
}

int waitForTouch()
{
    TSPoint tp = ts.getPoint();
    // Treat a reconnect as a touch so blocking loops exit cleanly on reconnect
    if (drawPending) return 1;
#ifdef DEMO_MODE
    // In demo mode, auto-advance after 2 seconds so the game starts without touch
    static uint32_t demoWaitStart = 0;
    if (demoWaitStart == 0) demoWaitStart = millis();
    if (millis() - demoWaitStart >= 2000) { demoWaitStart = 0; return 1; }
#endif
    if (tp.z > iPhoneTouchScreen::MINPRESSURE) return 1;
    return -1;
}

