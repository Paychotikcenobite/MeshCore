#include "ST7789LCDDisplay.h"

#ifndef PIN_TFT_MISO
  #define PIN_TFT_MISO -1
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 3
#endif

#ifndef DISPLAY_SCALE_X
  #define DISPLAY_SCALE_X 2.5f // 320 / 128
#endif

#ifndef DISPLAY_SCALE_Y
  #define DISPLAY_SCALE_Y 3.75f // 240 / 64
#endif

#if defined(LILYGO_TDECK) && defined(MESHCORE_COMPACT_UI)
  #define EFFECTIVE_SCALE_X 1.0f
  #define EFFECTIVE_SCALE_Y 1.0f
#else
  #define EFFECTIVE_SCALE_X DISPLAY_SCALE_X
  #define EFFECTIVE_SCALE_Y DISPLAY_SCALE_Y
#endif

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

bool ST7789LCDDisplay::i2c_probe(TwoWire& wire, uint8_t addr) {
  return true;
}

// Color scheme
#if defined(LILYGO_TDECK) && defined(MESHCORE_COMPACT_UI)
ColorVal UIColor::window_bkg = 0x0882;    // #0B1114
ColorVal UIColor::title_bkg = 0x10C3;     // #11191D
ColorVal UIColor::title_txt = 0xE77E;     // #E6EEF0
ColorVal UIColor::primary_txt = 0xE77E;   // #E6EEF0
ColorVal UIColor::secondary_txt = 0x9515; // #93A3A8
ColorVal UIColor::warning_txt = 0xE549;   // amber
ColorVal UIColor::popup_bkg = 0x2187;     // #223238
ColorVal UIColor::popup_txt = 0xE77E;
ColorVal UIColor::corp_blue = 0x1514;     // muted teal
#else
ColorVal UIColor::window_bkg = ST77XX_WHITE;
ColorVal UIColor::title_bkg = ST77XX_BLUE;
ColorVal UIColor::title_txt = ST77XX_WHITE;
ColorVal UIColor::primary_txt = ST77XX_BLACK;
ColorVal UIColor::secondary_txt = (18 << 11) | (36 << 5) | 18;
ColorVal UIColor::warning_txt = ST77XX_ORANGE;
ColorVal UIColor::popup_bkg = ST77XX_CYAN;
ColorVal UIColor::popup_txt = ST77XX_BLACK;
ColorVal UIColor::corp_blue = 0x001A;
#endif

bool ST7789LCDDisplay::begin() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();

    if (PIN_TFT_LEDA_CTL != -1) {
      pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
      digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
    }

    #if defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT) || defined(HELTEC_V4_R8_TFT)
      displaySPI.begin(PIN_TFT_SCL, PIN_TFT_MISO, PIN_TFT_SDA, PIN_TFT_CS);
    #endif

    display.init(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    display.setRotation(DISPLAY_ROTATION);
    display.setSPISpeed(40e6);
    display.fillScreen(ST77XX_BLACK);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize((uint8_t)(2 * EFFECTIVE_SCALE_X));
    display.cp437(true);

    _isOn = true;
  }

  return true;
}

void ST7789LCDDisplay::turnOn() {
  ST7789LCDDisplay::begin();
}

void ST7789LCDDisplay::turnOff() {
  if (_isOn) {
    if (PIN_TFT_LEDA_CTL != -1) {
      digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
    }
    if (PIN_TFT_RST != -1) {
      digitalWrite(PIN_TFT_RST, LOW);
    }
    if (PIN_TFT_LEDA_CTL != -1) {
      digitalWrite(PIN_TFT_LEDA_CTL, LOW);
    }
    _isOn = false;

    if (_peripher_power) _peripher_power->release();
  }
}

void ST7789LCDDisplay::clear() {
  display.fillScreen(ST77XX_BLACK);
}

void ST7789LCDDisplay::startFrame(ColorVal bkg) {
  display.fillScreen(bkg);
  display.setTextColor(_color = UIColor::primary_txt);
  display.setTextSize((uint8_t)(1 * EFFECTIVE_SCALE_X));
  display.cp437(true);
}

