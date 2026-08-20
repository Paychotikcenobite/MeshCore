#include "UITask.h"
#include "CommunicatorAppScreen.h"
#include <Wire.h>
#include <TouchDrvGT911.hpp>
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#include <math.h>

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS 60000
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
#ifndef TDECK_TOUCH_INT
  #define TDECK_TOUCH_INT 16
#endif

static const ColorVal ALERT_BG = 0x1127;
static const ColorVal ALERT_BORDER = 0x11EF;
static const ColorVal ALERT_TEXT = 0xFFFF;

// Preserve the physically validated v8 GT911 recovery wrapper and LilyGO driver path.
static TouchDrvGT911 tdeck_touch;

void UITask::begin(DisplayDriver* display, SensorManager* sensors_ptr, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors_ptr;
  _node_prefs = node_prefs;
  _msgcount = 0;
  _ui_started_at = millis();
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _alert_expiry = 0;
  _keyboard_poll_at = _trackball_poll_at = _touch_poll_at = 0;
  _touch_down = false;
  _touch_long_sent = false;
  _touch_started_at = 0;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif

  pinMode(TDECK_TRACK_RIGHT, INPUT_PULLUP);
  pinMode(TDECK_TRACK_UP, INPUT_PULLUP);
  pinMode(TDECK_TRACK_LEFT, INPUT_PULLUP);
  pinMode(TDECK_TRACK_DOWN, INPUT_PULLUP);
  pinMode(TDECK_TOUCH_INT, INPUT);

  if (_display) _display->turnOn();
  initTouch();

  home = new CommunicatorAppScreen(this, &rtc_clock);
  curr = home;
  CommunicatorAppScreen* app = (CommunicatorAppScreen*)home;
  app->persistenceBegin();
  // Rehydrate contacts created directly on the standalone T-Deck after the
  // MeshCore store has completed its normal startup load.
  app->manualContactsBegin();
  _next_refresh = 0;
  showAlert(_touch_ready ? "GT911 touch ready" : "GT911 TOUCH FAILED", _touch_ready ? 900 : 3000);
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
    CommunicatorAppScreen* app = (CommunicatorAppScreen*)home;
    app->clearUnread();
    app->persistenceCheckpoint(true);
  }
  _next_refresh = 0;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  CommunicatorAppScreen* app = home ? (CommunicatorAppScreen*)home : nullptr;
  if (app) {
    app->addMessage(path_len, from_name, text);
    app->persistenceCheckpoint(true);
  }
  bool wake = !app || app->shouldWakeForMessage(from_name);
  if (_display && wake) {
    if (!_display->isOn()) _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 0;
  }
}

void UITask::notify(UIEventType t) {
  (void)t;
  if (home) ((CommunicatorAppScreen*)home)->markAllDirty();
  _next_refresh = 0;
}

bool UITask::initTouch() {
  // Do not disturb the v8 hardware path: standard T-Deck shared I2C on 18/8,
  // GT911 INT on GPIO16, no reset pin, recovery wrapper around LilyGO's driver.
  pinMode(TDECK_TOUCH_INT, INPUT);
  tdeck_touch.setPins(-1, TDECK_TOUCH_INT);
  _touch_ready = tdeck_touch.begin(Wire, GT911_SLAVE_ADDRESS_L, 18, 8);
  if (!_touch_ready) {
    Serial.println("[compact-touch] GT911 init FAILED");
    _touch_down = false;
    return false;
  }

  tdeck_touch.setMaxCoordinates(320, 240);
  tdeck_touch.setSwapXY(true);
  tdeck_touch.setMirrorXY(false, false);
  _touch_down = false;
  _touch_long_sent = false;
  Serial.printf("[compact-touch] GT911 ready id=%lu fw=0x%04X\n",
                (unsigned long)tdeck_touch.getChipID(), tdeck_touch.getFwVersion());
  return true;
}

