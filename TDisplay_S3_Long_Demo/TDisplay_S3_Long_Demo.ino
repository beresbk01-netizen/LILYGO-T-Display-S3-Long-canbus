/*
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║  T-Display-S3-Long  +  MCP2515  —  TFT_eSPI sprite edition     ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  LIBRARIES: TFT_eSPI (Bodmer)  |  ACAN2515 (Pierre Molinaro)   ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  FILES: .ino  AXS15231B.cpp/.h  pins_config.h                  ║
 * ║         fontM.h  fontH.h  fontS.h  fontT.h  yt.h               ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  MCP2515: CS→9  SCK→5  MOSI→6  MISO→7  INT→4  (8MHz crystal)  ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  TOUCH:  tap left  (tx<160) → prev page                        ║
 * ║          tap right (tx>480) → next page                        ║
 * ║          hold cell 700ms   → channel picker (pages 1-3)        ║
 * ║          BOOT button       → next page                         ║
 * ╠══════════════════════════════════════════════════════════════════╣
 * ║  PAGES:  1-3 = configurable 2×2 cells                          ║
 * ║          4   = full data overview (all 12 channels)             ║
 * ╚══════════════════════════════════════════════════════════════════╝
 */

#include "AXS15231B.h"
#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#include <ACAN2515.h>
#include "pins_config.h"
#include "fontM.h"
#include "fontH.h"
#include "fontS.h"
#include "fontT.h"

/* ═══════════  MCP2515  ════════════════════════════════════════════ */
#define CAN_CS    9
#define CAN_INT   4
#define CAN_SCK   5
#define CAN_MOSI  6
#define CAN_MISO  7
#define CAN_QUARTZ   8000000UL   // 16000000UL for 16 MHz module
#define CAN_BITRATE  500000UL

SPIClass canSPI(FSPI);
ACAN2515 can(CAN_CS, canSPI, CAN_INT);
bool canReady = false;
bool demoMode = false;

/* ═══════════  DISPLAY  ════════════════════════════════════════════ */
TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

/* ═══════════  COLOURS  ════════════════════════════════════════════ */
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_DIM     0x4208
#define C_DIMMER  0x2104
#define C_BORDER  0x2945
#define C_PANEL   0x1082
#define C_CYAN    0x07FF
#define C_GREEN   0x07E0
#define C_RED     0xF800
#define C_YELLOW  0xFFE0
#define C_ORANGE  0xFCA0
#define C_PURPLE  0xC81F
#define C_BLUE    0x03FF
#define C_PINK    0xF81F
#define C_LGREEN  0xAFE5

/* ═══════════  CHANNELS  ════════════════════════════════════════════ */
enum ChID : uint8_t {
    CH_RPM=0, CH_SPEED, CH_COOLANT, CH_THROTTLE,
    CH_BATTERY, CH_GEAR, CH_OIL_TEMP, CH_BOOST,
    CH_FUEL, CH_EGT, CH_AFR, CH_OIL_PRESS,
    CH_COUNT
};

struct ChanDef {
    const char *label;
    const char *unit;
    float       minVal, maxVal;
    uint16_t    color;
    uint8_t     decimals;
    float       warnVal;   // value at which bar turns yellow
    float       critVal;   // value at which bar turns red
};

static const ChanDef CH[CH_COUNT] = {
//   label        unit      min   max    color    dec  warn   crit
    {"RPM",       "rpm",    0,   8000, C_CYAN,   0,   6000,  7200},
    {"SPEED",     "km/h",   0,    200, C_BLUE,   0,    130,   180},
    {"COOLANT",   "\xB0C",  0,    130, C_ORANGE, 0,     95,   110},
    {"THROTTLE",  "%",      0,    100, C_LGREEN, 0,     70,    90},
    {"BATTERY",   "V",     10,     15, C_YELLOW, 1,   11.5f, 10.5f},
    {"GEAR",      "",       0,      6, C_WHITE,  0,      5,     6},
    {"OIL TEMP",  "\xB0C",  0,    150, C_ORANGE, 0,    120,   140},
    {"BOOST",     "bar",    0,      3, C_PURPLE, 2,    1.8f,  2.5f},
    {"FUEL",      "%",      0,    100, C_LGREEN, 0,     20,    10},
    {"EGT",       "\xB0C",  0,   1000, C_RED,    0,    750,   900},
    {"AFR",       "",      10,     18, C_PINK,   1,     16,    17},
    {"OIL PRESS", "bar",    0,      7, C_CYAN,   1,      2,     1},
};

static float chVal[CH_COUNT]    = {};
static float chTarget[CH_COUNT] = {};

