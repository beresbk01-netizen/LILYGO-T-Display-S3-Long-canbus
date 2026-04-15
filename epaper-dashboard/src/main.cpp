#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <time.h>
#include <ArduinoJson.h>

#include "config.h"
#include "display.h"
#include "data_fetch.h"
#include "shopping.h"
#include "web_server.h"

// ── Global state ──────────────────────────────────────────────────────────────
static WeatherData  g_weather;
static ForecastDay  g_forecast[3];
static float        g_eurHuf           = 0.0f;
static float        g_history[EXCH_HISTORY_MAX];
static int          g_historyLen       = 0;
static long         g_pingMs           = -1;
static int          g_page             = 0;
static unsigned long g_lastUpdate      = 0;

// ── Exchange rate history ─────────────────────────────────────────────────────
static const char* EXCH_FILE = "/exch_history.json";

static void loadExchangeHistory() {
    g_historyLen = 0;
    if (!LittleFS.exists(EXCH_FILE)) return;
    File f = LittleFS.open(EXCH_FILE, "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok)
        for (JsonVariant v : doc["h"].as<JsonArray>())
            if (g_historyLen < EXCH_HISTORY_MAX)
                g_history[g_historyLen++] = v.as<float>();
    f.close();
}

static void saveExchangeHistory() {
    File f = LittleFS.open(EXCH_FILE, "w");
    if (!f) return;
    JsonDocument doc;
    JsonArray arr = doc["h"].to<JsonArray>();
    for (int i = 0; i < g_historyLen; i++) arr.add(g_history[i]);
    serializeJson(doc, f);
    f.close();
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
static void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    // Max 10 s — don't hang the main loop for longer
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) delay(500);
    Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi FAIL");
}

// ── Ping (TCP connect to Google DNS — avoids hostname lookup hang) ────────────
static long pingGoogle() {
    WiFiClient client;
    client.setTimeout(2000);   // 2 s hard limit
    unsigned long t0 = millis();
    // 8.8.8.8:53 — Google Public DNS, always reachable, no DNS lookup needed
    if (client.connect(IPAddress(8, 8, 8, 8), 53)) {
        long ms = (long)(millis() - t0);
        client.stop();
        return ms;
    }
    return -1L;
}

// ── Data update ───────────────────────────────────────────────────────────────
static void doUpdate() {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;   // skip if still no WiFi

    Serial.println("Fetching weather...");
    fetchWeather(g_weather, g_forecast);

    Serial.println("Fetching EUR/HUF...");
    float rate = fetchEurHuf();
    if (rate > 0.0f) {
        g_eurHuf = rate;
        if (g_historyLen < EXCH_HISTORY_MAX) {
            g_history[g_historyLen++] = rate;
        } else {
            memmove(g_history, g_history + 1, (EXCH_HISTORY_MAX - 1) * sizeof(float));
            g_history[EXCH_HISTORY_MAX - 1] = rate;
        }
        saveExchangeHistory();
    }

    Serial.println("Pinging...");
    g_pingMs = pingGoogle();
    g_lastUpdate = millis();
    Serial.printf("Ping: %ldms  EUR/HUF: %.2f\n", g_pingMs, g_eurHuf);
}

// ── Button ────────────────────────────────────────────────────────────────────
static bool          g_lastBtn   = HIGH;
static unsigned long g_lastBtnMs = 0;

static void checkButton() {
    bool state = digitalRead(BTN_PIN);
    if (state == LOW && g_lastBtn == HIGH && (millis() - g_lastBtnMs > 250)) {
        g_lastBtnMs = millis();
        g_page = (g_page + 1) % 2;
        Serial.printf("Page -> %d\n", g_page);
        if (g_page == 0)
            drawDashboard(g_weather, g_forecast, g_eurHuf,
                          g_history, g_historyLen, WiFi.RSSI(), g_pingMs);
        else
            drawShoppingPage(getShoppingList());
    }
    g_lastBtn = state;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n=== Dashboard boot ===");

    pinMode(BTN_PIN, INPUT_PULLUP);

    if (!LittleFS.begin(true)) Serial.println("LittleFS failed");

    loadExchangeHistory();
    loadShoppingList();

    initDisplay();

    connectWiFi();

    // mDNS: device accessible at http://shopping.local
    if (MDNS.begin("shopping"))
        Serial.println("mDNS: http://shopping.local");

    configTzTime(TIMEZONE, "pool.ntp.org", "time.cloudflare.com");
    struct tm ti;
    for (int i = 0; i < 20 && !getLocalTime(&ti); i++) delay(500);

    doUpdate();
    startWebServer();

    drawDashboard(g_weather, g_forecast, g_eurHuf,
                  g_history, g_historyLen, WiFi.RSSI(), g_pingMs);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    checkButton();
    handleWebServer();

    // Redraw shopping page immediately after web UI change
    if (g_page == 1 && isShoppingDirty()) {
        clearShoppingDirty();
        drawShoppingPage(getShoppingList());
    }

    // Periodic refresh
    if (millis() - g_lastUpdate >= UPDATE_INTERVAL_MS) {
        doUpdate();
        if (g_page == 0)
            drawDashboard(g_weather, g_forecast, g_eurHuf,
                          g_history, g_historyLen, WiFi.RSSI(), g_pingMs);
    }

    delay(10);
}
