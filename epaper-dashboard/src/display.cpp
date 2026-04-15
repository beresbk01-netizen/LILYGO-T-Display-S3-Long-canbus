#include "display.h"
#include "config.h"

// GxEPD2 includes all B/W panel drivers through GxEPD2_BW.h
// GxEPD2_583_T8 = Waveshare 5.83" 648×480 B/W (GDEP0583T8 / UC8154)
// If BUSY pin never releases, the panel may be older GDEH0583T31 – change class to GxEPD2_583
#include <GxEPD2_BW.h>

#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <SPI.h>
#include <time.h>
#include <math.h>

// ── Display instance ─────────────────────────────────────────────────────────
static GxEPD2_BW<GxEPD2_583_T8, GxEPD2_583_T8::HEIGHT> display(
    GxEPD2_583_T8(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ── Layout constants ──────────────────────────────────────────────────────────
static const int W    = 648;
static const int H    = 480;

static const int HDR_H   = 44;   // header bar height
static const int MID_Y   = HDR_H + 1;
static const int MID_H   = 215;  // main content height
static const int FC_Y    = MID_Y + MID_H + 1;
static const int FC_H    = 190;  // forecast strip height
static const int FTR_Y   = FC_Y + FC_H + 1;
static const int DIVX    = 320;  // vertical divider in middle section

// ── Helper: draw a thick horizontal rule ─────────────────────────────────────
static void hline(int x, int y, int w, uint16_t col = GxEPD_BLACK) {
    display.drawFastHLine(x, y, w, col);
    display.drawFastHLine(x, y+1, w, col);
}
static void vline(int x, int y, int h, uint16_t col = GxEPD_BLACK) {
    display.drawFastVLine(x, y, h, col);
    display.drawFastVLine(x+1, y, h, col);
}

// ── Helper: print text centred at (cx, y) ────────────────────────────────────
static void centredText(const char* s, int cx, int y, uint16_t col = GxEPD_BLACK) {
    int16_t x1,y1; uint16_t tw,th;
    display.getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(cx - tw/2, y);
    display.setTextColor(col);
    display.print(s);
}

// ── UTF-8 → Latin-1 for Adafruit GFX fonts ───────────────────────────────────
// FreeSans covers U+0020..U+00FF (Latin-1). OWM returns UTF-8.
// C3 XX sequences map directly: á é í ó ö ú ü etc.
// ő (U+0151, C5 91) and ű (U+0171, C5 B1) are outside Latin-1 → replace with o/u.
static String utf8ToLatin1(const String& s) {
    String out;
    out.reserve(s.length());
    for (int i = 0; i < (int)s.length(); i++) {
        uint8_t b = (uint8_t)s[i];
        if (b < 0x80) {
            out += (char)b;
        } else if (b == 0xC3 && i + 1 < (int)s.length()) {
            // U+00C0..U+00FF: second byte 0x80..0xBF → Latin-1 0xC0..0xFF
            out += (char)(0xC0 | ((uint8_t)s[++i] & 0x3F));
        } else if (b == 0xC5 && i + 1 < (int)s.length()) {
            uint8_t b2 = (uint8_t)s[++i];
            if      (b2 == 0x90) out += 'O';   // Ő
            else if (b2 == 0x91) out += 'o';   // ő
            else if (b2 == 0xB0) out += 'U';   // Ű
            else if (b2 == 0xB1) out += 'u';   // ű
        } else if (b >= 0xC0) {
            i++;    // skip second byte of any other 2-byte sequence
        }
    }
    return out;
}

// ── WiFi signal bars ─────────────────────────────────────────────────────────
static void drawWiFiBars(int x, int y, int rssi, uint16_t col = GxEPD_BLACK) {
    int bars = 0;
    if (rssi > -90) bars = 1;
    if (rssi > -75) bars = 2;
    if (rssi > -60) bars = 3;
    if (rssi > -50) bars = 4;

    for (int i = 0; i < 4; i++) {
        int bh = 5 + i * 5;
        int bx = x + i * 8;
        int by = y + (20 - bh);
        if (i < bars) display.fillRect(bx, by, 6, bh, col);
        else          display.drawRect(bx, by, 6, bh, col);
    }
    char buf[10];
    snprintf(buf, sizeof(buf), "%ddBm", rssi);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(col);
    display.setCursor(x + 38, y + 17);
    display.print(buf);
}

// ── Tiny "G" Google logo ──────────────────────────────────────────────────────
static void drawGoogleLogo(int cx, int cy, int r, uint16_t col = GxEPD_BLACK) {
    display.drawCircle(cx, cy, r,   col);
    display.drawCircle(cx, cy, r-1, col);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(col);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds("G", 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(cx - tw/2 - x1, cy + th/2);
    display.print("G");
}

// ── Weather icons (geometric, drawn programmatically) ────────────────────────
static void drawCloud(int cx, int cy, int r, bool filled) {
    // Three overlapping circles approximate a cloud
    int small = r * 55 / 100;
    if (filled) {
        display.fillCircle(cx,       cy,         r,     GxEPD_BLACK);
        display.fillCircle(cx - r,   cy + r/3,   small, GxEPD_BLACK);
        display.fillCircle(cx + r,   cy + r/3,   small, GxEPD_BLACK);
        // flat bottom
        display.fillRect(cx - r - small/2, cy, r*2 + small, r + small/2 + 2, GxEPD_BLACK);
    } else {
        display.drawCircle(cx,       cy,         r,     GxEPD_BLACK);
        display.drawCircle(cx - r,   cy + r/3,   small, GxEPD_BLACK);
        display.drawCircle(cx + r,   cy + r/3,   small, GxEPD_BLACK);
    }
}

static void drawIconSun(int cx, int cy, int r) {
    int inner = r * 38 / 100;
    int outer = r * 90 / 100;
    display.fillCircle(cx, cy, inner, GxEPD_BLACK);
    for (int i = 0; i < 8; i++) {
        float a = i * M_PI / 4.0f;
        int x1 = cx + (int)(inner * 1.4f * cos(a));
        int y1 = cy + (int)(inner * 1.4f * sin(a));
        int x2 = cx + (int)(outer * cos(a));
        int y2 = cy + (int)(outer * sin(a));
        display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
        display.drawLine(x1+1, y1, x2+1, y2, GxEPD_BLACK);
    }
}

static void drawIconMoon(int cx, int cy, int r) {
    // Crescent: filled circle minus an offset filled white circle
    display.fillCircle(cx, cy, r * 80 / 100, GxEPD_BLACK);
    display.fillCircle(cx + r * 30 / 100, cy - r * 20 / 100, r * 65 / 100, GxEPD_WHITE);
}

static void drawIconCloud(int cx, int cy, int r) {
    drawCloud(cx, cy - r/4, r * 55 / 100, true);
}

static void drawIconPartlyCloudy(int cx, int cy, int r, bool night) {
    // Small sun/moon top-left, cloud bottom-right
    int sunR = r * 35 / 100;
    if (night) drawIconMoon(cx - r/3, cy - r/3, sunR);
    else       drawIconSun (cx - r/3, cy - r/3, sunR);
    drawCloud(cx + r/5, cy + r/5, r * 50 / 100, true);
    // Overlay white partial circle to give depth
    display.fillCircle(cx + r/5, cy + r/5 - r*50/100, r*25/100, GxEPD_WHITE);
}

static void drawIconRain(int cx, int cy, int r) {
    int cloudR = r * 50 / 100;
    int cloudCy = cy - r / 5;
    drawCloud(cx, cloudCy, cloudR, true);
    // Rain drops: 4 diagonal lines below cloud
    for (int i = -1; i <= 2; i++) {
        int dx = i * (r / 3);
        int dropY = cloudCy + cloudR + r/5;
        display.drawLine(cx + dx,   dropY,
                         cx + dx-3, dropY + r/3, GxEPD_BLACK);
        display.drawLine(cx + dx+1, dropY,
                         cx + dx-2, dropY + r/3, GxEPD_BLACK);
    }
}

static void drawIconSnow(int cx, int cy, int r) {
    int cloudR = r * 50 / 100;
    int cloudCy = cy - r / 5;
    drawCloud(cx, cloudCy, cloudR, true);
    // Snow dots
    for (int i = -1; i <= 1; i++) {
        int dx = i * (r / 3);
        int dotY = cloudCy + cloudR + r / 3;
        display.fillCircle(cx + dx, dotY, 3, GxEPD_BLACK);
    }
}

static void drawIconThunder(int cx, int cy, int r) {
    int cloudR = r * 45 / 100;
    int cloudCy = cy - r / 4;
    drawCloud(cx, cloudCy, cloudR, true);
    // Lightning bolt
    int bx = cx - 3, by = cloudCy + cloudR + 2;
    display.fillTriangle(bx,     by,
                         bx + 14, by,
                         bx + 4,  by + r/3, GxEPD_BLACK);
    display.fillTriangle(bx + 2,  by + r/3,
                         bx + 16, by + r/3,
                         bx + 6,  by + r*2/3 + 2, GxEPD_BLACK);
}

static void drawIconFog(int cx, int cy, int r) {
    for (int i = 0; i < 4; i++) {
        int ly = cy - r/2 + i * (r / 3);
        int lx = cx - r * 75 / 100;
        int lw = r * 150 / 100;
        display.fillRoundRect(lx, ly, lw, 4, 2, GxEPD_BLACK);
    }
}

// Dispatch by WMO weather code (Open-Meteo)
// 0=clear, 1-2=partly cloudy, 3=overcast, 45/48=fog,
// 51-67=drizzle/rain, 71-77=snow, 80-82=showers, 85-86=snow showers, 95-99=thunder
static void drawWeatherIcon(int cx, int cy, int r, int wmo, bool night = false) {
    if      (wmo == 0)                          { if (night) drawIconMoon(cx,cy,r); else drawIconSun(cx,cy,r); }
    else if (wmo <= 2)                          drawIconPartlyCloudy(cx,cy,r,night);
    else if (wmo == 3)                          drawIconCloud(cx,cy,r);
    else if (wmo <= 48)                         drawIconFog(cx,cy,r);
    else if ((wmo >= 51 && wmo <= 67) ||
             (wmo >= 80 && wmo <= 82))          drawIconRain(cx,cy,r);
    else if ((wmo >= 71 && wmo <= 77) ||
             (wmo >= 85 && wmo <= 86))          drawIconSnow(cx,cy,r);
    else if (wmo >= 95)                         drawIconThunder(cx,cy,r);
    else                                        drawIconCloud(cx,cy,r);
}

// ── Sparkline graph ───────────────────────────────────────────────────────────
static void drawSparkline(int x, int y, int w, int h, const float* data, int len) {
    if (len < 2) return;
    float mn = data[0], mx = data[0];
    for (int i = 1; i < len; i++) { mn = min(mn, data[i]); mx = max(mx, data[i]); }
    float range = mx - mn;
    if (range < 0.01f) range = 1.0f;  // avoid divide-by-zero

    // Bounding box
    display.drawRect(x, y, w, h, GxEPD_BLACK);

    // Plot line
    for (int i = 1; i < len; i++) {
        float prev = data[i-1], curr = data[i];
        int x1 = x + (int)((float)(i-1) / (len-1) * (w - 2)) + 1;
        int y1 = y + h - 2 - (int)((prev - mn) / range * (h - 4));
        int x2 = x + (int)((float)(i)   / (len-1) * (w - 2)) + 1;
        int y2 = y + h - 2 - (int)((curr - mn) / range * (h - 4));
        display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
        display.drawLine(x1, y1+1, x2, y2+1, GxEPD_BLACK); // 2px thick
    }

    // Min/max labels
    char buf[16];
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);
    snprintf(buf, sizeof(buf), "%.0f", mx);
    display.setCursor(x + 3, y + 13);
    display.print(buf);
    snprintf(buf, sizeof(buf), "%.0f", mn);
    display.setCursor(x + 3, y + h - 4);
    display.print(buf);
}

// ── Header bar ────────────────────────────────────────────────────────────────
static void drawHeader(const WeatherData& w, int rssi, long pingMs) {
    // Solid black bar – white content for contrast
    display.fillRect(0, 0, W, HDR_H, GxEPD_BLACK);

    // WiFi bars + dBm (white)
    drawWiFiBars(8, 10, rssi, GxEPD_WHITE);

    // City  |  Day, D Mon  (bold, centred)
    char buf[64];
    time_t now; time(&now);
    struct tm* t = localtime(&now);
    const char* dayNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char* monNames[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(buf, sizeof(buf), "%s  |  %s %d %s",
             CITY_DISPLAY, dayNames[t->tm_wday], t->tm_mday, monNames[t->tm_mon]);
    display.setFont(&FreeSans12pt7b);
    centredText(buf, W/2, 32, GxEPD_WHITE);

    // Google logo + ping (white, right side)
    int gr = 10;
    int gx = W - 90, gy = HDR_H / 2;
    drawGoogleLogo(gx, gy, gr, GxEPD_WHITE);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_WHITE);
    if (pingMs >= 0) snprintf(buf, sizeof(buf), "%ldms", pingMs);
    else             snprintf(buf, sizeof(buf), "---");
    display.setCursor(gx + gr + 4, gy + 5);
    display.print(buf);
}

// ── Left panel: current weather ───────────────────────────────────────────────
static void drawWeatherPanel(const WeatherData& w) {
    int panelW = DIVX;
    int baseY  = MID_Y;

    // Icon centred in left portion (top half of panel)
    int iconCx = panelW / 3;
    int iconCy = baseY + 55;
    int iconR  = 45;
    drawWeatherIcon(iconCx, iconCy, iconR, w.conditionId, w.isNight);

    // Temperature – large
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f°C", w.temp);
    display.setFont(&FreeSansBold24pt7b);
    display.setTextColor(GxEPD_BLACK);
    int16_t tx,ty; uint16_t tw,th;
    display.getTextBounds(buf,0,0,&tx,&ty,&tw,&th);
    display.setCursor(panelW/2 + 8, baseY + 70);
    display.print(buf);

    // Description – convert UTF-8 (OWM lang=hu) to Latin-1 for the font
    String desc = utf8ToLatin1(w.description);
    desc[0] = toupper((uint8_t)desc[0]);
    display.setFont(&FreeSans9pt7b);
    display.setCursor(panelW/2 + 8, baseY + 92);
    display.print(desc);

    // Feels like / Humidity
    snprintf(buf, sizeof(buf), "Feels %.0f°  Hum %d%%", w.feelsLike, w.humidity);
    display.setFont(&FreeSans9pt7b);
    display.setCursor(8, baseY + 128);
    display.print(buf);

    // Sunrise / Sunset
    auto fmtTime = [](time_t ts, char* out, size_t n) {
        struct tm* t = localtime(&ts);
        snprintf(out, n, "%02d:%02d", t->tm_hour, t->tm_min);
    };
    char sunriseBuf[8], sunsetBuf[8];
    fmtTime(w.sunrise, sunriseBuf, sizeof(sunriseBuf));
    fmtTime(w.sunset,  sunsetBuf,  sizeof(sunsetBuf));

    display.setFont(&FreeSans12pt7b);
    snprintf(buf, sizeof(buf), "Rise %s  Set %s", sunriseBuf, sunsetBuf);
    display.setCursor(8, baseY + 160);
    display.print(buf);
}

// ── Right panel: EUR/HUF ──────────────────────────────────────────────────────
static void drawExchangePanel(float rate, const float* history, int histLen) {
    int px = DIVX + 8;
    int py = MID_Y + 8;
    int pw = W - DIVX - 16;

    // Label
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(px, py + 13);
    display.print("EUR / HUF");

    // Rate – large (show "---" until first successful fetch)
    char buf[32];
    if (rate > 0.0f) snprintf(buf, sizeof(buf), "%.2f", rate);
    else             snprintf(buf, sizeof(buf), "---");
    display.setFont(&FreeSansBold18pt7b);
    display.setCursor(px, py + 50);
    display.print(buf);

    // Trend arrow
    if (histLen >= 2) {
        const char* arrow = (history[histLen-1] >= history[histLen-2]) ? " ^" : " v";
        display.setFont(&FreeSans12pt7b);
        display.setCursor(px + pw - 28, py + 50);
        display.print(arrow);
    }

    // Sparkline – smaller rectangle (80px tall)
    if (histLen >= 2) {
        drawSparkline(px, py + 58, pw, 80, history, histLen);
    }

    vline(DIVX, MID_Y, MID_H);
}

// ── Forecast strip ────────────────────────────────────────────────────────────
static void drawForecast(const ForecastDay f[3]) {
    hline(0, FC_Y, W);
    int colW = W / 3;

    for (int i = 0; i < 3; i++) {
        int cx = colW * i + colW / 2;
        int topY = FC_Y + 8;

        // Day name
        display.setFont(&FreeSansBold18pt7b);
        display.setTextColor(GxEPD_BLACK);
        centredText(f[i].dayName.c_str(), cx, topY + 24);

        // Icon
        drawWeatherIcon(cx, topY + 75, 38, f[i].conditionId, false);

        // Temp range
        char buf[20];
        snprintf(buf, sizeof(buf), "%.0f° / %.0f°", f[i].tempMax, f[i].tempMin);
        display.setFont(&FreeSans12pt7b);
        centredText(buf, cx, FC_Y + FC_H - 10);

        // Column dividers
        if (i < 2) vline(colW * (i+1), FC_Y, FC_H);
    }
}

// ── Footer ────────────────────────────────────────────────────────────────────
static void drawFooter() {
    hline(0, FTR_Y, W);
    char buf[64];
    time_t now; time(&now);
    struct tm* t = localtime(&now);
    snprintf(buf, sizeof(buf), "Updated %02d:%02d  |  shopping.local",
             t->tm_hour, t->tm_min);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(8, FTR_Y + 18);
    display.print(buf);
}

// ── Public API ────────────────────────────────────────────────────────────────
void initDisplay() {
#if EPD_PWR >= 0
    // Power-enable pin: drive HIGH before anything else so the HAT's
    // voltage regulator is on before SPI init begins
    pinMode(EPD_PWR, OUTPUT);
    digitalWrite(EPD_PWR, HIGH);
    delay(20);   // give regulator time to stabilise
#endif

    // -1 for SS: GxEPD2 manages CS (EPD_CS) itself as a GPIO
    SPI.begin(19 /*SCK*/, 20 /*MISO*/, 18 /*MOSI*/, -1);
    display.init(115200, true, 2, false);
    display.setRotation(0);
    display.setTextWrap(false);
}

void drawDashboard(const WeatherData& w, const ForecastDay f[3],
                   float eurHuf, const float* history, int histLen,
                   int rssi, long pingMs) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        drawHeader(w, rssi, pingMs);
        drawWeatherPanel(w);
        drawExchangePanel(eurHuf, history, histLen);
        drawForecast(f);
        drawFooter();
    } while (display.nextPage());
    display.hibernate();
}

