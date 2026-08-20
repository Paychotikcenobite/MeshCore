#pragma once

// Compact-only wrapper around the exact GT911 driver lineage used by LilyGO's
// standard T-Deck example. include_next resolves the SensorLib header supplied
// by this PlatformIO environment, then the macro at the bottom substitutes the
// wrapper only in files that explicitly include this local header.
#include_next <TouchDrvGT911.hpp>
#include <Arduino.h>
#include <Wire.h>

#ifndef COMPACT_TDECK_POWERON
#define COMPACT_TDECK_POWERON 10
#endif
#ifndef COMPACT_TDECK_TOUCH_INT
#define COMPACT_TDECK_TOUCH_INT 16
#endif
#ifndef COMPACT_TDECK_I2C_SDA
#define COMPACT_TDECK_I2C_SDA 18
#endif
#ifndef COMPACT_TDECK_I2C_SCL
#define COMPACT_TDECK_I2C_SCL 8
#endif
#ifndef COMPACT_TDECK_KEYBOARD_ADDR
#define COMPACT_TDECK_KEYBOARD_ADDR 0x55
#endif

class CompactTDeckGT911 : public TouchDrvGT911 {
  TwoWire* _compact_wire = nullptr;
  uint8_t _compact_addr = GT911_SLAVE_ADDRESS_L;
  bool _compact_raw_fallback = false;
  bool _compact_swap_xy = false;
  bool _compact_mirror_x = false;
  bool _compact_mirror_y = false;
  uint16_t _compact_max_x = 0;
  uint16_t _compact_max_y = 0;
  uint16_t _compact_fw = 0;

  static bool probe(TwoWire& wire, uint8_t addr, uint8_t* code = nullptr) {
    wire.beginTransmission(addr);
    uint8_t rc = wire.endTransmission();
    if (code) *code = rc;
    return rc == 0;
  }

  static bool readRegs(TwoWire& wire, uint8_t addr, uint16_t reg, uint8_t* out, size_t len) {
    if (!out || !len) return false;
    wire.beginTransmission(addr);
    wire.write((uint8_t)(reg >> 8));
    wire.write((uint8_t)(reg & 0xFF));
    if (wire.endTransmission(true) != 0) return false;
    size_t got = wire.requestFrom((uint8_t)addr, (uint8_t)len);
    if (got != len) {
      while (wire.available()) (void)wire.read();
      return false;
    }
    for (size_t i = 0; i < len; ++i) {
      if (!wire.available()) return false;
      out[i] = (uint8_t)wire.read();
    }
    return true;
  }

  static bool writeReg8(TwoWire& wire, uint8_t addr, uint16_t reg, uint8_t value) {
    wire.beginTransmission(addr);
    wire.write((uint8_t)(reg >> 8));
    wire.write((uint8_t)(reg & 0xFF));
    wire.write(value);
    return wire.endTransmission(true) == 0;
  }

  static bool readProduct(TwoWire& wire, uint8_t addr, char id[5], uint16_t* fw = nullptr) {
    uint8_t raw[4] = {0};
    id[0] = id[1] = id[2] = id[3] = '?';
    id[4] = 0;
    if (!readRegs(wire, addr, 0x8140, raw, sizeof(raw))) return false;
    for (int i = 0; i < 4; ++i) id[i] = (char)raw[i];
    id[4] = 0;
    if (fw) {
      uint8_t f[2] = {0};
      if (readRegs(wire, addr, 0x8144, f, sizeof(f))) *fw = (uint16_t)f[0] | ((uint16_t)f[1] << 8);
      else *fw = 0;
    }
    return id[0] == '9' && id[1] == '1' && id[2] == '1';
  }

