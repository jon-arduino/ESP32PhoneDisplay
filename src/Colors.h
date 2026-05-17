#pragma once
// Colors.h — RGB565 colour constants for ESP32PhoneDisplay
//
// Defines the full set of colours typically available in Adafruit display
// driver headers (ST77XX, ILI9341, HX8357, etc.) plus common extras.
//
// Include this file to replace display-driver colour constants when porting:
//   #include <Colors.h>
//
// Each constant is guarded with #ifndef so this file can coexist with an
// existing display driver header without redefinition errors.
//
// RGB565 format: RRRRR GGGGGG BBBBB (5 red, 6 green, 5 blue bits)

// ── Basic colours ─────────────────────────────────────────────────────────────

#ifndef BLACK
#define BLACK       0x0000  //   0,   0,   0
#endif
#ifndef WHITE
#define WHITE       0xFFFF  // 255, 255, 255
#endif
#ifndef RED
#define RED         0xF800  // 255,   0,   0
#endif
#ifndef GREEN
#define GREEN       0x07E0  //   0, 255,   0
#endif
#ifndef BLUE
#define BLUE        0x001F  //   0,   0, 255
#endif
#ifndef CYAN
#define CYAN        0x07FF  //   0, 255, 255
#endif
#ifndef MAGENTA
#define MAGENTA     0xF81F  // 255,   0, 255
#endif
#ifndef YELLOW
#define YELLOW      0xFFE0  // 255, 255,   0
#endif
#ifndef ORANGE
#define ORANGE      0xFD20  // 255, 165,   0
#endif

// ── Extended colours ──────────────────────────────────────────────────────────

#ifndef NAVY
#define NAVY        0x000F  //   0,   0, 123
#endif
#ifndef DARKGREEN
#define DARKGREEN   0x03E0  //   0, 125,   0
#endif
#ifndef DARKCYAN
#define DARKCYAN    0x03EF  //   0, 125, 123
#endif
#ifndef MAROON
#define MAROON      0x7800  // 123,   0,   0
#endif
#ifndef PURPLE
#define PURPLE      0x780F  // 123,   0, 123
#endif
#ifndef OLIVE
#define OLIVE       0x7BE0  // 123, 125,   0
#endif
#ifndef DARKGREY
#define DARKGREY    0x7BEF  // 123, 125, 123
#endif
#ifndef GREY
#define GREY        0x7BEF  // alias for DARKGREY
#endif
#ifndef GRAY
#define GRAY        0x7BEF  // American spelling
#endif
#ifndef DARKGRAY
#define DARKGRAY    0x7BEF  // American spelling
#endif
#ifndef LIGHTGREY
#define LIGHTGREY   0xC618  // 198, 195, 198
#endif
#ifndef LIGHTGRAY
#define LIGHTGRAY   0xC618  // American spelling
#endif
#ifndef SILVER
#define SILVER      0xC618  // 192, 192, 192 — same as LIGHTGREY
#endif
#ifndef PINK
#define PINK        0xFE19  // 255, 192, 200
#endif
#ifndef GREENYELLOW
#define GREENYELLOW 0xAFE5  // 173, 255,  41
#endif
#ifndef BROWN
#define BROWN       0xA145  // 165,  42,  42
#endif
#ifndef GOLD
#define GOLD        0xFEA0  // 255, 215,   0
#endif
#ifndef VIOLET
#define VIOLET      0x901A  // 148,   0, 211
#endif
#ifndef INDIGO
#define INDIGO      0x4810  //  75,   0, 130
#endif

// ── ST77XX aliases (Adafruit_ST77xx.h) ───────────────────────────────────────
// Drop-in replacement — sketches using ST77XX_* constants need no changes.

#ifndef ST77XX_BLACK
#define ST77XX_BLACK    BLACK
#endif
#ifndef ST77XX_WHITE
#define ST77XX_WHITE    WHITE
#endif
#ifndef ST77XX_RED
#define ST77XX_RED      RED
#endif
#ifndef ST77XX_GREEN
#define ST77XX_GREEN    GREEN
#endif
#ifndef ST77XX_BLUE
#define ST77XX_BLUE     BLUE
#endif
#ifndef ST77XX_CYAN
#define ST77XX_CYAN     CYAN
#endif
#ifndef ST77XX_MAGENTA
#define ST77XX_MAGENTA  MAGENTA
#endif
#ifndef ST77XX_YELLOW
#define ST77XX_YELLOW   YELLOW
#endif
#ifndef ST77XX_ORANGE
#define ST77XX_ORANGE   ORANGE
#endif

