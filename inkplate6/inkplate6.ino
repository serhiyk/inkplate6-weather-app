#include <esp_system.h>
#include "Inkplate.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <time.h>
#include <math.h>
#include "driver/rtc_io.h"
#include <rom/rtc.h>
#include <weather.h>
#include <background.h>
#include <font10.h>
#include <font32.h>
#include <font64.h>
#include <config.h>

#if USE_INT_TEMPERATURE_SENSOR == 1
#include <Adafruit_SHT4x.h>
#endif

#define TIME_BAR_HEIGHT (16)
#define DISPLAY_WIDTH (800)
#define TIME_HEIGHT_SHIFT (120)

struct PointInt {
  int x;
  int y;
};

struct RtcTime {
  uint16_t year;
  uint8_t hour;
  uint8_t minutes;
  uint8_t seconds;
  uint8_t weekday;  // NOTE: In weekdays 0 means Sunday, 1 means Monday, ...
  uint8_t day;
  uint8_t month;
};

static const char* ssid = WIFI_SSID;
static const char* pass = WIFI_PASS;
static const char* ntpServer = NTP_SERVER;
static const long  gmtOffset_sec = 7200;      // UTC+2
static const int   daylightOffset_sec = 3600;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

Inkplate display(INKPLATE_1BIT);
static GFXcanvas1 timeBarCanvas(DISPLAY_WIDTH, TIME_BAR_HEIGHT);

#define WIFI_STATE_INIT (0)
#define WIFI_STATE_CONNECTED (1)
#define WIFI_STATE_FAILED (2)
static int wifiState;

static RtcTime rtcTime;
RTC_DATA_ATTR float extTemperature = NAN;

static int getBatteryPercentage(float voltage) {
  const int batteryTable[][2] = {
    {4200, 100},
    {4100, 90},
    {4000, 80},
    {3910, 70},
    {3850, 60},
    {3800, 50},
    {3760, 40},
    {3730, 30},
    {3700, 20},
    {3650, 10},
    {3500, 5},
    {3000, 0}
  };
  int voltage_mV = (int) (voltage * 1000);
  int numPoints = sizeof(batteryTable) / sizeof(batteryTable[0]);
  if (voltage_mV >= batteryTable[0][0]) return 100;
  if (voltage_mV <= batteryTable[numPoints - 1][0]) return 0;
  for (int i = 0; i < numPoints - 1; i++) {
    if (voltage_mV <= batteryTable[i][0] && voltage_mV > batteryTable[i + 1][0]) {
      int vHigh = batteryTable[i][0];
      int vLow = batteryTable[i + 1][0];
      int pHigh = batteryTable[i][1];
      int pLow = batteryTable[i + 1][1];
      return pLow + (float)(voltage_mV - vLow) * (pHigh - pLow) / (vHigh - vLow);
    }
  }
  return 0;
}

static void rotatePolygonInt(PointInt* points, int size, int centre_x, int centre_y, float angleDegrees) {
  float radians = angleDegrees * (M_PI / 180.0f);
  float c = cosf(radians);
  float s = sinf(radians);
  for (int i = 0; i < size; i++) {
    float dx = (float)(points[i].x - centre_x);
    float dy = (float)(points[i].y - centre_y);
    float new_x = centre_x + (dx * c - dy * s);
    float new_y = centre_y + (dx * s + dy * c);
    points[i].x = (int)(new_x + (new_x >= 0 ? 0.5f : -0.5f));
    points[i].y = (int)(new_y + (new_y >= 0 ? 0.5f : -0.5f));
  }
}

static bool wifi_connect(void) {
  switch (wifiState) {
    case WIFI_STATE_INIT:
    {
      WiFi.begin(ssid, pass);
      // WiFi.setTxPower(WIFI_POWER_8.5dBm);
      int timeout_counter = 40;  // 4c
      while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        if (--timeout_counter == 0) {
          WiFi.disconnect(true);
          WiFi.mode(WIFI_OFF);
          wifiState = WIFI_STATE_FAILED;
          return false;
        }
      }
      wifiState = WIFI_STATE_CONNECTED;
      return true;
    }
    case WIFI_STATE_CONNECTED:
      return true;
  }
  return false;
}

