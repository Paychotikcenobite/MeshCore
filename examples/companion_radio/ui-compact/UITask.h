#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>
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
  unsigned long _ui_started_at;
  int _msgcount;
  char _alert[80];

  UIScreen* home;
  UIScreen* curr;

  void setCurrScreen(UIScreen* c);
  char pollInput();
  char pollTrackball();
  char pollKeyboard();
  char checkDisplayOn(char c);

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial)
    : AbstractUITask(board, serial), _display(nullptr), _sensors(nullptr), _node_prefs(nullptr),
      _next_refresh(0), _auto_off(0), _alert_expiry(0), _keyboard_poll_at(0),
      _trackball_poll_at(0), _ui_started_at(0), _msgcount(0), home(nullptr), curr(nullptr) {
    _alert[0] = 0;
  }

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
  void gotoHomeScreen() { setCurrScreen(home); }
  void showAlert(const char* text, int duration_millis);
  int getMsgCount() const { return _msgcount; }
  NodePrefs* getNodePrefs() const { return _node_prefs; }

  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;
};