void ST7789LCDDisplay::setTextSize(int sz) {
  int scaled = (int)(sz * EFFECTIVE_SCALE_X);
  if (scaled < 1) scaled = 1;
  display.setTextSize((uint8_t)scaled);
}

void ST7789LCDDisplay::setColor(ColorVal c) {
  display.setTextColor(_color = c);
}

void ST7789LCDDisplay::setCursor(int x, int y) {
  display.setCursor((int)(x * EFFECTIVE_SCALE_X), (int)(y * EFFECTIVE_SCALE_Y));
}

void ST7789LCDDisplay::print(const char* str) {
  display.print(str);
}

void ST7789LCDDisplay::fillRect(int x, int y, int w, int h) {
  display.fillRect((int)(x * EFFECTIVE_SCALE_X), (int)(y * EFFECTIVE_SCALE_Y),
                   (int)(w * EFFECTIVE_SCALE_X), (int)(h * EFFECTIVE_SCALE_Y), _color);
}

void ST7789LCDDisplay::drawRect(int x, int y, int w, int h) {
  display.drawRect((int)(x * EFFECTIVE_SCALE_X), (int)(y * EFFECTIVE_SCALE_Y),
                   (int)(w * EFFECTIVE_SCALE_X), (int)(h * EFFECTIVE_SCALE_Y), _color);
}

void ST7789LCDDisplay::fillRoundRect(int x, int y, int w, int h, int r) {
  display.fillRoundRect((int)(x * EFFECTIVE_SCALE_X), (int)(y * EFFECTIVE_SCALE_Y),
                        (int)(w * EFFECTIVE_SCALE_X), (int)(h * EFFECTIVE_SCALE_Y),
                        (int)(r * EFFECTIVE_SCALE_X), _color);
}

void ST7789LCDDisplay::drawRoundRect(int x, int y, int w, int h, int r) {
  display.drawRoundRect((int)(x * EFFECTIVE_SCALE_X), (int)(y * EFFECTIVE_SCALE_Y),
                        (int)(w * EFFECTIVE_SCALE_X), (int)(h * EFFECTIVE_SCALE_Y),
                        (int)(r * EFFECTIVE_SCALE_X), _color);
}

void ST7789LCDDisplay::drawLine(int x0, int y0, int x1, int y1) {
  display.drawLine((int)(x0 * EFFECTIVE_SCALE_X), (int)(y0 * EFFECTIVE_SCALE_Y),
                   (int)(x1 * EFFECTIVE_SCALE_X), (int)(y1 * EFFECTIVE_SCALE_Y), _color);
}

void ST7789LCDDisplay::fillCircle(int x, int y, int r) {
  display.fillCircle((int)(x * EFFECTIVE_SCALE_X), (int)(y * EFFECTIVE_SCALE_Y),
                     (int)(r * EFFECTIVE_SCALE_X), _color);
}

void ST7789LCDDisplay::drawCircle(int x, int y, int r) {
  display.drawCircle((int)(x * EFFECTIVE_SCALE_X), (int)(y * EFFECTIVE_SCALE_Y),
                     (int)(r * EFFECTIVE_SCALE_X), _color);
}

void ST7789LCDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  uint8_t byteWidth = (w + 7) / 8;

  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      uint8_t byte = bits[j * byteWidth + i / 8];
      bool pixelOn = byte & (0x80 >> (i & 7));

      if (pixelOn) {
#if defined(LILYGO_TDECK) && defined(MESHCORE_COMPACT_UI)
        display.drawPixel(x + i, y + j, _color);
#else
        for (int dy = 0; dy < DISPLAY_SCALE_X; dy++) {
          for (int dx = 0; dx < DISPLAY_SCALE_X; dx++) {
            display.drawPixel(x * DISPLAY_SCALE_X + i * DISPLAY_SCALE_X + dx,
                              y * DISPLAY_SCALE_Y + j * DISPLAY_SCALE_X + dy, _color);
          }
        }
#endif
      }
    }
  }
}

uint16_t ST7789LCDDisplay::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return (uint16_t)(w / EFFECTIVE_SCALE_X);
}

void ST7789LCDDisplay::endFrame() {
}