// ── ILI9341 aliases (Adafruit_ILI9341.h) ─────────────────────────────────────
// Drop-in replacement — sketches using ILI9341_* constants need no changes.

#ifndef ILI9341_BLACK
#define ILI9341_BLACK       BLACK
#endif
#ifndef ILI9341_NAVY
#define ILI9341_NAVY        NAVY
#endif
#ifndef ILI9341_DARKGREEN
#define ILI9341_DARKGREEN   DARKGREEN
#endif
#ifndef ILI9341_DARKCYAN
#define ILI9341_DARKCYAN    DARKCYAN
#endif
#ifndef ILI9341_MAROON
#define ILI9341_MAROON      MAROON
#endif
#ifndef ILI9341_PURPLE
#define ILI9341_PURPLE      PURPLE
#endif
#ifndef ILI9341_OLIVE
#define ILI9341_OLIVE       OLIVE
#endif
#ifndef ILI9341_LIGHTGREY
#define ILI9341_LIGHTGREY   LIGHTGREY
#endif
#ifndef ILI9341_DARKGREY
#define ILI9341_DARKGREY    DARKGREY
#endif
#ifndef ILI9341_BLUE
#define ILI9341_BLUE        BLUE
#endif
#ifndef ILI9341_GREEN
#define ILI9341_GREEN       GREEN
#endif
#ifndef ILI9341_CYAN
#define ILI9341_CYAN        CYAN
#endif
#ifndef ILI9341_RED
#define ILI9341_RED         RED
#endif
#ifndef ILI9341_MAGENTA
#define ILI9341_MAGENTA     MAGENTA
#endif
#ifndef ILI9341_YELLOW
#define ILI9341_YELLOW      YELLOW
#endif
#ifndef ILI9341_WHITE
#define ILI9341_WHITE       WHITE
#endif
#ifndef ILI9341_ORANGE
#define ILI9341_ORANGE      ORANGE
#endif
#ifndef ILI9341_GREENYELLOW
#define ILI9341_GREENYELLOW GREENYELLOW
#endif
#ifndef ILI9341_PINK
#define ILI9341_PINK        PINK
#endif

// ── HX8357 aliases (Adafruit_HX8357.h) ───────────────────────────────────────

#ifndef HX8357_BLACK
#define HX8357_BLACK    BLACK
#endif
#ifndef HX8357_WHITE
#define HX8357_WHITE    WHITE
#endif
#ifndef HX8357_RED
#define HX8357_RED      RED
#endif
#ifndef HX8357_GREEN
#define HX8357_GREEN    GREEN
#endif
#ifndef HX8357_BLUE
#define HX8357_BLUE     BLUE
#endif
#ifndef HX8357_CYAN
#define HX8357_CYAN     CYAN
#endif
#ifndef HX8357_MAGENTA
#define HX8357_MAGENTA  MAGENTA
#endif
#ifndef HX8357_YELLOW
#define HX8357_YELLOW   YELLOW
#endif
#ifndef HX8357_ORANGE
#define HX8357_ORANGE   ORANGE
#endif

// ── SSD1351 aliases (Adafruit_SSD1351.h) ─────────────────────────────────────

#ifndef SSD1351_BLACK
#define SSD1351_BLACK   BLACK
#endif
#ifndef SSD1351_WHITE
#define SSD1351_WHITE   WHITE
#endif
#ifndef SSD1351_RED
#define SSD1351_RED     RED
#endif
#ifndef SSD1351_GREEN
#define SSD1351_GREEN   GREEN
#endif
#ifndef SSD1351_BLUE
#define SSD1351_BLUE    BLUE
#endif
#ifndef SSD1351_CYAN
#define SSD1351_CYAN    CYAN
#endif
#ifndef SSD1351_MAGENTA
#define SSD1351_MAGENTA MAGENTA
#endif
#ifndef SSD1351_YELLOW
#define SSD1351_YELLOW  YELLOW
#endif