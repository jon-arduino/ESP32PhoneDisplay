# Bitmap opcodes (ESP -> display)

All multi-byte fields are little-endian. A frame is `[0xA5][len u16][cmd][payload]`, `len = 1 + payload bytes`.
Every bitmap frame is self-contained: the library sends large images as horizontal strips (about 2 KB of
pixel data each), each strip with its own `y` and `h`. Rows of a 1-bit bitmap or mask are padded to whole bytes
(`(w+7)/8` bytes per row), MSB first - exactly like Adafruit_GFX.

| Opcode | Name | Payload | Adafruit_GFX equivalent |
|---|---|---|---|
| 0x40 | DRAW_BITMAP | x y w h (i16), 1-bit data | `drawBitmap(x,y,bmp,w,h,color)` - **carries no colour**; displays use the current text colour. Deprecated, kept for old displays. |
| 0x41 | DRAW_BITMAP_BG | x y w h (i16), fg bg (u16), 1-bit data | `drawBitmap(x,y,bmp,w,h,fg,bg)` |
| 0x42 | DRAW_BITMAP_FG | x y w h (i16), colour (u16), 1-bit data | `drawBitmap(x,y,bmp,w,h,color)`; also `drawXBitmap` (the ESP reverses the bit order) |
| 0x43 | DRAW_RGB_BITMAP | x y w h (i16), w*h RGB565 (u16, row-major) | `drawRGBBitmap(x,y,bmp,w,h)` |
| 0x44 | DRAW_RGB_BITMAP_MASK | x y w h (i16), 1-bit mask `(w+7)/8*h` bytes (1 = draw), w*h RGB565 | `drawRGBBitmap(x,y,bmp,mask,w,h)` |
| 0x45 | DRAW_GRAY_BITMAP | x y w h (i16), w*h gray (u8) | `drawGrayscaleBitmap` - **but shown as a gray ramp**: `((g&0xF8)<<8)\|((g&0xFC)<<3)\|(g>>3)`. Adafruit instead uses the 8-bit value itself as the colour. |

Display rules for 0x42-0x45: ignore the frame if `w < 1`, `h < 1`, or the payload is shorter than required
(0x40/0x41 may read missing bytes as 0 for backward compatibility). Clip to the screen like Adafruit_GFX; apply the
current rotation. 0x42-0x45 never use the text colour.

Limits: one frame holds at most 65534 payload bytes (`len` is 16 bits), so a single row must fit
(RGB: `w <= 32762`). The library strips images so frames stay near 2 KB; define `GFX_BITMAP_STRIP_BYTES` to change it.
Compatibility: displays that do not know 0x42-0x45 (older iPhone app builds) ignore them; define `GFX_LEGACY_BITMAP`
in the sketch to make `drawBitmap(..., color)` use 0x40 plus a `setTextColor(color)` instead.
