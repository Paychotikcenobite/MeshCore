#include "UITask.h"
#include <Wire.h>
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS 30000
#endif

#ifndef TDECK_TRACK_RIGHT
  #define TDECK_TRACK_RIGHT 2
#endif
#ifndef TDECK_TRACK_UP
  #define TDECK_TRACK_UP 3
#endif
#ifndef TDECK_TRACK_LEFT
  #define TDECK_TRACK_LEFT 1
#endif
#ifndef TDECK_TRACK_DOWN
  #define TDECK_TRACK_DOWN 15
#endif
#ifndef TDECK_KEYBOARD_ADDR
  #define TDECK_KEYBOARD_ADDR 0x55
#endif

static const ColorVal COMPACT_PANEL = 0x10C3;
static const ColorVal COMPACT_PANEL_2 = 0x1104;
static const ColorVal COMPACT_BORDER = 0x2187;
static const ColorVal COMPACT_TEAL = 0x1514;
static const ColorVal COMPACT_GREEN = 0x3E31;
static const ColorVal COMPACT_AMBER = 0xE549;
static const ColorVal COMPACT_BLUEGRAY = 0x4A8E;

static int batteryPercent(uint16_t mv) {
  const int minMv = 3300;
  const int maxMv = 4200;
  int pct = ((int)mv - minMv) * 100 / (maxMv - minMv);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

static void initialsFor(const char* name, char out[3]) {
  out[0] = '?';
  out[1] = 0;
  out[2] = 0;
  if (!name || !name[0]) return;

  out[0] = name[0];
  const char* p = name;
  while (*p && *p != ' ') p++;
  if (*p == ' ' && p[1]) {
    out[1] = p[1];
    out[2] = 0;
  } else if (name[1]) {
    out[1] = name[1];
    out[2] = 0;
  }
}

class CompactHomeScreen : public UIScreen {
public:
  enum Tab : uint8_t {
    CHATS = 0,
    NODES,
    RADIO,
    SETTINGS,
    TAB_COUNT
  };

  struct MsgEntry {
    uint32_t timestamp;
    uint8_t path_len;
    uint8_t unread;
    char origin[32];
    char text[72];
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;
  uint8_t _tab;
  MsgEntry _messages[4];
  uint8_t _message_count;
  uint8_t _message_head;

  void drawStatus(DisplayDriver& d) {
    d.setColor(UIColor::title_bkg);
    d.fillRect(0, 0, d.width(), 18);

    d.setTextSize(1);
    d.setColor(COMPACT_GREEN);
    d.setCursor(8, 5);
    d.print("o");

    d.setColor(UIColor::title_txt);
    d.setCursor(18, 5);
    d.print("MeshCore");

    char center[24];
    snprintf(center, sizeof(center), "%.1f MHz", _task->getNodePrefs()->freq);
    d.setColor(UIColor::secondary_txt);
    d.drawTextCentered(d.width() / 2, 5, center);

    char right[34];
    snprintf(right, sizeof(right), "%s  %d%%",
             _task->isBluetoothEnabled() ? "BLE" : "--",
             batteryPercent(_task->getBattMilliVolts()));
    d.setColor(UIColor::title_txt);
    d.drawTextRightAlign(d.width() - 8, 5, right);
  }

  void drawHeader(DisplayDriver& d, const char* title, const char* pill) {
    d.setTextSize(2);
    d.setColor(UIColor::primary_txt);
    d.setCursor(8, 24);
    d.print(title);

    d.setTextSize(1);
    int pillW = d.getTextWidth(pill) + 20;
    if (pillW < 52) pillW = 52;
    int pillX = d.width() - pillW - 8;
    d.setColor(COMPACT_PANEL_2);
    d.fillRoundRect(pillX, 23, pillW, 20, 7);
    d.setColor(COMPACT_BORDER);
    d.drawRoundRect(pillX, 23, pillW, 20, 7);
    d.setColor(UIColor::primary_txt);
    d.drawTextCentered(pillX + pillW / 2, 29, pill);
  }

  void drawTabs(DisplayDriver& d) {
    const char* labels[TAB_COUNT] = {"Chats", "Nodes", "Radio", "Settings"};
    const int x0 = 8;
    const int gap = 3;
    const int w = (d.width() - 16 - gap * 3) / 4;
    const int y = 50;
    const int h = 22;

    d.setTextSize(1);
    for (int i = 0; i < TAB_COUNT; ++i) {
      int x = x0 + i * (w + gap);
      d.setColor(i == _tab ? COMPACT_TEAL : COMPACT_PANEL_2);
      d.fillRoundRect(x, y, w, h, 5);
      d.setColor(i == _tab ? COMPACT_TEAL : COMPACT_BORDER);
      d.drawRoundRect(x, y, w, h, 5);
      d.setColor(i == _tab ? UIColor::primary_txt : UIColor::secondary_txt);
      d.drawTextCentered(x + w / 2, y + 7, labels[i]);
    }
  }

  void drawAvatar(DisplayDriver& d, int cx, int cy, ColorVal color, const char* name) {
    d.setColor(color);
    d.fillCircle(cx, cy, 12);
    char initials[3];
    initialsFor(name, initials);
    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    int w = d.getTextWidth(initials);
    d.setCursor(cx - w / 2, cy - 4);
    d.print(initials);
  }

  void drawCardFrame(DisplayDriver& d, int y) {
    d.setColor(COMPACT_PANEL);
    d.fillRoundRect(8, y, d.width() - 16, 30, 7);
    d.setColor(COMPACT_BORDER);
    d.drawRoundRect(8, y, d.width() - 16, 30, 7);
  }

  void drawMessageCard(DisplayDriver& d, const MsgEntry& m, int y, ColorVal avatarColor) {
    drawCardFrame(d, y);
    drawAvatar(d, 25, y + 15, avatarColor, m.origin);

    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    d.drawTextEllipsized(44, y + 5, 178, m.origin);

    int age = (int)(_rtc->getCurrentTime() - m.timestamp);
    char ago[12];
    if (age < 60) snprintf(ago, sizeof(ago), "%ds", age < 0 ? 0 : age);
    else if (age < 3600) snprintf(ago, sizeof(ago), "%dm", age / 60);
    else snprintf(ago, sizeof(ago), "%dh", age / 3600);

    d.setColor(UIColor::secondary_txt);
    d.drawTextRightAlign(d.width() - 18, y + 5, ago);
    d.drawTextEllipsized(44, y + 17, 220, m.text);

    if (m.unread) {
      d.setColor(COMPACT_TEAL);
      d.fillCircle(d.width() - 22, y + 20, 7);
      d.setColor(UIColor::primary_txt);
      char num[4];
      snprintf(num, sizeof(num), "%u", m.unread);
      int w = d.getTextWidth(num);
      d.setCursor(d.width() - 22 - w / 2, y + 16);
      d.print(num);
    }
  }

  void drawNodeCard(DisplayDriver& d, const AdvertPath& a, int y, ColorVal avatarColor) {
    drawCardFrame(d, y);
    drawAvatar(d, 25, y + 15, avatarColor, a.name);

    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    d.drawTextEllipsized(44, y + 5, 205, a.name);

    int age = (int)(_rtc->getCurrentTime() - a.recv_timestamp);
    char ago[12];
    if (age < 60) snprintf(ago, sizeof(ago), "%ds", age < 0 ? 0 : age);
    else if (age < 3600) snprintf(ago, sizeof(ago), "%dm", age / 60);
    else snprintf(ago, sizeof(ago), "%dh", age / 3600);
    d.setColor(UIColor::secondary_txt);
    d.drawTextRightAlign(d.width() - 18, y + 5, ago);

    char detail[52];
    if (a.path_len == 0xFF) {
      snprintf(detail, sizeof(detail), "direct route unknown");
    } else {
      snprintf(detail, sizeof(detail), "%u hop%s", a.path_len, a.path_len == 1 ? "" : "s");
    }
    d.drawTextEllipsized(44, y + 17, 245, detail);
  }

  void drawEmptyCard(DisplayDriver& d, int y, const char* title, const char* subtitle) {
    drawCardFrame(d, y);
    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    d.setCursor(18, y + 6);
    d.print(title);
    d.setColor(UIColor::secondary_txt);
    d.setCursor(18, y + 18);
    d.print(subtitle);
  }

  void drawChats(DisplayDriver& d) {
    char pill[20];
    snprintf(pill, sizeof(pill), "%d unread", _task->getMsgCount());
    drawHeader(d, "Chats", pill);
    drawTabs(d);

    const ColorVal colors[4] = {COMPACT_TEAL, COMPACT_GREEN, COMPACT_BLUEGRAY, COMPACT_AMBER};
    int y = 76;
    int shown = 0;

    for (int i = 0; i < _message_count && shown < 4; ++i) {
      int idx = (_message_head + 4 - i) % 4;
      drawMessageCard(d, _messages[idx], y, colors[shown]);
      y += 33;
      shown++;
    }

    AdvertPath recent[4];
    memset(recent, 0, sizeof(recent));
    int recentCount = the_mesh.getRecentlyHeard(recent, 4);
    for (int i = 0; i < recentCount && shown < 4; ++i) {
      if (!recent[i].name[0]) continue;
      drawNodeCard(d, recent[i], y, colors[shown]);
      y += 33;
      shown++;
    }

    if (shown == 0) {
      drawEmptyCard(d, y, "No conversations yet", "Waiting for MeshCore traffic...");
    }
  }

  void drawNodes(DisplayDriver& d) {
    AdvertPath recent[4];
    memset(recent, 0, sizeof(recent));
    int count = the_mesh.getRecentlyHeard(recent, 4);

    char pill[20];
    snprintf(pill, sizeof(pill), "%d recent", count);
    drawHeader(d, "Nodes", pill);
    drawTabs(d);

    const ColorVal colors[4] = {COMPACT_GREEN, COMPACT_TEAL, COMPACT_BLUEGRAY, COMPACT_AMBER};
    int y = 76;
    int shown = 0;
    for (int i = 0; i < count && shown < 4; ++i) {
      if (!recent[i].name[0]) continue;
      drawNodeCard(d, recent[i], y, colors[shown]);
      y += 33;
      shown++;
    }
    if (shown == 0) {
      drawEmptyCard(d, y, "No recent nodes", "Advert or wait for nearby traffic.");
    }
  }

  void drawRadio(DisplayDriver& d) {
    drawHeader(d, "Radio", "Enter: advert");
    drawTabs(d);

    NodePrefs* p = _task->getNodePrefs();
    const int x = 10;
    const int y = 82;
    const int w = d.width() - 20;
    const int h = 112;

    d.setColor(COMPACT_PANEL);
    d.fillRoundRect(x, y, w, h, 8);
    d.setColor(COMPACT_BORDER);
    d.drawRoundRect(x, y, w, h, 8);

    char buf[48];
    d.setTextSize(1);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 94); d.print("Frequency");
    d.setColor(UIColor::primary_txt);
    snprintf(buf, sizeof(buf), "%.3f MHz", p->freq);
    d.drawTextRightAlign(d.width() - 22, 94, buf);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 116); d.print("LoRa");
    d.setColor(UIColor::primary_txt);
    snprintf(buf, sizeof(buf), "SF%d  BW %.0f  CR%d", p->sf, p->bw, p->cr);
    d.drawTextRightAlign(d.width() - 22, 116, buf);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 138); d.print("TX power");
    d.setColor(UIColor::primary_txt);
    snprintf(buf, sizeof(buf), "%d dBm", p->tx_power_dbm);
    d.drawTextRightAlign(d.width() - 22, 138, buf);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 160); d.print("Noise floor");
    d.setColor(UIColor::primary_txt);
    snprintf(buf, sizeof(buf), "%d dBm", radio_driver.getNoiseFloor());
    d.drawTextRightAlign(d.width() - 22, 160, buf);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 182); d.print("BLE companion");
    d.setColor(_task->isBluetoothEnabled() ? COMPACT_GREEN : UIColor::warning_txt);
    d.drawTextRightAlign(d.width() - 22, 182,
                         _task->isBluetoothEnabled() ? "enabled" : "disabled");
  }

  void drawSettings(DisplayDriver& d) {
    drawHeader(d, "Settings", "Enter: BLE");
    drawTabs(d);

    const int x = 10;
    const int y = 82;
    const int w = d.width() - 20;
    const int h = 112;
    d.setColor(COMPACT_PANEL);
    d.fillRoundRect(x, y, w, h, 8);
    d.setColor(COMPACT_BORDER);
    d.drawRoundRect(x, y, w, h, 8);

    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 94); d.print("Bluetooth");
    d.setColor(_task->isBluetoothEnabled() ? COMPACT_GREEN : UIColor::warning_txt);
    d.drawTextRightAlign(d.width() - 22, 94,
                         _task->isBluetoothEnabled() ? "ON" : "OFF");

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 118); d.print("Battery");
    char batt[24];
    snprintf(batt, sizeof(batt), "%d%%  %u mV",
             batteryPercent(_task->getBattMilliVolts()),
             _task->getBattMilliVolts());
    d.setColor(UIColor::primary_txt);
    d.drawTextRightAlign(d.width() - 22, 118, batt);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 142); d.print("Firmware");
    d.setColor(UIColor::primary_txt);
    d.drawTextRightAlign(d.width() - 22, 142, FIRMWARE_VERSION);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 166); d.print("Input");
    d.setColor(UIColor::primary_txt);
    d.drawTextRightAlign(d.width() - 22, 166, "trackball + keyboard");

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 182); d.print("Keys 1-4 switch tabs");
  }

  void drawFooter(DisplayDriver& d) {
    const int y = 208;
    d.setColor(UIColor::title_bkg);
    d.fillRect(0, y, d.width(), d.height() - y);
    d.setColor(COMPACT_BORDER);
    d.drawLine(0, y, d.width() - 1, y);

    const char* labels[4] = {"Chats", "Nodes", "Radio", "Settings"};
    const int w = d.width() / 4;
    d.setTextSize(1);
    for (int i = 0; i < 4; ++i) {
      if (i == _tab) {
        d.setColor(COMPACT_TEAL);
        d.fillRoundRect(i * w + 8, y + 5, w - 16, 21, 6);
        d.setColor(UIColor::primary_txt);
      } else {
        d.setColor(UIColor::secondary_txt);
      }
      d.drawTextCentered(i * w + w / 2, y + 11, labels[i]);
    }
  }