void drawShoppingPage(const std::vector<String>& items) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Title bar
        display.fillRect(0, 0, W, HDR_H, GxEPD_BLACK);
        display.setFont(&FreeSansBold18pt7b);
        display.setTextColor(GxEPD_WHITE);
        centredText("Shopping List", W/2, 32);
        display.setTextColor(GxEPD_BLACK);

        // URL hint (so user knows where to browse)
        display.setFont(&FreeSans9pt7b);
        centredText("http://shopping.local", W/2, HDR_H + 18);

        // Items
        int lineH = 36;
        int startY = HDR_H + 30;
        display.setFont(&FreeSans12pt7b);

        if (items.empty()) {
            display.setFont(&FreeSans9pt7b);
            centredText("List is empty - add items from your browser", W/2, H/2);
        } else {
            for (int i = 0; i < (int)items.size() && (startY + (i+1)*lineH) < H - 10; i++) {
                int iy = startY + i * lineH;
                // Checkbox square + item text
                display.drawRect(16, iy + 8, 16, 16, GxEPD_BLACK);
                char row[64];
                snprintf(row, sizeof(row), "%s", items[i].c_str());
                display.setCursor(42, iy + lineH - 8);
                display.print(row);
                display.drawFastHLine(0, iy + lineH + 2, W, GxEPD_BLACK);
            }
        }

        // Footer hint
        display.setFont(&FreeSans9pt7b);
        display.setCursor(8, H - 8);
        display.print("Press button to return to dashboard");

    } while (display.nextPage());
    display.hibernate();
}