/* ═══════════  PAGES  ═══════════════════════════════════════════════ */
#define PAGES  4       // 3 configurable + 1 overview
#define CELLS  4       // cells per configurable page

static ChID pageCell[3][CELLS] = {
    { CH_RPM,     CH_SPEED,    CH_COOLANT,  CH_THROTTLE  },
    { CH_BATTERY, CH_GEAR,     CH_OIL_TEMP, CH_BOOST     },
    { CH_FUEL,    CH_EGT,      CH_AFR,      CH_OIL_PRESS },
};

static int curPage = 0;   // 0-3

/* ═══════════  CELL GEOMETRY  ══════════════════════════════════════ */
#define HDR_H   18
#define GAP      3
#define CELL_W  ((640 - GAP * 3) / 2)
#define CELL_H  ((180 - HDR_H - GAP * 3) / 2)

static const int CX[2] = { GAP,          GAP*2 + CELL_W };
static const int CY[2] = { HDR_H + GAP,  HDR_H + GAP*2 + CELL_H };

/* ═══════════  TOUCH STATE MACHINE  ════════════════════════════════
 * States:
 *   IDLE      - waiting for touch
 *   PRESSED   - finger down, watching for long press
 *   RELEASING - INT went HIGH, waiting T_CONFIRM ms to confirm
 *   COOLDOWN  - fired event, waiting for INT to stay HIGH before new touch
 */
static uint8_t touchCmd[8] = {0xb5,0xab,0xa5,0x5a,0x0,0x0,0x0,0x8};
static const uint8_t TC_ADDR = 0x3B;

#define T_LONGPRESS_MS  700
#define T_CONFIRM_MS     60   // how long INT must stay HIGH to count as release
#define T_COOLDOWN_MS   250   // silence after any event to prevent double-fire

enum TouchState : uint8_t { TS_IDLE, TS_PRESSED, TS_RELEASING, TS_COOLDOWN };

static TouchState tc_state    = TS_IDLE;
static int        tc_tx       = -1;
static int        tc_ty       = -1;
static uint32_t   tc_downMs   = 0;
static uint32_t   tc_eventMs  = 0;  // timestamp of last event / state change
static bool       tc_longFired = false;

static bool bootWasLow = false;

/* ═══════════  PICKER  ══════════════════════════════════════════════ */
static bool pickerOpen = false;
static int  pickerSlot = -1;

#define PK_X    6
#define PK_Y    6
#define PK_W   (640 - 12)
#define PK_H   (180 - 12)
#define PK_COLS 4
#define PK_ROWS 3
#define PK_BW  ((PK_W - (PK_COLS+1)*3) / PK_COLS)
#define PK_BH  ((PK_H - 22 - (PK_ROWS+1)*3) / PK_ROWS)

/* ══════════════════════════════════════════════════════════════════
 *  SETUP
 * ══════════════════════════════════════════════════════════════════ */
void setup()
{
    Serial.begin(115200);
    delay(300);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);

    pinMode(TOUCH_INT, INPUT_PULLUP);
    pinMode(TOUCH_RES, OUTPUT);
    digitalWrite(TOUCH_RES, HIGH); delay(2);
    digitalWrite(TOUCH_RES, LOW);  delay(10);
    digitalWrite(TOUCH_RES, HIGH); delay(2);
    Wire.begin(TOUCH_IICSDA, TOUCH_IICSCL);

    axs15231_init();
    spr.createSprite(640, 180);
    spr.setSwapBytes(true);

    analogSetAttenuation(ADC_11db);
    pinMode(PIN_BAT_VOLT, INPUT);

    canSPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
    ACAN2515Settings cfg(CAN_QUARTZ, CAN_BITRATE);
    cfg.mRequestedMode = ACAN2515Settings::NormalMode;
    uint16_t err = can.begin(cfg, []() { can.isr(); });
    canReady = (err == 0);
    demoMode = !canReady;
    Serial.printf("CAN %s (err=0x%04X)\n", canReady?"OK":"DEMO", err);

    // Seed demo
    chTarget[CH_RPM]=850;   chTarget[CH_SPEED]=0;
    chTarget[CH_COOLANT]=25; chTarget[CH_THROTTLE]=0;
    chTarget[CH_BATTERY]=12.6f; chTarget[CH_GEAR]=1;
    chTarget[CH_OIL_TEMP]=35;   chTarget[CH_BOOST]=0;
    chTarget[CH_FUEL]=80;   chTarget[CH_EGT]=150;
    chTarget[CH_AFR]=14.7f; chTarget[CH_OIL_PRESS]=2.5f;

    draw();
}