static void wifi_disconnect(void) {
  if (wifiState == WIFI_STATE_CONNECTED) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

static void time_sync1(void) {
  if (!wifi_connect()) {
    return;
  }
  unsigned long start_time = millis();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("NTP: error");
    return;
  }
  unsigned long duration = millis() - start_time;
  if (duration > 2000) {
    Serial.println("NTP: timeout");
    return;
  }
  Serial.printf("NTP: %02d:%02d:%02d\r\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  display.rtc.setTime(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  int full_year = timeinfo.tm_year + 1900;
  display.rtc.setDate(timeinfo.tm_wday, timeinfo.tm_mday, timeinfo.tm_mon + 1, full_year);
}

static void time_sync(void) {
  if (!wifi_connect()) {
    return;
  }
  timeClient.begin();
  unsigned long start_time = millis();
  if (timeClient.forceUpdate()) {
    unsigned long duration = millis() - start_time;
    if (duration < 2000) {
      time_t epochTime = timeClient.getEpochTime();
      setenv("TZ", TIMEZONE, 1);
      tzset();
      struct tm *timeinfo = localtime(&epochTime);
      display.rtc.setTime(timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
      int full_year = timeinfo->tm_year + 1900;
      display.rtc.setDate(timeinfo->tm_wday, timeinfo->tm_mday, timeinfo->tm_mon + 1, full_year);
      Serial.printf("NTP: %02d.%02d.%04d %02d:%02d:%02d\r\n", timeinfo->tm_mday, timeinfo->tm_mon + 1, full_year, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    } else {
      Serial.println("NTP: timeout");
    }
  } else {
    Serial.println("NTP: error");
  }
  timeClient.end();
}

static float getExtTemperature(void) {
  float temperature = NAN;
  if (wifi_connect()) {
    HTTPClient http;
    if (http.begin(EXT_TEMPERATURE_URL)) {
      http.setTimeout(1000);
      int httpResponseCode = http.GET();
      if (httpResponseCode > 0) {
        String payload = http.getString();
        temperature = payload.toFloat();
      }
    }
    http.end();
  }
  return temperature;
}

static void invertTimeBarCanvas(int x1, int x2) {
  for (int y = 0; y < TIME_BAR_HEIGHT; y++) {
    for (int x = x1; x < x2; x++) {
      bool currentPixel = timeBarCanvas.getPixel(x, y);
      timeBarCanvas.drawPixel(x, y, !currentPixel);
    }
  }
}

static void draw_sun(int x, int y, int r) {
  display.fillCircle(x, y, r, 1);
  display.fillCircle(x, y, r - 2, 0);
  PointInt ray[] = {{x - 1, y - r * 9 / 5}, {x + 1, y - r * 9 / 5}, {x + 1, y - r - 3}, {x - 1, y - r - 3}};
  for (int i = 0; i < 12; i++) {
    display.fillTriangle(ray[0].x, ray[0].y, ray[1].x, ray[1].y, ray[2].x, ray[2].y, 1);
    display.fillTriangle(ray[1].x, ray[1].y, ray[2].x, ray[2].y, ray[3].x, ray[3].y, 1);
    if (i == 11) {
      break;
    }
    rotatePolygonInt(ray, 4, x, y, 30.0f);
  }
}

static void draw_moon(int x, int y, int r, int phase_angle) {
  display.fillCircle(x, y, r, 1);
  if (phase_angle < 270) {
    if (phase_angle < 90) {
      display.fillCircle(x - r / 2, y, r, 0);
    } else {
      display.fillRect(x - r, y - r, r, r * 2, 0);
    }
    display.drawCircle(x, y, r, 1);
  }
}

static void draw_cloud(int x, int y, int width, bool fill) {
  int r1 = width / 4;
  int r2 = width / 6;
  int r3 = width / 8;
  int cloud_width = width / 20;
  display.fillCircle(x + r1 + r2 * 6 / 5, y - r1, r1, 1);
  display.fillCircle(x + r2, y - r2, r2, 1);
  display.fillCircle(x + width - r3, y - r3, r3, 1);
  display.fillCircle(x + width - r3 * 2, y - r3 * 3 / 2, r3, 1);
  display.fillRect(x + r2, y - r3 * 2, width - r3 - r2, r3 * 2, 1);
  if (!fill) {
    display.fillCircle(x + r1 + r2 * 6 / 5, y - r1, r1 - cloud_width, 0);
    display.fillCircle(x + r2, y - r2, r2 - cloud_width, 0);
    display.fillCircle(x + width - r3, y - r3, r3 - cloud_width, 0);
    display.fillCircle(x + width - r3 * 2, y - r3 * 3 / 2, r3 - cloud_width, 0);
    display.fillRect(x + r2, y - r3 * 3 / 2, width - r3 - r2, r3 * 3 / 2 - cloud_width, 0);
  }
}

static void draw_rain(int x, int y, int r) {
  display.drawCircle(x, y, r, 1);
  display.fillTriangle(x - r, y, x, y - r * 2, x + r, y, 1);
}

static void draw_snow(int x, int y, int r) {
  PointInt ray[] = {{x, y - r}, {x, y + r}};
  for (int i = 0; i < 3; i++) {
    display.drawLine(ray[0].x, ray[0].y, ray[1].x, ray[1].y, 1);
    if (i == 2) {
      break;
    }
    rotatePolygonInt(ray, 2, x, y, 60.0f);
  }
}

static void draw_thunder(int x, int y) {
  int d = 5;
  PointInt points[7] = {
    {x, y},
    {x - d, y},
    {x, y - (d * 4)},
    {x + d, y - (d * 4)},
    {x + (d / 2), y - d},
    {x + (d * 3 / 2), y - d},
    {x - (d / 2), y + (d * 3)}
  };
  display.fillTriangle(points[0].x, points[0].y, points[2].x, points[2].y, points[1].x, points[1].y, 1);
  display.fillTriangle(points[0].x, points[0].y, points[2].x, points[2].y, points[3].x, points[3].y, 1);
  display.fillTriangle(points[4].x, points[4].y, points[5].x, points[5].y, points[6].x, points[6].y, 1);
}

static void draw_weather(void) {
  const int half_period = 90;
  const int width = DISPLAY_WIDTH / FORECAST_SIZE;
  const int height = 90;
  const int sun_r = 12;
  const int sun_small_r = 10;
  const int moon_r = 20;
  const int moon_small_r = 16;
  const int cloud_offset = TIME_BAR_HEIGHT + sun_r * 2 + moon_r;
  timeBarCanvas.fillScreen(1);
  timeBarCanvas.setFont(&Academy10pt7b);
  timeBarCanvas.setTextSize(1);
  timeBarCanvas.setTextColor(0);
  display.setFont(&Academy10pt7b);
  display.setTextSize(1);
  WeatherData weather;
  getCurrentWeather(&weather);
  int sunrise_time = (int)weather.sunrise_hour * 60 + weather.sunrise_minute;
  int sunset_time = (int)weather.sunset_hour * 60 + weather.sunset_minute;
  for (int i = 0; i < weather.size; i++) {
    Forecast *forecast = &weather.forecast[i];
    char textString[6];
    sprintf(textString, "%d:00", forecast->hour);
    int16_t x1, y1;
    uint16_t textWidth, textHeight;
    display.getTextBounds(textString, 0, 0, &x1, &y1, &textWidth, &textHeight);
    timeBarCanvas.setCursor(width * i + (width - textWidth) / 2, textHeight);
    timeBarCanvas.print(textString);
    int forecast_time = (int)forecast->hour * 60;
    bool is_daytime = forecast_time > sunrise_time && forecast_time < sunset_time;
    if (forecast->sun_size == 2) {
      if (is_daytime) {
        draw_sun(width * i + width / 2, TIME_BAR_HEIGHT + sun_r * 2, sun_r);
      } else {
        draw_moon(width * i + width / 2, TIME_BAR_HEIGHT + sun_r * 2, moon_r, 60);
      }
    } else if (forecast->sun_size == 1) {
      if (is_daytime) {
        draw_sun(width * i + width * 2 / 3, TIME_BAR_HEIGHT + sun_small_r * 2, sun_small_r);
      } else {
        draw_moon(width * i + width * 2 / 3, TIME_BAR_HEIGHT + sun_small_r * 2, moon_small_r, 60);
      }
    }
    sprintf(textString, "%.0f", forecast->temperature);
    display.setCursor(width * i + 4, TIME_BAR_HEIGHT * 2);
    display.print(textString);
    static const int cloud_width_mod[6] = {0, 3, 5, 7, 8, 8};
    if (forecast->cloud_size > 0 && forecast->cloud_size < 6) {
      int cloud_width = width * cloud_width_mod[forecast->cloud_size] / 10;
      draw_cloud(width * i + (width - cloud_width) / 2, cloud_offset, cloud_width, forecast->cloud_size == 5);
    }
    int width_center = width * i + width / 2;
    if (forecast->rain_mask & 4)
      draw_rain(width_center, cloud_offset + 10, forecast->rain_size);
    if (forecast->rain_mask & 1)
      draw_rain(width_center - forecast->rain_size * 4, cloud_offset + 10, forecast->rain_size);
    if (forecast->rain_mask & 2)
      draw_rain(width_center + forecast->rain_size * 4, cloud_offset + 10, forecast->rain_size);
    if (forecast->rain_mask & 8)
      draw_rain(width_center - forecast->rain_size * 2, cloud_offset + 10 + forecast->rain_size * 4, forecast->rain_size);
    if (forecast->rain_mask & 16)
      draw_rain(width_center + forecast->rain_size * 2, cloud_offset + 10 + forecast->rain_size * 4, forecast->rain_size);
    if (forecast->snow_mask & 4)
      draw_snow(width_center, cloud_offset + 10, forecast->snow_size);
    if (forecast->snow_mask & 1)
      draw_snow(width_center - forecast->snow_size * 4, cloud_offset + 10, forecast->snow_size);
    if (forecast->snow_mask & 2)
      draw_snow(width_center + forecast->snow_size * 4, cloud_offset + 10, forecast->snow_size);
    if (forecast->snow_mask & 8)
      draw_snow(width_center - forecast->snow_size * 2, cloud_offset + 10 + forecast->snow_size * 2, forecast->snow_size);
    if (forecast->snow_mask & 16)
      draw_snow(width_center + forecast->snow_size * 2, cloud_offset + 10 + forecast->snow_size * 2, forecast->snow_size);
    if (forecast->thunder)
      draw_thunder(width * i + width / 2, cloud_offset);
    if (forecast_time + half_period <= sunrise_time || forecast_time - half_period >= sunset_time) {
      invertTimeBarCanvas(width * i, width * (i + 1));
    } else if (forecast_time - half_period < sunrise_time && sunrise_time < forecast_time + half_period) {
      int part_width = ((sunrise_time - (forecast_time - half_period)) * width) / 180;
      invertTimeBarCanvas(width * i, width * i + part_width);
    } else if (forecast_time - half_period < sunset_time && sunset_time < forecast_time + half_period) {
      int part_width = ((sunset_time - (forecast_time - half_period)) * width) / 180;
      invertTimeBarCanvas(width * i + part_width, width * (i + 1));
    }
  }
  display.drawBitmap(0, 0, timeBarCanvas.getBuffer(), DISPLAY_WIDTH, TIME_BAR_HEIGHT, 0, 1);
}

static void print_time(int hour, int minute) {
  char timeString[6];
  int16_t x1, y1;
  uint16_t textWidth, textHeight;
  display.setFont(&Academy64pt7b);
  display.setTextSize(2);
  sprintf(timeString, "%d:%02d", hour, minute);
  display.getTextBounds(timeString, 0, 0, &x1, &y1, &textWidth, &textHeight);
  display.setCursor((DISPLAY_WIDTH - textWidth) / 2, textHeight + TIME_HEIGHT_SHIFT);
  display.print(timeString);
}

static int print_sensor_temperature(float temperature, int x, int y) {
  char tempString[6];
  int16_t x1, y1;
  uint16_t textWidth, textHeight;
  display.setFont(&Academy32pt7b);
  display.setTextSize(1);
  sprintf(tempString, "%.01f", temperature);
  display.getTextBounds(tempString, 0, 0, &x1, &y1, &textWidth, &textHeight);
  display.setCursor(x, y + textHeight);
  display.print(tempString);
  display.fillCircle(x + textWidth + 16, y + 2, 6, 1);
  display.fillCircle(x + textWidth + 16, y + 2, 3, 0);
  return textHeight;
}

static void drawBattery(int x, int y) {
  float voltage = display.readBattery();
  int batteryPercentage = getBatteryPercentage(voltage);
  display.drawRect(x, y, 30, 14, 1);
  display.fillRect(x + 30, y + 4, 3, 6, 1);
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setCursor(x + 2, y + 4);
  display.print(batteryPercentage);
  display.print('%');
}

void setup() {
  Serial.begin(115200);
  display.begin();

  float intTemperature = NAN;
  float intHumidity = NAN;
#if USE_INT_TEMPERATURE_SENSOR == 1
  Adafruit_SHT4x sht4 = Adafruit_SHT4x();
  Wire.begin();
  if (sht4.begin()) {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    sensors_event_t humidity_event, temp_sht_event;
    if (sht4.getEvent(&humidity_event, &temp_sht_event)) {
      intTemperature = temp_sht_event.temperature;
      intHumidity = humidity_event.relative_humidity;
      // Serial.print("SHT4x: t=");
      // Serial.print(intTemperature);
      // Serial.print(" h=");
      // Serial.println(intHumidity);
    }
  } else {
      Serial.println("SHT4x not found");
  }
#else
  intTemperature = (float) display.readTemperature();
#endif

  if (!display.rtc.isSet())
  {
    // Set some default datetime
    display.rtc.setTime(6, 54, 11);
    display.rtc.setDate(6, 16, 5, 2020);
    display.rtc.setAlarm(0, 99, 99, 99, 99);
  }

  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
    case ESP_RST_POWERON:
      Serial.println("POWERON");
      time_sync();
    case ESP_RST_DEEPSLEEP:
      Serial.println("START");

      rtcTime.seconds = display.rtc.getSecond();
      rtcTime.minutes = display.rtc.getMinute();
      rtcTime.hour = display.rtc.getHour();
      rtcTime.day = display.rtc.getDay();
      rtcTime.weekday = display.rtc.getWeekday();
      rtcTime.month = display.rtc.getMonth();
      rtcTime.year = display.rtc.getYear();

      if (rtcTime.minutes == 1 || reason == ESP_RST_POWERON) {
        if (wifi_connect()) {
          updateCurrentWeather(rtcTime.year, rtcTime.month, rtcTime.day, rtcTime.hour);
        }
        if (rtcTime.hour == 4) {
          time_sync();
        }
      }
#if USE_EXT_TEMPERATURE_SENSOR == 1
      if (((rtcTime.minutes + 1) & 3) == 0) {  // get every 4 min
        extTemperature = getExtTemperature();
      }
#else
      extTemperature = getForecastTemperature();
#endif
        
      break;
    default:
      Serial.printf("Unknown reset reason: %d\r\n", reason);
      break;
  }

  wifi_disconnect();

  display.clearDisplay();

#if SD_ENABLED == 1
  loadBackgoundFiles(display);
  char fileName[BACKGROUND_FILE_NAME_SIZE];
  if (getBackgoundFile(fileName, rtcTime.month, rtcTime.day)) {
    if (display.sdCardInit()) {
      display.image.draw(fileName, 0, 350, 1);
    } else {
      Serial.println("sdCardInit error");
    }
  } else {
    Serial.printf("Background error: %s, size %d, pos %d\r\n", fileName, getBackgoundSize(), getBackgoundPos());
  }
#else
  const unsigned char *img_data;
  int img_width, img_height;
  if (getBackgoundData(&img_data, &img_width, &img_height, rtcTime.month, rtcTime.day)) {
    display.image.draw(img_data, 0, 600 - img_height, img_width, img_height);
  } else {
    Serial.printf("Background error\r\n");
  }
#endif

  draw_weather();
  print_time(rtcTime.hour, rtcTime.minutes);
  int tempShift = print_sensor_temperature(intTemperature, 5, TIME_HEIGHT_SHIFT);
  if (!isnan(extTemperature)) {
    print_sensor_temperature(extTemperature, 5, TIME_HEIGHT_SHIFT + 48 + tempShift);
  }
  display.setFont(&Academy10pt7b);
  display.setTextSize(1);
  if (!isnan(intHumidity)) {
    char humidityString[5];
    sprintf(humidityString, "%.0f%%", intHumidity);
    display.setCursor(5, TIME_HEIGHT_SHIFT + 28 + tempShift);
    display.print(humidityString);
  }
  float extHumidity = getForecastHumidity();
  if (!isnan(extHumidity)) {
    char humidityString[5];
    sprintf(humidityString, "%.0f%%", extHumidity);
    display.setCursor(5, TIME_HEIGHT_SHIFT + 48 + 28 + tempShift * 2);
    display.print(humidityString);
  }
  float extPressure = getForecastPressure();
  if (!isnan(extPressure)) {
    char pressureString[5];
    sprintf(pressureString, "%.0f", extPressure * 0.750063755f);
    display.setCursor(64, TIME_HEIGHT_SHIFT + 48 + 28 + tempShift * 2);
    display.print(pressureString);
  }
  drawBattery(765, TIME_HEIGHT_SHIFT);
#if SD_ENABLED == 1
  display.sdCardSleep();
#endif
  display.display();

  display.rtc.clearAlarmFlag();
  // esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  // esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, LOW);
  esp_deep_sleep_start();
}

void loop() {
}
