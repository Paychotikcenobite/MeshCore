#!/usr/bin/env python3
from pathlib import Path

path = Path("examples/companion_radio/ui-compact/UITask.cpp")
text = path.read_text()

start = text.index("bool UITask::initTouch() {")
end = text.index("char UITask::pollTrackball()", start)

replacement = r'''bool UITask::initTouch() {
  // Re-assert the T-Deck shared I2C bus at the exact point touch is initialized.
  // This mirrors LILYGO/Launcher behavior rather than relying on an earlier board init.
  Wire.begin(18, 8);
  delay(5);

  const uint8_t candidates[2] = {0x5D, 0x14};
  uint8_t id[4] = {0, 0, 0, 0};
  for (int i = 0; i < 2; ++i) {
    _touch_addr = candidates[i];
    Wire.beginTransmission(_touch_addr);
    if (Wire.endTransmission() == 0 && gtRead(0x8140, id, sizeof(id))) {
      _touch_down = false;
      MESH_DEBUG_PRINTLN("GT911 touch found at 0x%02X, id=%c%c%c%c", _touch_addr,
                         id[0], id[1], id[2], id[3]);
      return true;
    }
  }
  _touch_addr = 0;
  MESH_DEBUG_PRINTLN("GT911 touch not found");
  return false;
}

bool UITask::pollTouch(int16_t& x, int16_t& y, uint8_t& gesture) {
  if (!_touch_addr || millis() < _touch_poll_at) return false;
  _touch_poll_at = millis() + 12;

  uint8_t status = 0;
  if (!gtRead(0x814E, &status, 1)) return false;

  // LILYGO's working TouchDrvGT911 reads the low-nibble point count directly;
  // it does not require bit 7 (buffer-ready) to be set. Requiring bit 7 caused
  // valid T-Deck touches to be discarded in the previous Compact builds.
  uint8_t count = status & 0x0F;

  if (count > 0 && count <= 5) {
    uint8_t p[8];
    if (gtRead(0x814F, p, sizeof(p))) {
      int16_t rawX = (int16_t)(p[1] | (p[2] << 8));
      int16_t rawY = (int16_t)(p[3] | (p[4] << 8));

      // MeshCore's ST7789 driver uses DISPLAY_ROTATION=3. Launcher maps the
      // standard (non-Plus) T-Deck at rotation 3 with swapXY and no mirroring.
      _touch_x = rawY;
      _touch_y = rawX;
      if (_touch_x < 0) _touch_x = 0;
      if (_touch_x > 319) _touch_x = 319;
      if (_touch_y < 0) _touch_y = 0;
      if (_touch_y > 239) _touch_y = 239;

      if (!_touch_down) {
        _touch_start_x = _touch_x;
        _touch_start_y = _touch_y;
        _touch_down = true;
      }
      _touch_last_seen = millis();
    }
    gtWriteByte(0x814E, 0);  // clear GT911 point buffer, as the official driver does
    return false;
  }

  // The official driver clears the point buffer on every poll, not only when
  // the buffer-ready flag is set.
  gtWriteByte(0x814E, 0);

  if (_touch_down) {
    _touch_down = false;
    x = _touch_x;
    y = _touch_y;
    int dx = _touch_x - _touch_start_x;
    int dy = _touch_y - _touch_start_y;
    if (abs(dx) < 28 && abs(dy) < 28) gesture = TOUCH_TAP;
    else if (abs(dy) >= abs(dx)) gesture = dy < 0 ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
    else gesture = dx < 0 ? TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT;
    return true;
  }

  return false;
}

'''

text = text[:start] + replacement + text[end:]
path.write_text(text)
print(f"Patched {path} for GT911 v4 touch behavior")
