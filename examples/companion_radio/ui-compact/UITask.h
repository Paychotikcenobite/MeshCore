#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>
#include "TouchDrvGT911Recovery.hpp"
#include "../AbstractUITask.h"
#include "../NodePrefs.h"

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  unsigned long _next_refresh;
  unsigned long _auto_off;
  unsigned long _alert_expiry;
  unsigned long _keyboard_poll_at;
  unsigned long _trackball_poll_at;
  unsigned long _touch_poll_at;
  unsigned long _touch_last_seen;
  unsigned long _touch_started_at;
  unsigned long _ui_started_at;
  int _msgcount;
  char _alert[80];

  bool _touch_ready;
  bool _touch_down;
  bool _touch_long_sent;
  int16_t _touch_start_x, _touch_start_y;
  int16_t _touch_x, _touch_y;

  UIScreen* home;
  UIScreen* curr;

  void setCurrScreen(UIScreen* c);
  char pollTrackball();
  char pollKeyboard();
  char pollInput();
  bool initTouch();
  bool pollTouch(int16_t& x, int16_t& y, uint8_t& gesture);
  char checkDisplayOn(char c);

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial)
    : AbstractUITask(board, serial), _display(nullptr), _sensors(nullptr), _node_prefs(nullptr),
      _next_refresh(0), _auto_off(0), _alert_expiry(0), _keyboard_poll_at(0),
      _trackball_poll_at(0), _touch_poll_at(0), _touch_last_seen(0), _touch_started_at(0), _ui_started_at(0),
      _msgcount(0), _touch_ready(false), _touch_down(false), _touch_long_sent(false), _touch_start_x(0), _touch_start_y(0),
      _touch_x(0), _touch_y(0), home(nullptr), curr(nullptr) {
    _alert[0] = 0;
  }

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
  void gotoHomeScreen() { setCurrScreen(home); }
  void showAlert(const char* text, int duration_millis);
  int getMsgCount() const { return _msgcount; }
  NodePrefs* getNodePrefs() const { return _node_prefs; }
  bool touchReady() const { return _touch_ready; }

  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;
};