bool UITask::pollTouch(int16_t& x, int16_t& y, uint8_t& gesture) {
  if (!_touch_ready || millis() < _touch_poll_at) return false;
  _touch_poll_at = millis() + 12;

  int16_t px = 0, py = 0;
  uint8_t touched = tdeck_touch.getPoint(&px, &py, 1);
  if (touched > 0) {
    _touch_x = px;
    _touch_y = py;
    if (_touch_x < 0) _touch_x = 0;
    if (_touch_x > 319) _touch_x = 319;
    if (_touch_y < 0) _touch_y = 0;
    if (_touch_y > 239) _touch_y = 239;

    if (!_touch_down) {
      _touch_start_x = _touch_x;
      _touch_start_y = _touch_y;
      _touch_started_at = millis();
      _touch_long_sent = false;
      _touch_down = true;
    }
    _touch_last_seen = millis();

    if (!_touch_long_sent && millis() - _touch_started_at >= 650) {
      int dx = _touch_x - _touch_start_x;
      int dy = _touch_y - _touch_start_y;
      if (abs(dx) < 18 && abs(dy) < 18) {
        _touch_long_sent = true;
        x = _touch_x;
        y = _touch_y;
        gesture = COMPACT_TOUCH_LONG_PRESS;
        return true;
      }
    }
    return false;
  }

  // Preserve the v8 release classifier exactly for normal taps/swipes.
  if (_touch_down && millis() - _touch_last_seen > 70) {
    _touch_down = false;
    x = _touch_x;
    y = _touch_y;
    if (_touch_long_sent) {
      _touch_long_sent = false;
      return false;
    }
    int dx = _touch_x - _touch_start_x;
    int dy = _touch_y - _touch_start_y;
    if (abs(dx) < 28 && abs(dy) < 28) gesture = COMPACT_TOUCH_TAP;
    else if (abs(dy) >= abs(dx)) gesture = dy < 0 ? COMPACT_TOUCH_SWIPE_UP : COMPACT_TOUCH_SWIPE_DOWN;
    else gesture = dx < 0 ? COMPACT_TOUCH_SWIPE_LEFT : COMPACT_TOUCH_SWIPE_RIGHT;
    return true;
  }
  return false;
}

