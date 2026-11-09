#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>

#define FORECAST_SIZE (9)

struct Forecast {
  float temperature;
  uint8_t hour;
  uint8_t cloud_size;
  uint8_t sun_size;
  uint8_t snow_size;
  uint8_t snow_mask;
  uint8_t rain_size;
  uint8_t rain_mask;
  uint8_t thunder;
};

struct WeatherData {
  float temperature;
  float humidity;
  float pressure;
  Forecast forecast[FORECAST_SIZE];
  uint8_t sunrise_hour;
  uint8_t sunrise_minute;
  uint8_t sunset_hour;
  uint8_t sunset_minute;
  uint8_t size;
  uint8_t error_counter;
  bool updated;
};

void updateCurrentWeather(int currentYear, int currentMonth, int currentDay, int currentHour);
void getCurrentWeather(WeatherData *w);
float getForecastTemperature(void);
float getForecastHumidity(void);
float getForecastPressure(void);

#endif