public:
  CompactHomeScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _tab(CHATS), _message_count(0), _message_head(0) {
    memset(_messages, 0, sizeof(_messages));
  }

  void addMessage(uint8_t path_len, const char* from, const char* text) {
    _message_head = (_message_head + 1) % 4;
    if (_message_count < 4) _message_count++;

    MsgEntry& m = _messages[_message_head];
    memset(&m, 0, sizeof(m));
    m.timestamp = _rtc->getCurrentTime();
    m.path_len = path_len;
    m.unread = 1;
    StrHelper::strncpy(m.origin, from ? from : "Unknown", sizeof(m.origin));
    StrHelper::strncpy(m.text, text ? text : "", sizeof(m.text));
    _tab = CHATS;
  }

  void clearUnread() {
    for (int i = 0; i < 4; ++i) _messages[i].unread = 0;
  }

  int render(DisplayDriver& d) override {
    drawStatus(d);

    switch (_tab) {
      case CHATS: drawChats(d); break;
      case NODES: drawNodes(d); break;
      case RADIO: drawRadio(d); break;
      case SETTINGS: drawSettings(d); break;
      default: _tab = CHATS; drawChats(d); break;
    }

    drawFooter(d);
    return 1000;
  }

  bool handleInput(char c) override {
    if (c == KEY_LEFT) {
      _tab = (_tab + TAB_COUNT - 1) % TAB_COUNT;
      return true;
    }
    if (c == KEY_RIGHT) {
      _tab = (_tab + 1) % TAB_COUNT;
      return true;
    }
    if (c >= '1' && c <= '4') {
      _tab = (Tab)(c - '1');
      return true;
    }
    if (c == KEY_CANCEL) {
      _tab = CHATS;
      return true;
    }
    if (c == KEY_ENTER) {
      if (_tab == RADIO) {
        _task->notify(UIEventType::ack);
        if (the_mesh.advert()) _task->showAlert("Advert sent", 1000);
        else _task->showAlert("Advert failed", 1000);
        return true;
      }
      if (_tab == SETTINGS) {
        if (_task->isBluetoothEnabled()) {
          _task->disableBluetooth();
          _task->showAlert("Bluetooth disabled", 1000);
        } else {
          _task->enableBluetooth();
          _task->showAlert("Bluetooth enabled", 1000);
        }
        return true;
      }
    }
    return false;
  }
};

