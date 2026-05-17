// Breakout2 — optimised Breakout for ESP32PhoneDisplay
//
// Demonstrates the improvements over the baseline Breakout port:
//
//   Fixed frame rate — millis()-based 30fps budget keeps ball speed
//     consistent regardless of BLE jitter or variable loop time.
//
//   Fixed-point physics — ball position in sub-pixel fixed-point,
//     speed scales cleanly with score without floating-point overhead.
//
//   Brick flash state machine — WHITE→BLUE→gone across natural frame
//     boundaries. No blocking delays in the game loop.
//
//   Touch queue draining — newest paddle position used each frame.
//     Queue draining prevents lag buildup when BLE is briefly busy.
//
//   Ball position clamping — ball clamped to brick face on collision,
//     preventing the erase notch artifact seen in the baseline.
//
//   Display availability — game pauses cleanly when phone locks, app
//     switches, or display goes offline. Resumes without restart.
//
//   Session management — begin()/setTitle()/setButton only re-sent
//     on true BLE reconnect, not on every display redraw.
//
//   Full redraw on reconnect — game state reconstructed on screen
//     after BLE drop without restarting the game.
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
#define BLE_INTERVAL_MAX_MS  15    // BLE connection interval max (ms)
#define DEBUG                0     // 1 = frame overrun prints + BC stats

// ── Colours ───────────────────────────────────────────────────────────────────
#define BLACK           0x0000
#define BLUE            0x001F
#define RED             0xF800
#define GREEN           0x07E0
#define CYAN            0x07FF
#define YELLOW          0xFFE0
#define WHITE           0xFFFF
#define PRIMARY_DARK    0x4016

// ── Display ───────────────────────────────────────────────────────────────────
#define DISP_W      240
#define DISP_H      320
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
// WHITE→BLUE→gone across two natural frame boundaries.
// Ball physics pause while flash is active — prevents double-hit artifacts.
enum FlashState { FLASH_NONE, FLASH_WHITE, FLASH_BLUE };
struct BrickFlash {
    FlashState state = FLASH_NONE;
    int x = 0, y = 0, score = 0;
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
void initDisplay();
void redrawFullScreen();
void setBrick(int[], uint8_t, uint8_t);
void unsetBrick(int[], uint8_t, uint8_t);
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
BrickFlash      flash;

const uint8_t BIT_MASK[]     = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
uint8_t       pointsForRow[] = {7,7,5,5,3,3,1,1};
char          scoreFormat[]  = "%04d";

// ── Volatile flags — set on NimBLE task (core 0), read on loop task (core 1) ─
// Never call display functions or Serial from a BLE callback — set flags only.
static volatile bool  _displayOffline = false;  // pause game loop when display unavailable
static volatile bool  _redrawPending  = false;  // rebuild display from current game state
static volatile bool  _displayReset   = true;   // re-send begin()/setTitle()/buttons on next draw
static volatile bool  _autoPlay       = false;
static volatile int   _autoPlayMsg    = 0;       // 1=ON 2=OFF — printed from loop()
static volatile float _connIntervalMs = 0;       // printed from loop()

// ── setup() ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("[Breakout2] Booting...");

    transport.setConnectionInterval(BLE_INTERVAL_MIN_MS, BLE_INTERVAL_MAX_MS);

    transport.onConnInterval([](float ms) { _connIntervalMs = ms; });

    transport.onKey([](uint8_t key) {
        if      (key == '1') { _autoPlay = true;  _autoPlayMsg = 1; }
        else if (key == '2') { _autoPlay = false; _autoPlayMsg = 2; }
    });

    // onDisplayAvailable — primary signal for display ready/offline.
    // available=true fires ~100ms after connect and on app foreground return.
    transport.onDisplayAvailable([](bool available) {
        if (available) { _displayOffline = false; _redrawPending = true; }
        else             _displayOffline = true;
    });

    // onRedrawRequest — display state stale, rebuild from current game state.
    // Fires when app returns from background (alongside onDisplayAvailable).
    transport.onRedrawRequest([]() { _redrawPending = true; });

    // onConnected / onDisconnected — transport-level fallback.
    // onDisconnected sets _displayReset so session is re-established on reconnect.
    transport.onConnected([]() {
        _displayOffline = false; _redrawPending = true;
    });
    transport.onDisconnected([]() {
        _displayOffline = true;
        _displayReset   = true;  // session lost — begin()/setTitle() needed on next draw
    });

    transport.begin();
    Serial.println("[BLE] Advertising — waiting for iPhone...");

    while (!_redrawPending) delay(100);
    _redrawPending = false;

