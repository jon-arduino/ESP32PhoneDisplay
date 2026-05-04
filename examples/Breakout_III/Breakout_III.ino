// Breakout_II — fixed frame rate Breakout for ESP32PhoneDisplay
//
// Improvements over Breakout:
//   - Fixed 30fps frame rate via millis()-based frame budget
//   - Ball speed is pixels-per-frame (consistent regardless of BLE jitter)
//   - Brick flash uses frame-state machine — no blocking delays
//   - Ball pauses during brick flash (2 frames = 66ms)
//   - Touch and physics decoupled from BLE timing
//   - T1 = autoplay, T2 = player control
//
// Original game by Enrique Albertos (public domain)

#include <Arduino.h>
#include <ESP32PhoneDisplay_Compat.h>
#include <transport/BleTransport.h>
#include <touch/RemoteTouchScreen.h>


// ── Tuning ────────────────────────────────────────────────────────────────────
#define FRAME_MS             33    // target frame time (~30fps)
#define SPEED_MULTIPLIER     6.0f  // multiply ball speed — tune until it feels right
#define TOUCH_INTERVAL_MS    30    // iPhone MOVE event rate
#define BLE_INTERVAL_MIN_MS  15    // BLE connection interval min
#define BLE_INTERVAL_MAX_MS  30    // BLE connection interval max
#define DEBUG                false // set true for loop timing diagnostics

// ── Colours ───────────────────────────────────────────────────────────────────
#define BLACK    0x0000
#define BLUE     0x001F
#define RED      0xF800
#define GREEN    0x07E0
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0
#define WHITE    0xFFFF
#define PRIMARY_DARK_COLOR  0x4016

// ── Display ───────────────────────────────────────────────────────────────────
#define DISP_W  240
#define DISP_H  320

// ── Game constants ────────────────────────────────────────────────────────────
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
    int32_t  ballx, bally, ballxold, ballyold;  // fixed-point, signed for correct wrap handling
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
void drawBoxedString(uint16_t, uint16_t, const char*, uint16_t, uint16_t, uint16_t);
void clearDialog();
void touchToStart();
void gameOverTouchToStart();
int  readUiSelection(game_type*, game_state_type*, int16_t);
int  waitForTouch();
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
BleTransport              transport;
ESP32PhoneDisplay_Compat  tft(transport, DISP_W, DISP_H);
RemoteTouchScreen         ts(transport);

// ── State ─────────────────────────────────────────────────────────────────────
game_type*      game;
game_state_type state;
gameSize_type   gameSize;
uint16_t        backgroundColor = BLACK;
int             level;
BrickFlash      flash;

const uint8_t BIT_MASK[]     = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
uint8_t       pointsForRow[] = {7,7,5,5,3,3,1,1};

static volatile bool  drawPending    = false;
static volatile bool  _autoPlay      = false;
static volatile bool  _paused        = false;  // true while disconnected
static volatile int   _autoPlayMsg   = 0;    // 1=ON, 2=OFF — printed from loop()
static volatile float _connIntervalMs = 0;   // >0 = new interval to print from loop()
static volatile uint32_t _appKey1 = 0;       // incremented in onKey callback
static volatile uint32_t _appKey2 = 0;


// ── setup() ───────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);
    Serial.println("[Breakout_II] Booting...");

    transport.setConnectionInterval(BLE_INTERVAL_MIN_MS, BLE_INTERVAL_MAX_MS);

    transport.onConnInterval([](float ms) {
        _connIntervalMs = ms;   // printed from loop() to avoid dual-core Serial interleave
    });

    transport.onKey([](uint8_t key) {
        if      (key == '1') { _autoPlay = true;  _autoPlayMsg = 1; _appKey1++; }
        else if (key == '2') { _autoPlay = false; _autoPlayMsg = 2; _appKey2++; }
    });

    // Pause immediately on disconnect — stops physics and drawing
    transport.onSubscribed([](bool ready) {
        if (ready) {
            // Fallback for iPhone app versions that don't send BC_CMD_REDRAW_REQUEST.
            // When app is updated, onRedrawRequest fires ~100ms after connect
            // and also sets drawPending — whichever fires first wins.
            _paused     = false;
            drawPending = true;
        } else {
            _paused = true;
        }
    });

    // iPhone requests full redraw ~100ms after reconnect (new app versions)
    transport.onRedrawRequest([]() {
        _paused     = false;
        drawPending = true;
    });

    transport.begin();
    Serial.println("[Game] Waiting for iPhone...");

    while (!drawPending) delay(100);
    drawPending = false;

    gameSize = {0, 0, DISP_W, DISP_H};
    level = 0;
    tft.begin();
    tft.setTitle("Breakout II");
    tft.setButton1("Auto");    // T1 = autoplay mode
    tft.setButton2("Player");  // T2 = player mode
    ts.begin(TOUCH_MODE_RESISTIVE, TOUCH_INTERVAL_MS);
    newGame(&games[0], &state);
}