class CompactSplashScreen : public UIScreen {
  UITask* _task;
  unsigned long _dismiss_at;
public:
  CompactSplashScreen(UITask* task) : _task(task), _dismiss_at(millis() + 1800) {}

  int render(DisplayDriver& d) override {
    d.setColor(COMPACT_TEAL);
    d.fillRoundRect(22, 54, d.width() - 44, 82, 12);

    d.setColor(UIColor::title_bkg);
    d.fillRoundRect(24, 56, d.width() - 48, 78, 10);

    d.setTextSize(2);
    d.setColor(UIColor::primary_txt);
    d.drawTextCentered(d.width() / 2, 70, "MeshCore");

    d.setTextSize(1);
    d.setColor(COMPACT_GREEN);
    d.drawTextCentered(d.width() / 2, 96, "COMMUNICATOR COMPACT");

    d.setColor(UIColor::secondary_txt);
    d.drawTextCentered(d.width() / 2, 116, "LilyGO T-Deck");

    d.setColor(UIColor::secondary_txt);
    d.drawTextCentered(d.width() / 2, 164, FIRMWARE_VERSION);
    return 250;
  }

  void poll() override {
    if (millis() >= _dismiss_at) _task->gotoHomeScreen();
  }

  bool handleInput(char c) override {
    if (c != 0) {
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  _msgcount = 0;
  _ui_started_at = millis();
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _alert_expiry = 0;
  _keyboard_poll_at = 0;
  _trackball_poll_at = 0;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif

  pinMode(TDECK_TRACK_RIGHT, INPUT_PULLUP);
  pinMode(TDECK_TRACK_UP, INPUT_PULLUP);
  pinMode(TDECK_TRACK_LEFT, INPUT_PULLUP);
  pinMode(TDECK_TRACK_DOWN, INPUT_PULLUP);

  if (_display) _display->turnOn();

  home = new CompactHomeScreen(this, &rtc_clock);
  curr = new CompactSplashScreen(this);
  _next_refresh = 0;
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 0;
}

void UITask::showAlert(const char* text, int duration_millis) {
  StrHelper::strncpy(_alert, text ? text : "", sizeof(_alert));
  _alert_expiry = millis() + duration_millis;
  _next_refresh = 0;
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0 && home) {
    ((CompactHomeScreen*)home)->clearUnread();
  }
  _next_refresh = 0;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  if (home) {
    ((CompactHomeScreen*)home)->addMessage(path_len, from_name, text);
    setCurrScreen(home);
  }

  if (_display) {
    if (!_display->isOn() && !hasConnection()) _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 0;
  }
}

void UITask::notify(UIEventType t) {
  (void)t;
}

char UITask::pollTrackball() {
  if (millis() < _trackball_poll_at) return 0;
  _trackball_poll_at = millis() + 8;

  struct TrackState {
    int pin;
    char key;
    bool last;
  };

  static bool initialized = false;
  static TrackState states[4] = {
    {TDECK_TRACK_RIGHT, KEY_RIGHT, true},
    {TDECK_TRACK_UP, KEY_UP, true},
    {TDECK_TRACK_LEFT, KEY_LEFT, true},
    {TDECK_TRACK_DOWN, KEY_DOWN, true},
  };

  if (!initialized) {
    for (auto& s : states) s.last = digitalRead(s.pin);
    initialized = true;
    return 0;
  }

  for (auto& s : states) {
    bool now = digitalRead(s.pin);
    if (s.last && !now) {
      s.last = now;
      return s.key;
    }
    s.last = now;
  }
  return 0;
}

char UITask::pollKeyboard() {
  if (millis() < _keyboard_poll_at) return 0;
  _keyboard_poll_at = millis() + 20;

  Wire.beginTransmission(TDECK_KEYBOARD_ADDR);
  if (Wire.endTransmission() != 0) return 0;

  Wire.requestFrom((uint8_t)TDECK_KEYBOARD_ADDR, (uint8_t)1);
  if (!Wire.available()) return 0;

  char c = (char)Wire.read();
  if (c == 0) return 0;
  if (c == '\n' || c == '\r') return KEY_ENTER;
  if (c == 27) return KEY_CANCEL;

  if (c == 'a' || c == 'A' || c == 'h' || c == 'H') return KEY_LEFT;
  if (c == 'd' || c == 'D' || c == 'l' || c == 'L') return KEY_RIGHT;
  if (c == 'w' || c == 'W' || c == 'k' || c == 'K') return KEY_UP;
  if (c == 's' || c == 'S' || c == 'j' || c == 'J') return KEY_DOWN;

  return c;
}

char UITask::checkDisplayOn(char c) {
  if (_display && !_display->isOn()) {
    _display->turnOn();
    _next_refresh = 0;
    _auto_off = millis() + AUTO_OFF_MILLIS;
    return 0;
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  return c;
}

char UITask::pollInput() {
  char c = 0;

#if defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = KEY_ENTER;
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    if (millis() - _ui_started_at < 8000) {
      the_mesh.enterCLIRescue();
      return 0;
    }
  }
#endif

  if (!c) c = pollTrackball();
  if (!c) c = pollKeyboard();
  if (!c) return 0;
  return checkDisplayOn(c);
}

void UITask::loop() {
  char c = pollInput();
  if (c && curr) {
    curr->handleInput(c);
    _next_refresh = 0;
  }

  if (curr) curr->poll();

  if (_display && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_ms = curr->render(*_display);

      if (millis() < _alert_expiry) {
        const int w = _display->width() - 56;
        const int x = 28;
        const int y = 94;
        _display->setColor(UIColor::popup_bkg);
        _display->fillRoundRect(x, y, w, 48, 10);
        _display->setColor(COMPACT_TEAL);
        _display->drawRoundRect(x, y, w, 48, 10);
        _display->setTextSize(1);
        _display->setColor(UIColor::popup_txt);
        _display->drawTextCentered(_display->width() / 2, y + 20, _alert);
        _next_refresh = _alert_expiry;
      } else {
        _next_refresh = millis() + delay_ms;
      }

      _display->endFrame();
    }

#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) {
      _display->turnOff();
    }
#endif
  }
}
