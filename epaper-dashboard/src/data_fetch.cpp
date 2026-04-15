#include "data_fetch.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static const char* DAY_NAMES[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

// WMO weather code → short English description
static const char* wmoDesc(int c) {
    if (c == 0)  return "Clear sky";
    if (c <= 2)  return "Partly cloudy";
    if (c == 3)  return "Overcast";
    if (c <= 48) return "Fog";
    if (c <= 55) return "Drizzle";
    if (c <= 67) return "Rain";
    if (c <= 77) return "Snow";
    if (c <= 82) return "Showers";
    if (c <= 86) return "Snow showers";
    if (c >= 95) return "Thunderstorm";
    return "Cloudy";
}

// ── Weather: Open-Meteo (free, no API key, plain HTTP) ────────────────────────
bool fetchWeather(WeatherData& out, ForecastDay forecast[3]) {
    HTTPClient http;
    // forecast_days=4 → today + 3 days; timeformat=unixtime → easy time_t parsing
    String url =
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=" OWM_LAT "&longitude=" OWM_LON
        "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
        "&timezone=Europe%2FBudapest&forecast_days=4&timeformat=unixtime";

    http.begin(url);
    http.setTimeout(10000);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[Weather] HTTP %d\n", code);
        http.end();
        return false;
    }

    // Only pull the fields we need to save RAM
    JsonDocument filter;
    filter["current"]["temperature_2m"]       = true;
    filter["current"]["apparent_temperature"] = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["current"]["weather_code"]         = true;
    filter["current"]["is_day"]               = true;
    filter["daily"]["time"][0]                = true;   // [0] = filter for each element
    filter["daily"]["weather_code"][0]        = true;
    filter["daily"]["temperature_2m_max"][0]  = true;
    filter["daily"]["temperature_2m_min"][0]  = true;
    filter["daily"]["sunrise"][0]             = true;
    filter["daily"]["sunset"][0]              = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    if (err) { Serial.printf("[Weather] JSON: %s\n", err.c_str()); return false; }

    out.temp        = doc["current"]["temperature_2m"].as<float>();
    out.feelsLike   = doc["current"]["apparent_temperature"].as<float>();
    out.humidity    = doc["current"]["relative_humidity_2m"].as<int>();
    out.conditionId = doc["current"]["weather_code"].as<int>();   // WMO code
    out.isNight     = (doc["current"]["is_day"].as<int>() == 0);
    out.description = wmoDesc(out.conditionId);
    out.cityName    = CITY_DISPLAY;
    out.sunrise     = (time_t)doc["daily"]["sunrise"][0].as<long>();
    out.sunset      = (time_t)doc["daily"]["sunset"][0].as<long>();

    JsonArray tArr   = doc["daily"]["time"].as<JsonArray>();
    JsonArray wArr   = doc["daily"]["weather_code"].as<JsonArray>();
    JsonArray maxArr = doc["daily"]["temperature_2m_max"].as<JsonArray>();
    JsonArray minArr = doc["daily"]["temperature_2m_min"].as<JsonArray>();

    for (int i = 0; i < 3; i++) {
        int idx = i + 1;  // skip today (idx 0)
        if (idx >= (int)tArr.size()) { forecast[i] = {"---", 0, 0, 0}; continue; }
        time_t dt = (time_t)tArr[idx].as<long>();
        struct tm* ft = localtime(&dt);
        forecast[i].dayName     = DAY_NAMES[ft->tm_wday];
        forecast[i].conditionId = wArr[idx].as<int>();   // WMO code
        forecast[i].tempMax     = maxArr[idx].as<float>();
        forecast[i].tempMin     = minArr[idx].as<float>();
    }
    return true;
}

// ── EUR/HUF: National Bank of Poland (NBP) plain HTTP ────────────────────────
// api.nbp.pl publishes EUR/PLN and HUF/PLN daily in JSON over plain HTTP.
// EUR/HUF = EUR/PLN ÷ HUF/PLN
static float nbpMid(const char* code) {
    HTTPClient http;
    String url = "http://api.nbp.pl/api/exchangerates/rates/a/";
    url += code;
    url += "/?format=json";
    http.begin(url);
    http.setTimeout(8000);
    if (http.GET() != 200) {
        Serial.printf("[NBP/%s] HTTP %d\n", code, http.GET());
        http.end();
        return 0.0f;
    }
    // Response: {"rates":[{"mid":4.2845}]}
    JsonDocument filter;
    filter["rates"][0]["mid"] = true;
    JsonDocument doc;
    deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    return doc["rates"][0]["mid"].as<float>();
}

float fetchEurHuf() {
    float eurPln = nbpMid("eur");
    if (eurPln <= 0.0f) { Serial.println("[EUR/HUF] EUR/PLN failed"); return 0.0f; }
    float hufPln = nbpMid("huf");
    if (hufPln <= 0.0f) { Serial.println("[EUR/HUF] HUF/PLN failed"); return 0.0f; }
    float rate = eurPln / hufPln;
    Serial.printf("[EUR/HUF] %.4f / %.6f = %.2f\n", eurPln, hufPln, rate);
    return (rate > 100.0f && rate < 1000.0f) ? rate : 0.0f;
}
