#pragma once
#include <Arduino.h>
#include <time.h>

struct WeatherData {
    float  temp;
    float  feelsLike;
    int    humidity;
    int    conditionId;     // OWM weather condition code
    bool   isNight;         // true when icon ends in 'n'
    String description;
    String cityName;
    time_t sunrise;         // Unix timestamp (UTC)
    time_t sunset;          // Unix timestamp (UTC)
};

struct ForecastDay {
    String dayName;         // "Mon", "Tue", …
    int    conditionId;
    float  tempMin;
    float  tempMax;
};

// Returns true on success; fills out + forecast[3]
bool  fetchWeather(WeatherData& out, ForecastDay forecast[3]);

// Returns EUR/HUF rate, or 0.0 on failure (uses frankfurter.app, no API key)
float fetchEurHuf();
