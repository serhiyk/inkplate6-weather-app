#include "weather.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <esp_sleep.h>

static const String url = R"(http://api.open-meteo.com/v1/forecast?latitude=50.14&longitude=24.08&current=temperature_2m,relative_humidity_2m,surface_pressure,cloud_cover,precipitation,weather_code&hourly=temperature_2m,cloud_cover,precipitation_probability,precipitation,rain,snowfall,weather_code,cape&daily=sunrise,sunset&forecast_days=2&timezone=auto)";

struct IsoTime {
  int year;
  int month;
  int day;
  int hour;
  int minute;
};

// mask:
//  1   4   2
//    8   16
struct RainSnowSize {
  uint8_t weather_code;
  uint8_t snow_size;
  uint8_t snow_mask;
  uint8_t rain_size;
  uint8_t rain_mask;
  uint8_t thunder;
};

struct CloudSunSize {
  uint8_t cloud_cover;
  uint8_t cloud_size;
  uint8_t sun_size;
};

static const CloudSunSize cloudSunSize[] = {
  {100, 4, 0},
  {90, 3, 1},
  {60, 2, 1},
  {30, 1, 1},
  {10, 0, 2}
};

static const RainSnowSize rainSnowSize[] = {
  {51, 0, 0, 2, 4, 0},
  {53, 0, 0, 2, 3, 0},
  {55, 0, 0, 3, 3, 0},
  {61, 0, 0, 2, 7, 0},
  {63, 0, 0, 3, 7, 0},
  {65, 0, 0, 4, 7, 0},
  {71, 4, 7, 0, 0, 0},
  {73, 5, 7, 0, 0, 0},
  {75, 6, 7, 0, 0, 0},
  {77, 4, 3, 0, 0, 0},
  {80, 0, 0, 2, 31, 0},
  {81, 0, 0, 3, 31, 0},
  {82, 0, 0, 4, 27, 1},
  {85, 4, 31, 0, 0, 0},
  {86, 6, 31, 0, 0, 0},
  {95, 0, 0, 3, 27, 1},
  {96, 4, 2, 2, 1, 1},
  {99, 5, 7, 3, 24, 1}
};

RTC_DATA_ATTR float cachedTemperature = 0.0f;
RTC_DATA_ATTR int lastFetchedHour = -1; //
RTC_DATA_ATTR WeatherData weatherData;

static const RainSnowSize *getRainSnowSize(int weather_code) {
  for (const RainSnowSize &s : rainSnowSize) {
    if (s.weather_code == weather_code) {
      return &s;
    }
  }
  return nullptr;
}

static void parseIsoTime(const char *timeStr, IsoTime &t) {
  sscanf(timeStr, "%d-%d-%dT%d:%d", &t.year, &t.month, &t.day, &t.hour, &t.minute);
}

static int getCurrentHourlyIndex(JsonDocument& doc, int currentYear, int currentMonth, int currentDay, int currentHour) {
  char currentTime[20];
  snprintf(currentTime, sizeof(currentTime), "%04d-%02d-%02dT%02d:00", currentYear, currentMonth, currentDay, currentHour);
  JsonArray timeArray = doc["hourly"]["time"];
  int startIndex = -1;
  int currentIndex = 0;
  for (const JsonVariant &timeVal : timeArray) {
    const char* timeStr = timeVal.as<const char*>();
    if (timeStr && strcmp(timeStr, currentTime) == 0) {
      startIndex = currentIndex;
      break;
    }
    currentIndex++;
  }
  return startIndex;
}

