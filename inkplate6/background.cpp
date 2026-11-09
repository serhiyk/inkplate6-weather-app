#include "background.h"
#include "Inkplate.h"
#include "config.h"
#include <cstring>

#if SD_ENABLED == 1


#define BACKGROUND_SIZE (64)

struct Background {
  uint8_t month_start;
  uint8_t month_end;
  uint8_t day_start;
  uint8_t day_end;
  char file_name[BACKGROUND_FILE_NAME_SIZE];
};

struct BackgroundData {
  Background backgrounds[BACKGROUND_SIZE];
  uint8_t size;
  uint8_t pos;
  bool initialized;
};

RTC_DATA_ATTR BackgroundData backgroundData;

void loadBackgoundFiles(Inkplate &display) {
  if (backgroundData.initialized) {
    return;
  }
  backgroundData.initialized = true;
  backgroundData.size = 0;
  backgroundData.pos = 0;
  if (!display.sdCardInit()) {
    Serial.println("SD Card init error");
    return;
  }

  SdFile root;
  if (!root.open("/", O_RDONLY)) {
    Serial.println("root open error");
    return;
  }

  SdFile file;
  char fileName[BACKGROUND_FILE_NAME_SIZE];
  int i = 0;
  while (file.openNext(&root, O_RDONLY)) {
    if (file.getName(fileName, sizeof(fileName))) {
      if (file.isDir()) {
        Serial.print("Skipped DIR");
      } else {
        // Serial.print("FILE: ");
        // Serial.println(fileName);
        if (i < BACKGROUND_SIZE) {
          unsigned int month_start, month_end, day_start, day_end;
          if (sscanf(fileName, "%u-%u_%u-%u_", &month_start, &month_end, &day_start, &day_end) == 4) {
            backgroundData.backgrounds[i].month_start = month_start;
            backgroundData.backgrounds[i].month_end = month_end;
            backgroundData.backgrounds[i].day_start = day_start;
            backgroundData.backgrounds[i].day_end = day_end;
            std::memcpy(backgroundData.backgrounds[i].file_name, fileName, sizeof(fileName));
            i++;
          } else {
            Serial.println("File name is invalid");
          }
        }
      }
    } else {
      Serial.println("File name to long");
    }
    file.close();
  }
  backgroundData.size = i;

  root.close();
}

bool getBackgoundFile(char *fileName, int month, int day) {
  const int currentPos = backgroundData.pos;
  const int dataSize = backgroundData.size;
  for (int i = currentPos + 1; i < currentPos + dataSize; i++) {
    int idx = i < dataSize ? i : i - dataSize;
    if (month >= backgroundData.backgrounds[idx].month_start &&
        month <= backgroundData.backgrounds[idx].month_end) {
      if (backgroundData.backgrounds[idx].day_start == 0 ||
          (day >= backgroundData.backgrounds[idx].day_start &&
          day <= backgroundData.backgrounds[idx].day_end)) {
        std::memcpy(fileName, backgroundData.backgrounds[idx].file_name, BACKGROUND_FILE_NAME_SIZE);
        backgroundData.pos = idx;
        return true;
      }
    }
  }
  return false;
}

int getBackgoundSize(void) {
  return backgroundData.size;
}

int getBackgoundPos(void) {
  return backgroundData.pos;
}


#else


struct BackgroundInt {
  const unsigned char *data;
  uint16_t width;
  uint16_t height;
  uint8_t month_start;
  uint8_t month_end;
  uint8_t day_start;
  uint8_t day_end;
};

#include <image.h>

static RTC_DATA_ATTR int pos;

bool getBackgoundData(const unsigned char **data, int *width, int *height, int month, int day) {
  const int currentPos = pos;
  const int dataSize = sizeof(backgroundImages) / sizeof(backgroundImages[0]);
  for (int i = currentPos + 1; i < currentPos + dataSize; i++) {
    int idx = i < dataSize ? i : i - dataSize;
    if (month >= backgroundImages[idx].month_start &&
        month <= backgroundImages[idx].month_end) {
      if (backgroundImages[idx].day_start == 0 ||
          (day >= backgroundImages[idx].day_start &&
          day <= backgroundImages[idx].day_end)) {
        *data = backgroundImages[idx].data;
        *width = backgroundImages[idx].width;
        *height = backgroundImages[idx].height;
        pos = idx;
        return true;
      }
    }
  }
  return false;
}

#endif
