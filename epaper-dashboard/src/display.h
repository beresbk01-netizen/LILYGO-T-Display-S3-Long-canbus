#pragma once
#include "data_fetch.h"
#include <vector>
#include <Arduino.h>

void initDisplay();

void drawDashboard(const WeatherData& w, const ForecastDay f[3],
                   float eurHuf, const float* history, int histLen,
                   int rssi, long pingMs);

void drawShoppingPage(const std::vector<String>& items);