static void parseForecast(JsonDocument& doc, int startIndex) {
  JsonArray timeArray = doc["hourly"]["time"];
  JsonArray tempArray = doc["hourly"]["temperature_2m"];
  JsonArray cloudArray = doc["hourly"]["cloud_cover"];
  JsonArray weatherArray = doc["hourly"]["weather_code"];
  int i = 0;
  int idx;
  int arraySize = tempArray.size();
  for (; i < FORECAST_SIZE; i++) {
    int currentIndex = startIndex + i * 3;
    if (currentIndex >= arraySize) {
      break;
    }
    float temp1 = tempArray[currentIndex];
    idx = currentIndex + 1;
    float temp2 = tempArray[idx < arraySize ? idx : currentIndex];
    idx = currentIndex + 2;
    float temp3 = tempArray[idx < arraySize ? idx : currentIndex];
    float temp = (temp1 + temp2 + temp3) * (1.0f/3.0f);
    int cloud_cover1 = cloudArray[currentIndex];
    idx = currentIndex + 1;
    int cloud_cover2 = cloudArray[idx < arraySize ? idx : currentIndex];
    idx = currentIndex + 2;
    int cloud_cover3 = cloudArray[idx < arraySize ? idx : currentIndex];
    int cloud_cover = (cloud_cover1 + cloud_cover2 + cloud_cover3) / 3;
    int cloud_size = 0;
    int sun_size = 0;
    int snow_size = 0;
    int snow_mask = 0;
    int rain_size = 0;
    int rain_mask = 0;
    int thunder = 0;
    for (const CloudSunSize &s : cloudSunSize) {
      if (cloud_cover <= s.cloud_cover) {
        cloud_size = s.cloud_size;
        sun_size = s.sun_size;
      }
    }
    int weather_code1 = weatherArray[currentIndex];
    idx = currentIndex + 1;
    int weather_code2 = weatherArray[idx < arraySize ? idx : currentIndex];
    idx = currentIndex + 2;
    int weather_code3 = weatherArray[idx < arraySize ? idx : currentIndex];
    int weather_code = std::max({weather_code1, weather_code2, weather_code3});
    const RainSnowSize *rsSize = getRainSnowSize(weather_code);
    if (rsSize) {
      snow_size = rsSize->snow_size;
      snow_mask = rsSize->snow_mask;
      rain_size = rsSize->rain_size;
      rain_mask = rsSize->rain_mask;
      thunder = rsSize->thunder;
      cloud_size = 5;
    }
    IsoTime ts;
    parseIsoTime(timeArray[currentIndex], ts);
    weatherData.forecast[i].hour = ts.hour;
    weatherData.forecast[i].temperature = temp;
    weatherData.forecast[i].cloud_size = cloud_size;
    weatherData.forecast[i].sun_size = sun_size;
    weatherData.forecast[i].snow_size = snow_size;
    weatherData.forecast[i].snow_mask = snow_mask;
    weatherData.forecast[i].rain_size = rain_size;
    weatherData.forecast[i].rain_mask = rain_mask;
    weatherData.forecast[i].thunder = thunder;
  }
  weatherData.size = i;
}

void updateCurrentWeather(int currentYear, int currentMonth, int currentDay, int currentHour) {
  weatherData.updated = false;
  weatherData.temperature = NAN;
  weatherData.humidity = NAN;
  weatherData.pressure = NAN;
  if (weatherData.error_counter < 4) {
    weatherData.error_counter++;
  } else {
    weatherData.size = 0;
  }
  Serial.println("FORECAST");
  HTTPClient http;
  if (http.begin(url)) {
    http.setTimeout(2000);

    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      String payload = http.getString();
      
      JsonDocument doc;
      JsonDocument filter;
      filter["current"] = true;
      filter["hourly"] = true;
      filter["daily"] = true;
      DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

      if (!error) {
        weatherData.error_counter = 0;
        weatherData.temperature = doc["current"]["temperature_2m"];
        weatherData.humidity = doc["current"]["relative_humidity_2m"];
        weatherData.pressure = doc["current"]["surface_pressure"];
        IsoTime ts;
        parseIsoTime(doc["daily"]["sunrise"][0], ts);
        weatherData.sunrise_hour = ts.hour;
        weatherData.sunrise_minute = ts.minute;
        parseIsoTime(doc["daily"]["sunset"][0], ts);
        weatherData.sunset_hour = ts.hour;
        weatherData.sunset_minute = ts.minute;
        int startIndex = getCurrentHourlyIndex(doc, currentYear, currentMonth, currentDay, currentHour);
        if (startIndex >= 0) {
          parseForecast(doc, startIndex);
        }
        weatherData.updated = true;
      } else {
        Serial.printf("FORECAST: JSON %d\r\n", error);
      }
    } else {
      Serial.printf("FORECAST: response %d\r\n", httpResponseCode);
    }
  } else {
    Serial.println("FORECAST: error");
  }
  http.end();
}

void getCurrentWeather(WeatherData *w) {
  *w = weatherData;
}

float getForecastTemperature(void) {
  return weatherData.updated ? weatherData.temperature : NAN;
}

float getForecastHumidity(void) {
  return weatherData.updated ? weatherData.humidity : NAN;
}

float getForecastPressure(void) {
  return weatherData.updated ? weatherData.pressure : NAN;
}
