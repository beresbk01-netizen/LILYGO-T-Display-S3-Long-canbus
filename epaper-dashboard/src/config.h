#pragma once

// WiFi
#define WIFI_SSID       "SMD_IoT"
#define WIFI_PASSWORD   "Admin2020"

// OpenWeatherMap (free tier)
// Sign up at https://openweathermap.org/api -> "Current Weather Data" (free)
#define OWM_API_KEY     "238f5fb8c6d61492e38913e8dc18e659"
#define OWM_LAT         "47.9553"           // Nyiregyhaza latitude
#define OWM_LON         "21.7234"           // Nyiregyhaza longitude
#define OWM_UNITS       "metric"            // metric = Celsius

// City name shown on display (ASCII-safe; OWM returns UTF-8 with unrenderable chars)
#define CITY_DISPLAY    "Nyiregyhaza"

// Timezone (POSIX string)
#define TIMEZONE        "CET-1CEST,M3.5.0,M10.5.0/3"

// Update interval
#define UPDATE_INTERVAL_MS  (10UL * 60 * 1000)   // 10 minutes

// E-Paper SPI wiring  (XIAO ESP32-C6 -> Waveshare 5.83" HAT)
//   HAT pin    XIAO pin   GPIO
//   VCC     ->  3V3
//   GND     ->  GND
//   DIN     ->  D10        GPIO18  (SPI MOSI)
//   CLK     ->  D8         GPIO19  (SPI SCK)
//   CS      ->  D7         GPIO17
//   DC      ->  D6         GPIO16
//   RST     ->  D5         GPIO23
//   BUSY    ->  D4         GPIO22
//   PWR     ->  D2         GPIO2
#define EPD_CS    17
#define EPD_DC    16
#define EPD_RST   23
#define EPD_BUSY  22
#define EPD_PWR    2   // set -1 if your HAT has no PWR pin

// Button: D1 (GPIO1) -> GND, internal pull-up.
// GPIO0 (D0) is the boot-mode pin -- avoid it for buttons.
// GPIO21 (D3) is SDIO_DATA1 -- unreliable as GPIO.
// GPIO1 (D1) is a clean general-purpose pin.
#define BTN_PIN   1

// Exchange rate history depth
#define EXCH_HISTORY_MAX  48