/* ══════════════════════════════════════════════════════════════════
 *  LOOP
 * ══════════════════════════════════════════════════════════════════ */
void loop()
{
    if (canReady) { can.poll(); receiveCAN(); sendHeartbeat(); }
    else if (demoMode) simulateData();

    for (int i = 0; i < CH_COUNT; i++)
        chVal[i] += (chTarget[i] - chVal[i]) * 0.09f;

    static uint32_t lastDraw = 0;
    if (millis()-lastDraw >= 50) { lastDraw=millis(); draw(); }

    /* BOOT button short press → next page */
    bool bootNow = (digitalRead(PIN_BUTTON_1)==LOW);
    if (!bootNow && bootWasLow)
        doNextPage(1);
    bootWasLow = bootNow;

    pollTouch();
}

/* ══════════════════════════════════════════════════════════════════
 *  TOUCH STATE MACHINE
 * ══════════════════════════════════════════════════════════════════ */
void pollTouch()
{
    bool intLow = (digitalRead(TOUCH_INT) == LOW);

    switch (tc_state) {

    case TS_IDLE:
        if (intLow) {
            readTouch(tc_tx, tc_ty);
            tc_downMs    = millis();
            tc_longFired = false;
            tc_state     = TS_PRESSED;
            Serial.printf("DOWN tx=%d ty=%d\n", tc_tx, tc_ty);
        }
        break;

    case TS_PRESSED:
        if (!intLow) {
            /* Finger lifted — start confirmation timer */
            tc_eventMs = millis();
            tc_state   = TS_RELEASING;
        } else {
            /* Still held — check long press */
            if (!tc_longFired &&
                millis()-tc_downMs >= T_LONGPRESS_MS) {
                tc_longFired = true;
                tc_eventMs   = millis();
                Serial.printf("LONG PRESS tx=%d ty=%d\n", tc_tx, tc_ty);
                onLongPress(tc_tx, tc_ty);
                tc_state = TS_COOLDOWN;  // go straight to cooldown after long press
            }
        }
        break;

    case TS_RELEASING:
        if (intLow) {
            /* Finger touched again before confirmation — go back to pressed */
            tc_downMs    = millis();
            tc_longFired = false;
            tc_state     = TS_PRESSED;
        } else if (millis()-tc_eventMs >= T_CONFIRM_MS) {
            /* Confirmed release — fire tap */
            Serial.printf("TAP tx=%d ty=%d\n", tc_tx, tc_ty);
            onTap(tc_tx, tc_ty);
            tc_eventMs = millis();
            tc_state   = TS_COOLDOWN;
        }
        break;

    case TS_COOLDOWN:
        /* Wait T_COOLDOWN_MS with INT HIGH before accepting new touch */
        if (!intLow && millis()-tc_eventMs >= T_COOLDOWN_MS) {
            tc_state = TS_IDLE;
        } else if (intLow) {
            /* Reset cooldown timer if finger is still/again on screen */
            tc_eventMs = millis();
        }
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  READ TOUCH COORDS
 * ══════════════════════════════════════════════════════════════════ */
void readTouch(int &tx, int &ty)
{
    uint8_t buf[20]={0};
    Wire.beginTransmission(TC_ADDR);
    Wire.write(touchCmd, 8);
    Wire.endTransmission();
    Wire.requestFrom(TC_ADDR,(uint8_t)8);
    uint32_t t0=millis();
    while (!Wire.available() && millis()-t0<10);
    Wire.readBytes(buf, 8);

    int px = AXS_GET_POINT_X(buf,0);
    int py = AXS_GET_POINT_Y(buf,0);
    if (px>640) px=640;
    if (py>180) py=180;
    tx = constrain(map(px, 627, 10, 0, 640), 0, 639);
    ty = constrain(map(py, 180,  0, 0, 180), 0, 179);
}

/* ══════════════════════════════════════════════════════════════════
 *  EVENT HANDLERS
 * ══════════════════════════════════════════════════════════════════ */
void onTap(int tx, int ty)
{
    if (pickerOpen) { handlePickerTap(tx, ty); return; }
    if (tx < 160) { doNextPage(-1); return; }
    if (tx > 480) { doNextPage(+1); return; }
}

void onLongPress(int tx, int ty)
{
    if (pickerOpen || curPage >= 3) return;  // no picker on overview page
    int slot = hitCell(tx, ty);
    if (slot >= 0) { pickerSlot=slot; pickerOpen=true; }
}

void doNextPage(int dir)
{
    if (pickerOpen) { pickerOpen=false; return; }
    curPage = (curPage + dir + PAGES) % PAGES;
}

int hitCell(int tx, int ty)
{
    for (int r=0; r<2; r++)
        for (int c=0; c<2; c++)
            if (tx>=CX[c] && tx<CX[c]+CELL_W &&
                ty>=CY[r] && ty<CY[r]+CELL_H)
                return r*2+c;
    return -1;
}

void handlePickerTap(int tx, int ty)
{
    for (int i=0; i<CH_COUNT; i++) {
        int bx = PK_X+3+(i%PK_COLS)*(PK_BW+3);
        int by = PK_Y+22+(i/PK_COLS)*(PK_BH+3);
        if (tx>=bx&&tx<bx+PK_BW&&ty>=by&&ty<by+PK_BH) {
            pageCell[curPage][pickerSlot]=(ChID)i;
            pickerOpen=false;
            return;
        }
    }
    pickerOpen=false;
}

/* ══════════════════════════════════════════════════════════════════
 *  DRAW ROUTER
 * ══════════════════════════════════════════════════════════════════ */
void draw()
{
    spr.fillSprite(C_BLACK);
    if (pickerOpen) { drawPicker(); }
    else {
        drawHeader();
        if (curPage < 3) {
            for (int r=0;r<2;r++)
                for (int c=0;c<2;c++)
                    drawCell(CX[c],CY[r],CELL_W,CELL_H,
                             pageCell[curPage][r*2+c]);
            drawLongPressArc();
        } else {
            drawOverviewPage();
        }
    }
    lcd_PushColors_rotated_90(0,0,640,180,(uint16_t*)spr.getPointer());
}

/* ══════════════════════════════════════════════════════════════════
 *  HEADER
 * ══════════════════════════════════════════════════════════════════ */
void drawHeader()
{
    spr.drawFastHLine(0, HDR_H-1, 640, C_BORDER);

    /* Page buttons */
    static const char *pnames[] = {"1","2","3","ALL"};
    for (int i=0; i<PAGES; i++) {
        int bw  = (i<3) ? 24 : 36;
        int dx  = 320 - (PAGES-1)*16 - 6 + i*34;
        if (i==curPage) {
            spr.fillRoundRect(dx-bw/2, 2, bw, 13, 3, C_CYAN);
            spr.setTextColor(C_BLACK, C_CYAN);
        } else {
            spr.drawRoundRect(dx-bw/2, 2, bw, 13, 3, C_BORDER);
            spr.setTextColor(C_DIM, C_BLACK);
        }
        spr.setTextDatum(MC_DATUM);
        spr.drawString(pnames[i], dx, 9, 1);
    }

    /* CAN status */
    uint16_t dot = canReady?C_GREEN:C_YELLOW;
    spr.fillCircle(8, 9, 4, dot);
    spr.setTextColor(dot, C_BLACK);
    spr.setTextDatum(ML_DATUM);
    spr.drawString(canReady?"LIVE":"DEMO", 16, 9, 1);

    /* Hint */
    const char *hint = (curPage<3) ? "< tap >  hold=edit" : "< tap >";
    spr.setTextColor(C_DIMMER, C_BLACK);
    spr.setTextDatum(MR_DATUM);
    spr.drawString(hint, 638, 9, 1);
}

/* ══════════════════════════════════════════════════════════════════
 *  CONFIGURABLE CELL  (pages 1-3)
 * ══════════════════════════════════════════════════════════════════ */
void drawCell(int x, int y, int w, int h, ChID id)
{
    const ChanDef &d = CH[id];
    float val = chVal[id];
    float pct = constrain((val-d.minVal)/(d.maxVal-d.minVal), 0.f, 1.f);

    /* Warn/crit colour for value text */
    uint16_t vc = C_WHITE;
    if (d.warnVal < d.critVal) {
        if (val >= d.critVal) vc = C_RED;
        else if (val >= d.warnVal) vc = C_YELLOW;
    } else {  // inverted (battery, oil pressure, fuel)
        if (val <= d.critVal) vc = C_RED;
        else if (val <= d.warnVal) vc = C_YELLOW;
    }

    spr.drawRoundRect(x,   y,   w,   h,   5, d.color);
    spr.drawRoundRect(x+1, y+1, w-2, h-2, 5, C_BORDER);

    spr.setTextColor(d.color, C_BLACK);
    spr.setTextDatum(TL_DATUM); spr.setTextFont(2);
    spr.drawString(d.label, x+6, y+4);

    spr.setTextColor(C_DIM, C_BLACK);
    spr.setTextDatum(TR_DATUM); spr.setTextFont(2);
    spr.drawString(d.unit, x+w-6, y+4);

    char buf[12]; fmtVal(id, val, buf);
    spr.loadFont(fontM);
    spr.setTextColor(vc, C_BLACK);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(buf, x+w/2, y+h/2+2);
    spr.unloadFont();

    /* Bar */
    int bx=x+6, by=y+h-9, bw=w-12;
    spr.fillRoundRect(bx, by, bw, 5, 2, C_BORDER);
    if (pct>0.01f) {
        uint16_t bc = (pct<0.65f)?d.color:(pct<0.85f)?C_YELLOW:C_RED;
        spr.fillRoundRect(bx, by, (int)(bw*pct), 5, 2, bc);
    }
}

void fmtVal(ChID id, float v, char *buf)
{
    if (id==CH_GEAR) {
        if((int)v==0) strcpy(buf,"N");
        else snprintf(buf,12,"%d",(int)v);
        return;
    }
    switch(CH[id].decimals) {
        case 0: snprintf(buf,12,"%d",  (int)v); break;
        case 1: snprintf(buf,12,"%.1f",v);      break;
        case 2: snprintf(buf,12,"%.2f",v);      break;
        default:snprintf(buf,12,"%.1f",v);      break;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  LONG PRESS ARC
 * ══════════════════════════════════════════════════════════════════ */
void drawLongPressArc()
{
    if (tc_state!=TS_PRESSED || tc_longFired || tc_tx<0) return;
    uint32_t held = millis()-tc_downMs;
    if (held<60) return;
    int slot = hitCell(tc_tx, tc_ty);
    if (slot<0) return;
    int cx=CX[slot%2]+CELL_W/2, cy=CY[slot/2]+CELL_H/2;
    int rad=min(CELL_W,CELL_H)/2-10;
    float prog=constrain((float)held/T_LONGPRESS_MS,0.f,1.f);
    int endDeg=(int)(360.f*prog);
    for (int a=0; a<=endDeg; a+=4) {
        float rf=(a-90)*0.01745f;
        spr.fillCircle(cx+(int)(rad*cosf(rf)), cy+(int)(rad*sinf(rf)), 2, C_CYAN);
    }
    spr.fillCircle(cx, cy, 4, C_CYAN);
}

/* ══════════════════════════════════════════════════════════════════
 *  PAGE 4 — OVERVIEW  (all 12 channels, compact layout)
 *
 *  Layout: 2 columns of 6 rows each
 *  Each row: colour dot | label | big value | unit | mini bar
 *  Plus a top summary strip: RPM arc  |  SPEED  |  GEAR
 * ══════════════════════════════════════════════════════════════════ */
void drawOverviewPage()
{
    /* ── Top strip: RPM arc + speed + gear ── */
    const int STRIP_H = 62;
    const int STRIP_Y = HDR_H;

    /* RPM arc (left) */
    {
        int cx=52, cy=STRIP_Y+32, r=28;
        float pct = constrain(chVal[CH_RPM]/8000.f, 0.f, 1.f);
        /* Background arc */
        for (int a=135; a<=405; a+=3) {
            float rf=a*0.01745f;
            spr.fillCircle(cx+(int)(r*cosf(rf)), cy+(int)(r*sinf(rf)), 2, C_BORDER);
        }
        /* Filled arc */
        uint16_t ac=(pct<0.75f)?C_CYAN:(pct<0.90f)?C_YELLOW:C_RED;
        int endA=135+(int)(270.f*pct);
        for (int a=135; a<=endA; a+=3) {
            float rf=a*0.01745f;
            spr.fillCircle(cx+(int)(r*cosf(rf)), cy+(int)(r*sinf(rf)), 2, ac);
        }
        /* RPM text */
        char buf[8]; snprintf(buf,8,"%d",(int)chVal[CH_RPM]);
        spr.setTextColor(ac, C_BLACK);
        spr.setTextDatum(MC_DATUM);
        spr.loadFont(fontS);
        spr.drawString(buf, cx, cy-4);
        spr.unloadFont();
        spr.setTextColor(C_DIM, C_BLACK);
        spr.drawString("rpm", cx, cy+10, 1);
    }

    /* Speed (centre-left) */
    {
        int cx=175;
        char buf[8]; snprintf(buf,8,"%d",(int)chVal[CH_SPEED]);
        spr.setTextColor(C_BLUE, C_BLACK);
        spr.setTextDatum(MC_DATUM);
        spr.loadFont(fontH);
        spr.drawString(buf, cx, STRIP_Y+26);
        spr.unloadFont();
        spr.setTextColor(C_DIM, C_BLACK);
        spr.drawString("km/h", cx, STRIP_Y+50, 2);
    }

    /* Throttle bar (centre) */
    {
        int bx=240, by=STRIP_Y+8, bw=80, bh=10;
        spr.setTextColor(C_DIM, C_BLACK);
        spr.setTextDatum(TL_DATUM);
        spr.drawString("THROTTLE", bx, by-1, 1);
        spr.fillRoundRect(bx, by+11, bw, bh, 3, C_BORDER);
        float tp=constrain(chVal[CH_THROTTLE]/100.f, 0.f, 1.f);
        spr.fillRoundRect(bx, by+11, (int)(bw*tp), bh, 3, C_LGREEN);
        char tb[8]; snprintf(tb,8,"%.0f%%",chVal[CH_THROTTLE]);
        spr.setTextColor(C_LGREEN, C_BLACK);
        spr.drawString(tb, bx, by+24, 1);

        /* Coolant bar below */
        spr.setTextColor(C_DIM, C_BLACK);
        spr.drawString("COOLANT", bx, by+36, 1);
        spr.fillRoundRect(bx, by+47, bw, bh, 3, C_BORDER);
        float cp=constrain((chVal[CH_COOLANT]-20.f)/110.f, 0.f, 1.f);
        uint16_t cc=(chVal[CH_COOLANT]<95)?C_ORANGE:(chVal[CH_COOLANT]<110)?C_YELLOW:C_RED;
        spr.fillRoundRect(bx, by+47, (int)(bw*cp), bh, 3, cc);
        char cb[8]; snprintf(cb,8,"%.0f\xB0",chVal[CH_COOLANT]);
        spr.setTextColor(cc, C_BLACK);
        spr.drawString(cb, bx, by+60, 1);
    }

    /* Gear box (right of centre) */
    {
        int gx=345, gy=STRIP_Y+6, gw=48, gh=50;
        spr.setTextColor(C_DIM, C_BLACK);
        spr.setTextDatum(TC_DATUM);
        spr.drawString("GEAR", gx+gw/2, gy, 1);
        spr.fillRoundRect(gx, gy+12, gw, gh-12, 6, C_PANEL);
        spr.drawRoundRect(gx, gy+12, gw, gh-12, 6, C_BORDER);
        char gb[4];
        int gv=(int)chVal[CH_GEAR];
        if (gv==0) strcpy(gb,"N"); else snprintf(gb,4,"%d",gv);
        spr.loadFont(fontM);
        spr.setTextColor(C_WHITE, C_PANEL);
        spr.setTextDatum(MC_DATUM);
        spr.drawString(gb, gx+gw/2, gy+30);
        spr.unloadFont();
    }

    /* Battery status (right) */
    {
        int bx=410, by=STRIP_Y+6;
        float bv=chVal[CH_BATTERY];
        uint16_t bc=(bv>12.2f)?C_GREEN:(bv>11.5f)?C_YELLOW:C_RED;
        spr.setTextColor(C_DIM, C_BLACK);
        spr.setTextDatum(TL_DATUM);
        spr.drawString("BATTERY", bx, by, 1);
        /* Battery icon */
        spr.drawRoundRect(bx, by+12, 80, 20, 3, C_WHITE);
        spr.fillRect(bx+80, by+16, 4, 12, C_WHITE);
        float bp=constrain((bv-10.f)/5.f, 0.f, 1.f);
        spr.fillRoundRect(bx+2, by+14, (int)(76.f*bp), 16, 2, bc);
        char bbuf[10]; snprintf(bbuf,10,"%.2fV",bv);
        spr.setTextColor(bc, C_BLACK);
        spr.drawString(bbuf, bx, by+35, 2);
    }

    /* OIL PRESSURE warning lamp */
    {
        float op=chVal[CH_OIL_PRESS];
        uint16_t oc=(op>2.f)?C_GREEN:(op>1.f)?C_YELLOW:C_RED;
        int lx=530, ly=STRIP_Y+14;
        spr.fillCircle(lx, ly, 10, oc);
        spr.drawCircle(lx, ly, 10, C_WHITE);
        spr.setTextColor(C_DIM, C_BLACK);
        spr.setTextDatum(TL_DATUM);
        spr.drawString("OIL", lx+14, ly-6, 1);
        char ob[8]; snprintf(ob,8,"%.1f",op);
        spr.setTextColor(oc, C_BLACK);
        spr.drawString(ob, lx+14, ly+4, 1);
    }

    /* Divider */
    spr.drawFastHLine(0, STRIP_Y+STRIP_H, 640, C_BORDER);

    /* ── Bottom grid: 2 columns × 6 rows of channel readouts ── */
    const int GRID_Y = STRIP_Y + STRIP_H + 2;
    const int COL_W  = 316;
    const int ROW_H  = (180 - GRID_Y - 2) / 6;   // ~19 px

    /* Channels to show in left and right columns */
    ChID left[6]  = { CH_RPM, CH_SPEED, CH_COOLANT, CH_THROTTLE, CH_BATTERY, CH_GEAR };
    ChID right[6] = { CH_OIL_TEMP, CH_BOOST, CH_FUEL, CH_EGT, CH_AFR, CH_OIL_PRESS };

    for (int row=0; row<6; row++) {
        for (int col=0; col<2; col++) {
            ChID id    = (col==0) ? left[row] : right[row];
            const ChanDef &d = CH[id];
            float val  = chVal[id];
            float pct  = constrain((val-d.minVal)/(d.maxVal-d.minVal), 0.f, 1.f);

            int rx = col==0 ? 0 : COL_W+4;
            int ry = GRID_Y + row*ROW_H;
            int rw = COL_W;

            /* Alternating row tint */
            if (row%2==0)
                spr.fillRect(rx, ry, rw, ROW_H-1, 0x0841);

            /* Warn/crit colour */
            uint16_t vc = d.color;
            if (d.warnVal < d.critVal) {
                if (val>=d.critVal) vc=C_RED;
                else if (val>=d.warnVal) vc=C_YELLOW;
            } else {
                if (val<=d.critVal) vc=C_RED;
                else if (val<=d.warnVal) vc=C_YELLOW;
            }

            /* Colour dot */
            spr.fillCircle(rx+6, ry+ROW_H/2, 3, d.color);

            /* Label */
            spr.setTextColor(C_DIM, row%2==0?0x0841:C_BLACK);
            spr.setTextDatum(ML_DATUM);
            spr.setTextFont(1);
            spr.drawString(d.label, rx+13, ry+ROW_H/2);

            /* Value */
            char buf[12]; fmtVal(id, val, buf);
            spr.setTextColor(vc, row%2==0?0x0841:C_BLACK);
            spr.setTextDatum(MR_DATUM);
            spr.setTextFont(2);
            spr.drawString(buf, rx+rw-42, ry+ROW_H/2);

            /* Unit */
            spr.setTextColor(C_DIMMER, row%2==0?0x0841:C_BLACK);
            spr.setTextFont(1);
            spr.drawString(d.unit, rx+rw-4, ry+ROW_H/2);

            /* Mini bar (last 38 px of row) */
            int mbx=rx+rw-42, mby=ry+ROW_H-5, mbw=38;
            spr.fillRoundRect(mbx, mby, mbw, 3, 1, C_BORDER);
            if (pct>0.01f)
                spr.fillRoundRect(mbx, mby, (int)(mbw*pct), 3, 1, vc);
        }

        /* Column divider */
        spr.drawFastVLine(COL_W+2, GRID_Y, 180-GRID_Y, C_BORDER);

        /* Row divider */
        spr.drawFastHLine(0, GRID_Y+row*ROW_H+ROW_H-1, 640, C_BORDER);
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  PICKER OVERLAY
 * ══════════════════════════════════════════════════════════════════ */
void drawPicker()
{
    spr.fillRoundRect(PK_X,PK_Y,PK_W,PK_H,7,0x0842);
    spr.drawRoundRect(PK_X,  PK_Y,  PK_W,  PK_H,  7,C_CYAN);
    spr.drawRoundRect(PK_X+1,PK_Y+1,PK_W-2,PK_H-2,7,C_BORDER);

    static const char *cname[]={"TOP-LEFT","TOP-RIGHT","BOT-LEFT","BOT-RIGHT"};
    char title[44];
    snprintf(title,sizeof(title),"CHANGE %s  —  tap to select",
             pickerSlot>=0?cname[pickerSlot]:"");
    spr.setTextColor(C_CYAN,0x0842);
    spr.setTextDatum(TL_DATUM);
    spr.drawString(title,PK_X+6,PK_Y+5,1);
    spr.drawFastHLine(PK_X+4,PK_Y+19,PK_W-8,C_BORDER);

    ChID curCh=(pickerSlot>=0)?pageCell[curPage][pickerSlot]:CH_RPM;
    for (int i=0;i<CH_COUNT;i++) {
        int bx=PK_X+3+(i%PK_COLS)*(PK_BW+3);
        int by=PK_Y+22+(i/PK_COLS)*(PK_BH+3);
        bool sel=((int)curCh==i);
        uint16_t bg=sel?CH[i].color:(uint16_t)0x1082;
        spr.fillRoundRect(bx,by,PK_BW,PK_BH,4,bg);
        spr.drawRoundRect(bx,by,PK_BW,PK_BH,4,sel?C_WHITE:CH[i].color);
        spr.setTextColor(sel?C_BLACK:CH[i].color,bg);
        spr.setTextDatum(TC_DATUM); spr.setTextFont(2);
        spr.drawString(CH[i].label,bx+PK_BW/2,by+4);
        spr.setTextColor(sel?C_DIMMER:C_DIMMER,bg);
        spr.setTextDatum(BC_DATUM); spr.setTextFont(1);
        spr.drawString(CH[i].unit,bx+PK_BW/2,by+PK_BH-3);
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  CAN
 * ══════════════════════════════════════════════════════════════════ */
void receiveCAN()
{
    CANMessage m;
    while(can.receive(m)) {
        switch(m.id) {
            case 0x100:if(m.len>=2)chTarget[CH_RPM]      =(float)((uint16_t)(m.data[0]<<8)|m.data[1]);break;
            case 0x101:if(m.len>=1)chTarget[CH_SPEED]    =(float)m.data[0];break;
            case 0x102:if(m.len>=1)chTarget[CH_COOLANT]  =(float)m.data[0];break;
            case 0x103:if(m.len>=2)chTarget[CH_BATTERY]  =((uint16_t)(m.data[0]<<8)|m.data[1])/100.f;break;
            case 0x104:if(m.len>=1)chTarget[CH_THROTTLE] =(float)m.data[0];break;
            case 0x105:if(m.len>=1)chTarget[CH_GEAR]     =(float)m.data[0];break;
            case 0x106:if(m.len>=1)chTarget[CH_OIL_TEMP] =(float)m.data[0];break;
            case 0x107:if(m.len>=2)chTarget[CH_BOOST]    =((uint16_t)(m.data[0]<<8)|m.data[1])/100.f;break;
            case 0x108:if(m.len>=1)chTarget[CH_FUEL]     =(float)m.data[0];break;
            case 0x109:if(m.len>=2)chTarget[CH_EGT]      =(float)((uint16_t)(m.data[0]<<8)|m.data[1]);break;
            case 0x10A:if(m.len>=2)chTarget[CH_AFR]      =((uint16_t)(m.data[0]<<8)|m.data[1])/10.f;break;
            case 0x10B:if(m.len>=2)chTarget[CH_OIL_PRESS]=((uint16_t)(m.data[0]<<8)|m.data[1])/100.f;break;
        }
    }
}

void sendHeartbeat()
{
    static uint32_t t=0;
    if(millis()-t<1000)return;t=millis();
    CANMessage m;m.id=0x200;m.len=4;m.ext=false;m.rtr=false;
    uint32_t s=millis()/1000;
    m.data[0]=s>>24;m.data[1]=s>>16;m.data[2]=s>>8;m.data[3]=s;
    can.tryToSend(m);
}

/* ══════════════════════════════════════════════════════════════════
 *  DEMO SIMULATION
 * ══════════════════════════════════════════════════════════════════ */
void simulateData()
{
    static uint32_t last=0;
    if(millis()-last<120)return;last=millis();
    float t=millis()/1000.f;
    chTarget[CH_RPM]      =800  +3000*(0.5f+0.5f*sinf(t*0.40f))+400*sinf(t*1.3f);
    chTarget[CH_SPEED]    =       120*(0.5f+0.5f*sinf(t*0.35f));
    chTarget[CH_COOLANT]  =72   +  25*(0.5f+0.5f*sinf(t*0.07f));
    chTarget[CH_THROTTLE] =5    +  70*(0.5f+0.5f*sinf(t*0.45f));
    chTarget[CH_BATTERY]  =12.f +0.9f*(0.5f+0.5f*sinf(t*0.20f));
    chTarget[CH_OIL_TEMP] =80   +  30*(0.5f+0.5f*sinf(t*0.06f));
    chTarget[CH_BOOST]    =0.f  +1.8f*(0.5f+0.5f*sinf(t*0.50f));
    chTarget[CH_FUEL]     =78   -   5*(0.5f+0.5f*sinf(t*0.02f));
    chTarget[CH_EGT]      =300  + 500*(0.5f+0.5f*sinf(t*0.40f));
    chTarget[CH_AFR]      =14.7f+  2.f*sinf(t*0.80f);
    chTarget[CH_OIL_PRESS]=2.0f +1.5f*(0.5f+0.5f*sinf(t*0.30f));
    float spd=chTarget[CH_SPEED];
    chTarget[CH_GEAR]=spd<15?1:spd<35?2:spd<60?3:spd<90?4:spd<120?5:6;
}
