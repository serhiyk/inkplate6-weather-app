#ifndef BACKGROUND_H
#define BACKGROUND_H

#include "Inkplate.h"

#define BACKGROUND_FILE_NAME_SIZE (32)

void loadBackgoundFiles(Inkplate &display);
bool getBackgoundFile(char *fileName, int month, int day);
int getBackgoundSize(void);
int getBackgoundPos(void);

bool getBackgoundData(const unsigned char **data, int *width, int *height, int month, int day);

#endif