    level = 0;
    initDisplay();
    newGame(&games[0], &state);
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop()
{
    uint32_t frameStart = millis();

    // Deferred Serial output — safe on core 1
    if (_autoPlayMsg == 1) { Serial.println("[Game] Auto-play ON");  Serial.flush(); _autoPlayMsg = 0; }
    if (_autoPlayMsg == 2) { Serial.println("[Game] Player mode ON"); Serial.flush(); _autoPlayMsg = 0; }
    if (_connIntervalMs > 0) {
        Serial.printf("[BLE] Interval: %.1fms\n", _connIntervalMs);
        Serial.flush();
        _connIntervalMs = 0;
    }

#if DEBUG
    static uint32_t lastKey1 = 0, lastKey2 = 0;
    auto bcst = transport.bcStats();
    if (bcst.key1 != lastKey1 || bcst.key2 != lastKey2) {
        Serial.printf("[BC] K1=%u K2=%u touch=%u sync=%u overrun=%u invalid=%u unknown=%u\n",
                      bcst.key1, bcst.key2, bcst.touch,
                      bcst.syncErrors, bcst.overruns, bcst.invalidFrames, bcst.unknownCmds);
        lastKey1 = bcst.key1; lastKey2 = bcst.key2;
    }
#endif

    // Display offline — phone locked, app backgrounded, or BT dropped
    if (_displayOffline) { delay(100); return; }

    // Redraw — BLE reconnect or app foreground return
    if (_redrawPending) {
        _redrawPending = false;
        flash.state = FLASH_NONE;
        bool fullRestart = _displayReset;  // true = BLE reconnect, false = app switch/return
        initDisplay();                     // clears _displayReset, sends begin() if needed
        if (fullRestart) {
            // BLE reconnected — restart game from scratch in player mode
            Serial.println("[Game] Reconnected — restarting");
            Serial.flush();
            _autoPlay   = false;   // always start in player mode after reconnect
            state.score = 0;
            level       = 0;
            newGame(&games[0], &state);   // shows "TOUCH TO START"
        } else {
            // App returned from background — resume game from current state
            Serial.println("[Game] Display available — resuming");
            Serial.flush();
            redrawFullScreen();
        }
        return;
    }

    // 1. Read touch — before any BLE sends
    state.playerxold = state.playerx;
    readUiSelection(game, &state);

    // 2. Brick flash state machine
    if (flash.state != FLASH_NONE) {
        if (flash.state == FLASH_WHITE) {
            drawBrick(&state, flash.x, flash.y, BLUE);
            flash.state = FLASH_BLUE;
        } else {
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
    drawBall((int16_t)(state.ballx >> game->exponent),
             (int16_t)(state.bally >> game->exponent),
             (int16_t)(state.ballxold >> game->exponent),
             (int16_t)(state.ballyold >> game->exponent),
             game->ballsize);
    drawPlayer(game, &state);
    state.playerxold = state.playerx;
    state.ballxold   = state.ballx;
    state.ballyold   = state.bally;

    // 5. Flush frame
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

    // 7. Frame budget — yield to FreeRTOS during wait so drain task gets CPU
#if DEBUG
    uint32_t elapsed = millis() - frameStart;
    if (elapsed > FRAME_MS) {
        Serial.printf("[Frame] overrun: %ums\n", elapsed);
        Serial.flush();
    }
#endif
    while (millis() - frameStart < FRAME_MS) delay(1);
}

// ── Display session ───────────────────────────────────────────────────────────

void initDisplay()
{
    // begin()/setTitle()/setButton only sent on new BLE session.
    // On display available after app switch, session is intact — skip these.
    if (_displayReset) {
        _displayReset = false;
        display.begin(DISP_W, DISP_H);
        display.setTitle("Breakout 2");
        display.setButton1("Auto");    // T1 — autoplay
        display.setButton2("Player");  // T2 — player control
        ts.begin(TOUCH_MODE_RESISTIVE, TOUCH_INTERVAL_MS);
    }
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

    // Clamp ball Y to brick face — prevents ball sitting 1-2px inside the
    // brick in fixed-point space. Without clamping, drawBall() erases from
    // the penetrating position next frame, notching the adjacent brick.
    // s->vely is pre-reversal here so its sign tells us which face was hit.
    if (s->vely < 0) {
        // Ball moving up — hit bottom face of this row
        s->bally = (int32_t)(s->walltop + (yRow + 1) * s->brickheight) << game->exponent;
    } else if (s->vely > 0) {
        // Ball moving down — hit top face of this row
        s->bally = (int32_t)(s->walltop + yRow * s->brickheight - game->ballsize) << game->exponent;
    }

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
    if (x + g->ballsize >= DISP_W) {
        s->velx = -abs(s->velx);
        s->ballx = (int32_t)(DISP_W - g->ballsize - 1) << g->exponent;
    }
    if (x < 0) {
        s->velx = abs(s->velx);
        s->ballx = 0;
    }
    if (y <= SCORE_SIZE) {
        s->vely = abs(s->vely);
        s->bally = (int32_t)(SCORE_SIZE + 1) << g->exponent;
    }
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

void drawBoxedString(int16_t x, int16_t y, const char* str,
                     uint8_t textSize, uint16_t foreColor, uint16_t bgColor)
{
    display.setTextSize(textSize);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(str, x, y, &x1, &y1, &w, &h);
    display.fillRect(x, y, w, h, bgColor);
    display.setCursor(x, y);
    display.setTextColor(foreColor);
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
    drawBoxedString(0, 200, "   BREAKOUT 2",    3, YELLOW, BLACK);
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

// ── Redraw — reconstruct display from current game state ──────────────────────

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
    // Drain queue — use newest point only for paddle position.
    // Without draining, queued points introduce lag as they're processed
    // one per frame while new ones keep arriving.
    TSPoint tp;
    bool touched = false;
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
    if (_displayOffline)                 return -1;  // lost connection — keep waiting
    if (_redrawPending && _displayReset) return 1;   // BLE reconnected — exit to restart game
    // Note: _redrawPending alone (from onRedrawRequest) does not exit here —
    // that's a display refresh request, not a reconnect requiring game restart.
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
