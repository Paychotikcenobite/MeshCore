#pragma once

#include <helpers/ui/DisplayDriver.h>

namespace CompactAA {

enum FontRole : uint8_t {
  REGULAR_9 = 0,
  MEDIUM_11 = 1,
};

int textWidth(const char* s, FontRole role = REGULAR_9);
int lineHeight(FontRole role = REGULAR_9);
void text(DisplayDriver& d, int x, int y, const char* s, ColorVal fg, ColorVal bg,
          FontRole role = REGULAR_9);
void textCentered(DisplayDriver& d, int cx, int y, const char* s, ColorVal fg, ColorVal bg,
                  FontRole role = REGULAR_9);
void textRight(DisplayDriver& d, int right, int y, const char* s, ColorVal fg, ColorVal bg,
               FontRole role = REGULAR_9);
void textEllipsized(DisplayDriver& d, int x, int y, int maxw, const char* s, ColorVal fg, ColorVal bg,
                    FontRole role = REGULAR_9);
void circle(DisplayDriver& d, int cx, int cy, int r, ColorVal fg, ColorVal bg);
void backIcon(DisplayDriver& d, int x, int y, ColorVal fg, ColorVal bg);
void searchIcon(DisplayDriver& d, int x, int y, ColorVal fg, ColorVal bg);
void chevronIcon(DisplayDriver& d, int x, int y, ColorVal fg, ColorVal bg);

} // namespace CompactAA