// ── loop() ────────────────────────────────────────────────────────────────────

int selection = -1;

void loop()
{
    uint32_t frameStart = millis();

    // Print messages from loop() — avoids dual-core Serial interleave
    if (_autoPlayMsg == 1) { Serial.println("[Game] Auto-play ON");  _autoPlayMsg = 0; }
    if (_autoPlayMsg == 2) { Serial.println("[Game] Player mode ON"); _autoPlayMsg = 0; }
    if (_connIntervalMs > 0) { Serial.printf("[BLE] Interval: %.1fms\n", _connIntervalMs); _connIntervalMs = 0; }

    // Print BC stats when key counts change.
    // bc counts (s.key1/2) = incremented in dispatch() on core 0.
    // app counts (_appKey1/2) = incremented in _keyCallback on core 0,
    //   same call stack as dispatch(). Should ALWAYS match bc counts.
    //   A mismatch means _keyCallback didn't fire despite dispatch() running.
    static uint32_t lastKey1 = 0, lastKey2 = 0;
    auto s = transport.bcStats();
    if (s.key1 != lastKey1 || s.key2 != lastKey2) {
        bool mismatch = (s.key1 != (uint32_t)_appKey1 || s.key2 != (uint32_t)_appKey2);
        Serial.printf("[BC] K1=%u/%u K2=%u/%u touch=%u sync=%u overrun=%u invalid=%u unknown=%u%s\n",
                      s.key1, (uint32_t)_appKey1, s.key2, (uint32_t)_appKey2,
                      s.touch, s.syncErrors, s.overruns, s.invalidFrames, s.unknownCmds,
                      mismatch ? " *** CB MISMATCH ***" : "");
        lastKey1 = s.key1;
        lastKey2 = s.key2;
    }

    // Paused — connection lost, spin quietly until reconnect
    if (_paused) {
        delay(100);
        return;
    }

    // Redraw requested by iPhone after reconnect
    if (drawPending) {
        drawPending = false;
        Serial.println("[Game] Reconnected — redrawing");
        flash.state = FLASH_NONE;
        tft.begin();
        tft.setTitle("Breakout II");
        tft.setButton1("Auto");
        tft.setButton2("Player");
        ts.begin(TOUCH_MODE_RESISTIVE, TOUCH_INTERVAL_MS);
        redrawFullScreen();
        return;
    }

    // 1. Read touch — always first, before any BLE sends
    state.playerxold = state.playerx;
    selection = readUiSelection(game, &state, selection);

    // 2. Advance brick flash state machine
    if (flash.state != FLASH_NONE) {
        if (flash.state == FLASH_WHITE) {
            drawBrick(&state, flash.x, flash.y, BLUE);
            flash.state = FLASH_BLUE;
        } else {
            // FLASH_BLUE — remove brick
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

    // 4. Draw ball and player together — batched into one BLE notification
    drawBall((int16_t)(state.ballx >> game->exponent), (int16_t)(state.bally >> game->exponent),
             (int16_t)(state.ballxold >> game->exponent), (int16_t)(state.ballyold >> game->exponent),
             game->ballsize);
    drawPlayer(game, &state);
    state.playerxold = state.playerx;
    state.ballxold   = state.ballx;
    state.ballyold   = state.bally;

    // 5. Flush frame to iPhone
    tft.flush();

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
    // delay(1) inside loop yields to BLE stack and IDLE task
    if (DEBUG) {
        uint32_t elapsed = millis() - frameStart;
        if (elapsed > FRAME_MS)
            Serial.printf("[Frame] overrun: %ums\n", elapsed);
    }
    while (millis() - frameStart < FRAME_MS) delay(1);
}

// ── Game functions ────────────────────────────────────────────────────────────

void newGame(game_type* g, game_state_type* s)
{
    game = g;
    setupState(game, s);
    clearDialog();
    updateLives(game->lives, s->remainingLives);
    updateScore(s->score);
    setupWall(game, s);
    tft.flush();
    touchToStart();
    clearDialog();
    updateLives(game->lives, s->remainingLives);
    updateScore(s->score);
    setupWall(game, s);
    tft.flush();
}

void setupStateSizes(game_type* g, game_state_type* s)
{
    s->bottom      = tft.height() - 30;
    s->brickwidth  = tft.width()  / g->columns;
    s->brickheight = tft.height() / 24;
}

void setupState(game_type* g, game_state_type* s)
{
    setupStateSizes(g, s);
    for (int i = 0; i < g->rows; i++) s->wallState[i] = 0;
    s->playerx        = tft.width() / 2 - g->playerwidth / 2;
    s->remainingLives = g->lives;
    s->bally          = s->bottom << g->exponent;
    s->ballyold       = s->bottom << g->exponent;
    s->velx           = g->initVelx;
    s->vely           = g->initVely;
    flash.state       = FLASH_NONE;
}

void updateLives(int lives, int remaining)
{
    for (int i = 0; i < lives;     i++) tft.fillCircle((1+i)*15, 15, 5, BLACK);
    for (int i = 0; i < remaining; i++) tft.fillCircle((1+i)*15, 15, 5, YELLOW);
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
    tft.fillRect((s->brickwidth * xBrick) + game->brickGap,
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
    tft.fillRect(s->playerx, s->bottom, g->playerwidth, g->playerheight, YELLOW);
    if (s->playerx < s->playerxold)
        tft.fillRect(s->playerx + g->playerwidth, s->bottom,
                     abs(s->playerx - s->playerxold), g->playerheight, backgroundColor);
    else if (s->playerx > s->playerxold)
        tft.fillRect(s->playerxold, s->bottom,
                     abs(s->playerx - s->playerxold), g->playerheight, backgroundColor);
}

void drawBall(int x, int y, int xold, int yold, int ballsize)
{
    if      (xold<=x && yold<=y) { tft.fillRect(xold,yold,ballsize,y-yold,BLACK); tft.fillRect(xold,yold,x-xold,ballsize,BLACK); }
    else if (xold>=x && yold>=y) { tft.fillRect(x+ballsize,yold,xold-x,ballsize,BLACK); tft.fillRect(xold,y+ballsize,ballsize,yold-y,BLACK); }
    else if (xold<=x && yold>=y) { tft.fillRect(xold,yold,x-xold,ballsize,BLACK); tft.fillRect(xold,y+ballsize,ballsize,yold-y,BLACK); }
    else                         { tft.fillRect(xold,yold,ballsize,y-yold,BLACK); tft.fillRect(x+ballsize,yold,xold-x,ballsize,BLACK); }
    tft.fillRect(x, y, ballsize, ballsize, YELLOW);
}

// Start flash — called from checkCornerCollision
// Does NOT remove brick or update score yet — flash state machine handles that
void startBrickFlash(game_state_type* s, int xBrick, int yRow)
{
    flash.state = FLASH_WHITE;
    flash.x     = xBrick;
    flash.y     = yRow;
    flash.score = pointsForRow[yRow];
    drawBrick(s, xBrick, yRow, WHITE);  // white this frame
}

void checkBrickCollision(game_type* g, game_state_type* s, int16_t x, int16_t y)
{
    if (flash.state != FLASH_NONE) return;  // already flashing a brick
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
    // Only check if coordinates are within brick wall area
    if (x < 0 || x >= tft.width()) return 0;
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
    int16_t sw = (int16_t)tft.width();

    // Right wall — ball right edge past screen right
    if (x + g->ballsize >= sw) {
        s->velx = -abs(s->velx);
        s->ballx = (int32_t)(sw - g->ballsize - 1) << g->exponent;
    }
    // Left wall — ball left edge past screen left
    if (x < 0) {
        s->velx = abs(s->velx);
        s->ballx = 0;
    }
    // Top wall — ball top edge in score area
    if (y <= SCORE_SIZE) {
        s->vely = abs(s->vely);
        s->bally = (int32_t)(SCORE_SIZE + 1) << g->exponent;
    }
    // Paddle — ball bottom edge overlaps paddle vertically and horizontally
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
    if (y + g->ballsize >= tft.height()) {
        s->remainingLives--;
        updateLives(g->lives, s->remainingLives);
        tft.flush();
        s->vely = -abs(s->vely);
        // Clamp back inside screen
        s->bally = (int32_t)(tft.height() - g->ballsize - 1) << g->exponent;
    }
}

void updateScore(int score)
{
    char buffer[5];
    snprintf(buffer, sizeof(buffer), scoreFormat, score);
    drawBoxedString(tft.width() - 50, 6, buffer, 2, YELLOW, PRIMARY_DARK_COLOR);
}

void touchToStart()
{
    drawBoxedString(0, 200, "   BREAKOUT",      3, YELLOW, BLACK);
    drawBoxedString(0, 240, "  TOUCH TO START", 2, RED,    BLACK);
    tft.flush();
    while (waitForTouch() < 0) delay(20);
}

void gameOverTouchToStart()
{
    drawBoxedString(0, 180, "  GAME OVER",      3, YELLOW, BLACK);
    drawBoxedString(0, 220, "  TOUCH TO START", 2, RED,    BLACK);
    tft.flush();
    while (waitForTouch() < 0) delay(20);
}

void drawBoxedString(uint16_t x, uint16_t y, const char* str,
                     uint16_t fontsize, uint16_t foreColor, uint16_t bgColor)
{
    tft.setTextSize(fontsize);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(str, x, y, &x1, &y1, &w, &h);
    tft.fillRect(x, y, w, h, bgColor);
    tft.setCursor(x, y);
    tft.setTextColor(foreColor);
    tft.print(str);
}

void clearDialog()
{
    tft.fillRect(gameSize.x, gameSize.y, gameSize.width, gameSize.height, backgroundColor);
    tft.fillRect(gameSize.x, gameSize.y, gameSize.width, SCORE_SIZE, PRIMARY_DARK_COLOR);
}

// ── Brick helpers ─────────────────────────────────────────────────────────────
void    setBrick(int wall[], uint8_t x, uint8_t y)   { wall[y] |=  BIT_MASK[x]; }
void    unsetBrick(int wall[], uint8_t x, uint8_t y) { wall[y] &= ~BIT_MASK[x]; }
boolean isBrickIn(int wall[], uint8_t x, uint8_t y)  { return wall[y] & BIT_MASK[x]; }

// ── Redraw ───────────────────────────────────────────────────────────────────
// Reconstructs the full display from current game state after reconnect.
// Called from loop() when drawPending is set by onRedrawRequest.

void redrawFullScreen()
{
    // Background and score bar
    tft.fillRect(gameSize.x, gameSize.y, gameSize.width, gameSize.height, backgroundColor);
    tft.fillRect(gameSize.x, gameSize.y, gameSize.width, SCORE_SIZE, PRIMARY_DARK_COLOR);

    // Lives and score
    updateLives(game->lives, state.remainingLives);
    updateScore(state.score);

    // Remaining bricks
    int colors[] = {RED, RED, BLUE, BLUE, YELLOW, YELLOW, GREEN, GREEN};
    for (int i = 0; i < game->rows; i++)
        for (int j = 0; j < game->columns; j++)
            if (isBrickIn(state.wallState, j, i))
                drawBrick(&state, j, i, colors[i]);

    // Ball and paddle
    drawBall((int16_t)(state.ballx >> game->exponent),
             (int16_t)(state.bally >> game->exponent),
             (int16_t)(state.ballx >> game->exponent),
             (int16_t)(state.bally >> game->exponent),
             game->ballsize);
    drawPlayer(game, &state);

    tft.flush();
}

// ── Touch ─────────────────────────────────────────────────────────────────────

int readUiSelection(game_type* g, game_state_type* s, int16_t lastSelected)
{
    if (_autoPlay) {
        s->playerx = (s->ballx >> g->exponent) - g->playerwidth / 2;
        if (s->playerx >= tft.width() - g->playerwidth)
            s->playerx = tft.width() - g->playerwidth;
        if (s->playerx < 0) s->playerx = 0;
        return 1;
    }

    // Drain queue — use last (newest) point for paddle
    TSPoint tp;
    bool    touched = false;
    while (ts.available()) {
        TSPoint p = ts.getQueuedPoint();
        if (p.z > RemoteTouchScreen::MINPRESSURE) { tp = p; touched = true; }
    }
    if (touched) {
        int16_t newX = tp.x - g->playerwidth / 2;
        if (newX < 0) newX = 0;
        if (newX >= tft.width() - g->playerwidth)
            newX = tft.width() - g->playerwidth;
        s->playerx = newX;
        return 1;
    }
    return -1;
}

int waitForTouch()
{
    if (_paused)     return -1;   // disconnected — keep waiting
    if (drawPending) return 1;
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
