#pragma once

#include <stdint.h>
#include <string.h>

using ColorVal = uint16_t;

class UIColor {
public:
  // color definitions (by element _type_)
  static ColorVal window_bkg, title_bkg, title_txt, primary_txt, secondary_txt, warning_txt, popup_bkg, popup_txt, corp_blue;
};

class DisplayDriver {
  int _w, _h;
protected:
  DisplayDriver(int w, int h) { _w = w; _h = h; }
public:
  int width() const { return _w; }
  int height() const { return _h; }

  virtual bool isOn() = 0;
  virtual bool isEink() { return false; }
  virtual void turnOn() = 0;
  virtual void turnOff() = 0;
  virtual void clear() = 0;
  virtual void startFrame(ColorVal bkg = UIColor::window_bkg) = 0;
  virtual void setTextSize(int sz) = 0;
  virtual void setColor(ColorVal c) = 0;
  virtual void setCursor(int x, int y) = 0;
  virtual void print(const char* str) = 0;
  virtual void printWordWrap(const char* str, int max_width) { print(str); }
  virtual void fillRect(int x, int y, int w, int h) = 0;
  virtual void drawRect(int x, int y, int w, int h) = 0;

  // Optional richer primitives. Drivers that do not override these retain
  // conservative rectangular fallbacks, so existing boards are unaffected.
  virtual void fillRoundRect(int x, int y, int w, int h, int r) { fillRect(x, y, w, h); }
  virtual void drawRoundRect(int x, int y, int w, int h, int r) { drawRect(x, y, w, h); }
  virtual void drawLine(int x0, int y0, int x1, int y1) {
    if (x0 == x1) {
      int y = y0 < y1 ? y0 : y1;
      int h = (y0 < y1 ? y1 - y0 : y0 - y1) + 1;
      fillRect(x0, y, 1, h);
    } else if (y0 == y1) {
      int x = x0 < x1 ? x0 : x1;
      int w = (x0 < x1 ? x1 - x0 : x0 - x1) + 1;
      fillRect(x, y0, w, 1);
    }
  }
  virtual void fillCircle(int x, int y, int r) { fillRect(x-r, y-r, r*2+1, r*2+1); }
  virtual void drawCircle(int x, int y, int r) { drawRect(x-r, y-r, r*2+1, r*2+1); }

  virtual void drawXbm(int x, int y, const uint8_t* bits, int w, int h) = 0;
  virtual uint16_t getTextWidth(const char* str) = 0;
  virtual void drawTextCentered(int mid_x, int y, const char* str) {
    int w = getTextWidth(str);
    setCursor(mid_x - w/2, y);
    print(str);
  }
  virtual void drawTextRightAlign(int x_anch, int y, const char* str) {
    int w = getTextWidth(str);
    setCursor(x_anch - w, y);
    print(str);
  }
  virtual void drawTextLeftAlign(int x_anch, int y, const char* str) {
    setCursor(x_anch, y);
    print(str);
  }

  // convert UTF-8 characters to displayable block characters for compatibility
  virtual void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] != 0 && j < dest_size - 1; i++) {
      unsigned char c = (unsigned char)src[i];
      if (c >= 32 && c <= 126) {
        dest[j++] = c;
      } else if (c >= 0x80) {
        dest[j++] = '\xDB';
        while (src[i+1] && (src[i+1] & 0xC0) == 0x80)
          i++;
      }
    }
    dest[j] = 0;
  }

  // draw text with ellipsis if it exceeds max_width
  virtual void drawTextEllipsized(int x, int y, int max_width, const char* str) {
    char temp_str[256];
    size_t len = strlen(str);
    if (len >= sizeof(temp_str)) len = sizeof(temp_str) - 1;
    memcpy(temp_str, str, len);
    temp_str[len] = 0;

    if (getTextWidth(temp_str) <= max_width) {
      setCursor(x, y);
      print(temp_str);
      return;
    }

    const char* ellipsis;
    int i_width = getTextWidth("i");
    int l_width = getTextWidth("l");
    if (i_width != l_width) {
      ellipsis = "... ";
    } else {
      ellipsis = "...";
    }

    int ellipsis_width = getTextWidth(ellipsis);
    int str_len = strlen(temp_str);

    while (str_len > 0 && getTextWidth(temp_str) > max_width - ellipsis_width) {
      temp_str[--str_len] = 0;
    }
    strcat(temp_str, ellipsis);

    setCursor(x, y);
    print(temp_str);
  }

  virtual void endFrame() = 0;
};