char UITask::pollTrackball() {
  if (millis() < _trackball_poll_at) return 0;
  _trackball_poll_at = millis() + 8;
  struct S { int pin; char key; bool last; };
  static bool init = false;
  static S s[4] = {
    {TDECK_TRACK_RIGHT, KEY_RIGHT, true},
    {TDECK_TRACK_UP, KEY_UP, true},
    {TDECK_TRACK_LEFT, KEY_LEFT, true},
    {TDECK_TRACK_DOWN, KEY_DOWN, true}
  };
  if (!init) {
    for (auto& v : s) v.last = digitalRead(v.pin);
    init = true;
    return 0;
  }
  for (auto& v : s) {
    bool now = digitalRead(v.pin);
    if (v.last && !now) { v.last = now; return v.key; }
    v.last = now;
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
  if (!c) return 0;
  if (c == '\n' || c == '\r') return KEY_ENTER;
  if (c == 27) return KEY_CANCEL;
  return c;
}

char UITask::checkDisplayOn(char c) {
  if (_display && !_display->isOn()) {
    _display->turnOn();
    if (home) ((CommunicatorAppScreen*)home)->markAllDirty();
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
  if (ev == BUTTON_EVENT_CLICK) c = KEY_ENTER;
  else if (ev == BUTTON_EVENT_LONG_PRESS && millis() - _ui_started_at < 8000) {
    the_mesh.enterCLIRescue();
    return 0;
  }
#endif
  if (!c) c = pollTrackball();
  if (!c) c = pollKeyboard();
  if (!c) return 0;
  return checkDisplayOn(c);
}

void UITask::loop() {
  CommunicatorAppScreen* app = home ? (CommunicatorAppScreen*)home : nullptr;

  if (_alert_expiry && millis() >= _alert_expiry) {
    _alert_expiry = 0;
    if (app) app->markAllDirty();
    _next_refresh = 0;
  }

  int16_t tx = 0, ty = 0;
  uint8_t gesture = COMPACT_TOUCH_TAP;
  if (pollTouch(tx, ty, gesture)) {
    if (_display && !_display->isOn()) {
      _display->turnOn();
      if (app) app->markAllDirty();
    } else if (app) {
      app->persistenceCheckpoint(true);

      bool handled = false;
      // Message actions and Add Contact are modal. They get first refusal so
      // taps cannot leak through to chat/header navigation underneath them.
      if (app->messageActionActive()) {
        handled = app->handleMessageActionTouch(tx, ty, gesture);
      } else if (app->contactAddActive()) {
        handled = app->handleContactAddTouch(tx, ty, gesture);
      } else {
        // A pending local reply chip is itself tappable to cancel.
        handled = app->handleReplyTouch(tx, ty, gesture);
        // Replace the old Piece-4 placeholder long-press with the real modal.
        if (!handled) handled = app->tryOpenMessageActions(tx, ty, gesture);
        // Intercept Add contact before the older New Conversation placeholder.
        if (!handled) handled = app->tryBeginContactAdd(tx, ty, gesture);
        if (!handled && gesture == COMPACT_TOUCH_TAP && ty <= 42) {
          if (tx >= 277) { app->openSettingsSingleTop(); handled = true; }
          else if (tx >= 234) { app->openRadioSingleTop(); handled = true; }
        }
        if (!handled && gesture == COMPACT_TOUCH_TAP && tx < 42 && ty >= 44 && ty <= 78) {
          app->navigateBack();
          handled = true;
        }
        if (!handled) handled = app->handlePersistentDataTouch(tx, ty, gesture);
        if (!handled) app->handleTouch(tx, ty, gesture);
      }
      app->reconcileDirectSendState();
      // If the touch just sent a reply, force the new message ID/reply_to link
      // immediately so a fast second send cannot steal the pending reference.
      app->persistenceCheckpoint(app->replyPending());
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 0;
  }

  char c = pollInput();
  if (c && curr) {
    if (app && c == KEY_CANCEL) app->persistenceCheckpoint(true);
    if (app && app->messageActionActive()) {
      app->handleMessageActionInput(c);
    } else if (app && app->contactAddActive()) {
      app->handleContactAddInput(c);
    } else if (app && app->replyPending() && c == KEY_CANCEL) {
      app->clearPendingReply();
      showAlert("Reply reference cancelled", 800);
    } else {
      curr->handleInput(c);
    }
    if (app) {
      app->reconcileDirectSendState();
      bool force_reply_bind = c == KEY_ENTER && app->replyPending();
      app->persistenceCheckpoint(force_reply_bind);
    }
    _next_refresh = 0;
  }
  if (curr) curr->poll();
  if (app) {
    app->reconcileDirectSendState();
    app->persistenceCheckpoint(false);
  }

  if (_display && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      // Capture dirty state before render(), because render clears it. Fluent
      // 4-bit masks are only necessary on full frames, not composer-only text
      // updates. This keeps typing responsive despite true edge blending.
      bool full_visual = app && app->fullVisualRedrawPending();
      _display->startFrame();
      int delay_ms = curr->render(*_display);
      if (app) app->drawDirectSendOverlay(*_display);
      if (app) app->drawReplyComposerOverlay(*_display);
      if (app && app->messageActionActive()) app->drawMessageActionOverlay(*_display);
      if (app && app->contactAddActive()) {
        app->drawContactAddOverlay(*_display);
        full_visual = true;
      }
      if (app && full_visual) app->redrawHeaderActionIconsFluent(*_display);
      if (_alert_expiry) {
        const int x = 38, y = 95, w = 244, h = 46;
        _display->setColor(ALERT_BG);
        _display->fillRoundRect(x, y, w, h, 8);
        _display->setColor(ALERT_BORDER);
        _display->drawRoundRect(x, y, w, h, 8);
        _display->setTextSize(1);
        _display->setColor(ALERT_TEXT);
        _display->drawTextCentered(160, y + 18, _alert);
        _next_refresh = _alert_expiry;
      } else {
        _next_refresh = millis() + (unsigned long)delay_ms;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) {
      if (app) app->persistenceCheckpoint(true);
      _display->turnOff();
    }
#endif
  }
}