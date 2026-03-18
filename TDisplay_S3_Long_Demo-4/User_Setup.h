// ═══════════════════════════════════════════════════════════════════
//  TFT_eSPI  —  User_Setup.h  for  T-Display-S3-Long
//
//  Copy to:  ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
//
//  TFT_eSPI is used ONLY as a sprite / framebuffer engine here.
//  The real display is driven by AXS15231B.cpp over QSPI.
//  No TFT_eSPI SPI transactions ever reach the panel.
// ═══════════════════════════════════════════════════════════════════

#define USER_SETUP_LOADED   // skip User_Setup_Select.h

// ── Driver (placeholder — never initialised) ──────────────────────
#define ILI9341_DRIVER

// ── Display dimensions ────────────────────────────────────────────
// Physical panel is portrait 180 × 640.
// The sketch creates a 640 × 180 sprite and rotates it on push.
#define TFT_WIDTH   180
#define TFT_HEIGHT  640

// ── SPI / pin assignments ─────────────────────────────────────────
// All set to -1 so TFT_eSPI never touches any GPIO.
// DO NOT define TFT_BL here — pins_config.h already defines it as 1
// and a second #define would silently override it to -1, killing
// the backlight and display init.
#define TFT_MOSI    -1
#define TFT_SCLK    -1
#define TFT_CS      -1
#define TFT_DC      -1
#define TFT_RST     -1
#define TFT_MISO    -1
// TFT_BL intentionally NOT defined here

// ── Frequencies (irrelevant — SPI never fires) ────────────────────
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY   5000000
#define SPI_TOUCH_FREQUENCY  2500000

// ── Colour order ──────────────────────────────────────────────────
// Byte-swap is handled by spr.setSwapBytes(true) in the sketch.
#define TFT_RGB_ORDER TFT_RGB

// ── Fonts ─────────────────────────────────────────────────────────
#define LOAD_GLCD    // Font 1  (6x8)
#define LOAD_FONT2   // Font 2  (16 px)
#define LOAD_FONT4   // Font 4  (26 px)
#define LOAD_FONT6   // Font 6  (48 px digits)
#define LOAD_FONT7   // Font 7  (7-seg 48 px)
#define LOAD_FONT8   // Font 8  (75 px digits)
#define LOAD_GFXFF   // FreeFonts (required by loadFont/VLW)
#define SMOOTH_FONT  // required for spr.loadFont(fontM/H/S/T)