  uint8_t rawGetPoint(int16_t* x_array, int16_t* y_array, uint8_t size) {
    if (!_compact_wire || !x_array || !y_array || size == 0) return 0;
    uint8_t status = 0;
    if (!readRegs(*_compact_wire, _compact_addr, 0x814E, &status, 1)) return 0;
    uint8_t count = status & 0x0F;
    if (count == 0 || count > 5) {
      if (count > 5) (void)writeReg8(*_compact_wire, _compact_addr, 0x814E, 0x00);
      return 0;
    }

    uint8_t point[8] = {0};
    if (!readRegs(*_compact_wire, _compact_addr, 0x814F, point, sizeof(point))) {
      (void)writeReg8(*_compact_wire, _compact_addr, 0x814E, 0x00);
      return 0;
    }
    (void)writeReg8(*_compact_wire, _compact_addr, 0x814E, 0x00);

    int16_t x = (int16_t)((uint16_t)point[1] | ((uint16_t)point[2] << 8));
    int16_t y = (int16_t)((uint16_t)point[3] | ((uint16_t)point[4] << 8));
    if (_compact_swap_xy) { int16_t t = x; x = y; y = t; }
    if (_compact_mirror_x && _compact_max_x) x = (int16_t)_compact_max_x - x;
    if (_compact_mirror_y && _compact_max_y) y = (int16_t)_compact_max_y - y;
    x_array[0] = x;
    y_array[0] = y;
    return 1;
  }

public:
  bool begin(TwoWire& wire,
             uint8_t addr = GT911_SLAVE_ADDRESS_H,
             int sda = COMPACT_TDECK_I2C_SDA,
             int scl = COMPACT_TDECK_I2C_SCL) {
    _compact_wire = &wire;
    _compact_raw_fallback = false;
    _compact_fw = 0;

    pinMode(COMPACT_TDECK_POWERON, OUTPUT);
    digitalWrite(COMPACT_TDECK_POWERON, HIGH);
    pinMode(COMPACT_TDECK_TOUCH_INT, INPUT);

    // Re-establish the shared T-Deck I2C bus deterministically before probing.
    wire.end();
    delay(10);
    wire.begin(sda, scl, 100000);
    delay(25);

    uint8_t rc55 = 0xFF, rc5d = 0xFF, rc14 = 0xFF;
    bool kb = probe(wire, COMPACT_TDECK_KEYBOARD_ADDR, &rc55);
    bool p5d = probe(wire, GT911_SLAVE_ADDRESS_L, &rc5d);
    bool p14 = probe(wire, GT911_SLAVE_ADDRESS_H, &rc14);
    char id5d[5] = "----", id14[5] = "----";
    uint16_t fw5d = 0, fw14 = 0;
    bool id5dok = p5d && readProduct(wire, GT911_SLAVE_ADDRESS_L, id5d, &fw5d);
    bool id14ok = p14 && readProduct(wire, GT911_SLAVE_ADDRESS_H, id14, &fw14);

    Serial.printf("[compact-touch] raw probe KB55=%s(rc=%u) 5D=%s(rc=%u,id=%s,fw=%04X) 14=%s(rc=%u,id=%s,fw=%04X) INT=%d\n",
                  kb ? "ACK" : "NACK", rc55,
                  p5d ? "ACK" : "NACK", rc5d, p5d ? id5d : "----", fw5d,
                  p14 ? "ACK" : "NACK", rc14, p14 ? id14 : "----", fw14,
                  digitalRead(COMPACT_TDECK_TOUCH_INT));

    bool driver_ok = TouchDrvGT911::begin(wire, addr, sda, scl);
    if (driver_ok) {
      Serial.println("[compact-touch] SensorLib GT911 path ready");
      return true;
    }

    // If the product-ID registers prove the controller is alive, retain touch
    // via direct GT911 point reads rather than failing the whole UI.
    if (id5dok || id14ok) {
      _compact_addr = id5dok ? GT911_SLAVE_ADDRESS_L : GT911_SLAVE_ADDRESS_H;
      _compact_fw = id5dok ? fw5d : fw14;
      _compact_raw_fallback = true;
      Serial.printf("[compact-touch] SensorLib rejected init; raw fallback active at 0x%02X\n", _compact_addr);
      return true;
    }

    Serial.println("[compact-touch] no readable GT911 product ID on 0x5D or 0x14");
    return false;
  }

  uint8_t getPoint(int16_t* x_array, int16_t* y_array, uint8_t size = 1) {
    return _compact_raw_fallback ? rawGetPoint(x_array, y_array, size)
                                 : TouchDrvGT911::getPoint(x_array, y_array, size);
  }

  uint32_t getChipID() { return _compact_raw_fallback ? 911 : TouchDrvGT911::getChipID(); }
  uint16_t getFwVersion() { return _compact_raw_fallback ? _compact_fw : TouchDrvGT911::getFwVersion(); }

  void setMaxCoordinates(uint16_t x, uint16_t y) {
    _compact_max_x = x; _compact_max_y = y;
    TouchDrvGT911::setMaxCoordinates(x, y);
  }
  void setSwapXY(bool swap) {
    _compact_swap_xy = swap;
    TouchDrvGT911::setSwapXY(swap);
  }
  void setMirrorXY(bool mirrorX, bool mirrorY) {
    _compact_mirror_x = mirrorX; _compact_mirror_y = mirrorY;
    TouchDrvGT911::setMirrorXY(mirrorX, mirrorY);
  }
};

#define TouchDrvGT911 CompactTDeckGT911
