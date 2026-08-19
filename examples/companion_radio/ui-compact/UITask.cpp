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
static const int CARD_Y = 76;
static const int CARD_H = 30;
static const int CARD_STEP = 33;
static const int MAX_VISIBLE_ITEMS = 4;

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
    CHANNELS,
    NODES,
    MAP,
    TAB_COUNT
  };

  enum View : uint8_t {
    HOME = 0,
    CONTACTS,
    RADIO,
    SETTINGS,
    COMPOSE_CONTACT,
    COMPOSE_CHANNEL
  };

  struct MsgEntry {
    uint32_t timestamp;
    uint8_t path_len;
    uint8_t unread;
    bool outgoing;
    char origin[32];
    char text[72];
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;
  uint8_t _tab;
  uint8_t _view;
  uint8_t _selected;

  MsgEntry _messages[4];
  uint8_t _message_count;
  uint8_t _message_head;

  ContactInfo _contacts[MAX_VISIBLE_ITEMS];
  uint8_t _contact_count;

  ChannelDetails _channels[MAX_VISIBLE_ITEMS];
  uint8_t _channel_indexes[MAX_VISIBLE_ITEMS];
  uint8_t _channel_count;

  ContactInfo _compose_contact;
  ChannelDetails _compose_channel;
  uint8_t _compose_channel_index;
  char _compose[MAX_TEXT_LEN + 1];
  uint16_t _compose_len;

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
    const char* labels[TAB_COUNT] = {"Chats", "Channels", "Nodes", "Map"};
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

  void drawCardFrame(DisplayDriver& d, int y, bool selected = false) {
    d.setColor(COMPACT_PANEL);
    d.fillRoundRect(8, y, d.width() - 16, CARD_H, 7);
    d.setColor(selected ? COMPACT_TEAL : COMPACT_BORDER);
    d.drawRoundRect(8, y, d.width() - 16, CARD_H, 7);
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

    char preview[84];
    if (m.outgoing) snprintf(preview, sizeof(preview), "You: %s", m.text);
    else StrHelper::strncpy(preview, m.text, sizeof(preview));
    d.drawTextEllipsized(44, y + 17, 220, preview);

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

  void drawNodeCard(DisplayDriver& d, const AdvertPath& a, int y, ColorVal avatarColor, bool selected = false) {
    drawCardFrame(d, y, selected);
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
      snprintf(detail, sizeof(detail), "route unknown");
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

  void loadContacts() {
    _contact_count = 0;
    ContactInfo contact;
    ContactsIterator it = the_mesh.startContactsIterator();
    while (_contact_count < MAX_VISIBLE_ITEMS && it.hasNext(&the_mesh, contact)) {
      if (contact.type == ADV_TYPE_CHAT && contact.name[0]) {
        _contacts[_contact_count++] = contact;
      }
    }
    if (_contact_count == 0) _selected = 0;
    else if (_selected >= _contact_count) _selected = _contact_count - 1;
  }

  void loadChannels() {
    _channel_count = 0;
#ifdef MAX_GROUP_CHANNELS
    for (int i = 0; i < MAX_GROUP_CHANNELS && _channel_count < MAX_VISIBLE_ITEMS; ++i) {
      ChannelDetails channel;
      if (the_mesh.getChannel(i, channel) && channel.name[0]) {
        _channels[_channel_count] = channel;
        _channel_indexes[_channel_count] = (uint8_t)i;
        _channel_count++;
      }
    }
#endif
    if (_channel_count == 0) _selected = 0;
    else if (_selected >= _channel_count) _selected = _channel_count - 1;
  }

  void addCachedMessage(uint8_t path_len, const char* from, const char* text, bool outgoing) {
    _message_head = (_message_head + 1) % 4;
    if (_message_count < 4) _message_count++;

    MsgEntry& m = _messages[_message_head];
    memset(&m, 0, sizeof(m));
    m.timestamp = _rtc->getCurrentTime();
    m.path_len = path_len;
    m.unread = outgoing ? 0 : 1;
    m.outgoing = outgoing;
    StrHelper::strncpy(m.origin, from ? from : "Unknown", sizeof(m.origin));
    StrHelper::strncpy(m.text, text ? text : "", sizeof(m.text));
  }

  void drawChats(DisplayDriver& d) {
    char pill[20];
    snprintf(pill, sizeof(pill), "%d unread", _task->getMsgCount());
    drawHeader(d, "Chats", pill);
    drawTabs(d);

    const ColorVal colors[4] = {COMPACT_TEAL, COMPACT_GREEN, COMPACT_BLUEGRAY, COMPACT_AMBER};
    int y = CARD_Y;
    int shown = 0;

    for (int i = 0; i < _message_count && shown < 4; ++i) {
      int idx = (_message_head + 4 - i) % 4;
      drawMessageCard(d, _messages[idx], y, colors[shown]);
      y += CARD_STEP;
      shown++;
    }

    AdvertPath recent[4];
    memset(recent, 0, sizeof(recent));
    int recentCount = the_mesh.getRecentlyHeard(recent, 4);
    for (int i = 0; i < recentCount && shown < 4; ++i) {
      if (!recent[i].name[0]) continue;
      drawNodeCard(d, recent[i], y, colors[shown]);
      y += CARD_STEP;
      shown++;
    }

    if (shown == 0) {
      drawEmptyCard(d, y, "No conversations yet", "Waiting for MeshCore traffic...");
    }
  }

  void drawChannels(DisplayDriver& d) {
    loadChannels();
    char pill[20];
    snprintf(pill, sizeof(pill), "%u configured", _channel_count);
    drawHeader(d, "Channels", pill);
    drawTabs(d);

    const ColorVal colors[4] = {COMPACT_TEAL, COMPACT_GREEN, COMPACT_AMBER, COMPACT_BLUEGRAY};
    int y = CARD_Y;
    for (int i = 0; i < _channel_count; ++i) {
      drawCardFrame(d, y, i == _selected);
      drawAvatar(d, 25, y + 15, colors[i % 4], _channels[i].name);
      d.setTextSize(1);
      d.setColor(UIColor::primary_txt);
      d.drawTextEllipsized(44, y + 6, 220, _channels[i].name);
      d.setColor(UIColor::secondary_txt);
      d.setCursor(44, y + 18);
      d.print(i == _selected ? "Enter to compose" : "MeshCore group channel");
      y += CARD_STEP;
    }
    if (_channel_count == 0) {
      drawEmptyCard(d, y, "No configured channels", "Configure channels from companion app.");
    }
  }

  void drawNodes(DisplayDriver& d) {
    AdvertPath recent[4];
    memset(recent, 0, sizeof(recent));
    int count = the_mesh.getRecentlyHeard(recent, 4);
    if (count <= 0) _selected = 0;
    else if (_selected >= count) _selected = count - 1;

    char pill[20];
    snprintf(pill, sizeof(pill), "%d recent", count);
    drawHeader(d, "Nodes", pill);
    drawTabs(d);

    const ColorVal colors[4] = {COMPACT_GREEN, COMPACT_TEAL, COMPACT_BLUEGRAY, COMPACT_AMBER};
    int y = CARD_Y;
    int shown = 0;
    for (int i = 0; i < count && shown < 4; ++i) {
      if (!recent[i].name[0]) continue;
      drawNodeCard(d, recent[i], y, colors[shown], shown == _selected);
      y += CARD_STEP;
      shown++;
    }
    if (shown == 0) {
      drawEmptyCard(d, y, "No recent nodes", "Advert or wait for nearby traffic.");
    }
  }

  void drawMap(DisplayDriver& d) {
    drawHeader(d, "Map", "GPS");
    drawTabs(d);

    const int x = 10;
    const int y = 80;
    const int w = d.width() - 20;
    const int h = 112;
    d.setColor(COMPACT_PANEL);
    d.fillRoundRect(x, y, w, h, 8);
    d.setColor(COMPACT_BORDER);
    d.drawRoundRect(x, y, w, h, 8);

    d.setColor(COMPACT_BORDER);
    d.drawLine(x + w / 2, y + 12, x + w / 2, y + h - 12);
    d.drawLine(x + 12, y + h / 2, x + w - 12, y + h / 2);
    d.drawCircle(x + w / 2, y + h / 2, 28);
    d.drawCircle(x + w / 2, y + h / 2, 10);

    LocationProvider* loc = sensors.getLocationProvider();
    d.setTextSize(1);
    if (loc && loc->isValid()) {
      d.setColor(COMPACT_GREEN);
      d.fillCircle(x + w / 2, y + h / 2, 4);
      char pos[48];
      snprintf(pos, sizeof(pos), "%.4f  %.4f",
               loc->getLatitude() / 1000000.0,
               loc->getLongitude() / 1000000.0);
      d.setColor(UIColor::primary_txt);
      d.drawTextCentered(d.width() / 2, y + h - 17, pos);
    } else {
      d.setColor(UIColor::warning_txt);
      d.drawTextCentered(d.width() / 2, y + h / 2 - 4, "NO GPS FIX");
    }

    d.setColor(UIColor::secondary_txt);
    d.drawTextCentered(d.width() / 2, 197, "Offline map tiles come after core messaging");
  }

  void drawContacts(DisplayDriver& d) {
    loadContacts();
    char pill[20];
    snprintf(pill, sizeof(pill), "%u chat", _contact_count);
    drawHeader(d, "Contacts", pill);

    const ColorVal colors[4] = {COMPACT_TEAL, COMPACT_GREEN, COMPACT_BLUEGRAY, COMPACT_AMBER};
    int y = 55;
    for (int i = 0; i < _contact_count; ++i) {
      drawCardFrame(d, y, i == _selected);
      drawAvatar(d, 25, y + 15, colors[i % 4], _contacts[i].name);
      d.setTextSize(1);
      d.setColor(UIColor::primary_txt);
      d.drawTextEllipsized(44, y + 6, 220, _contacts[i].name);
      d.setColor(UIColor::secondary_txt);
      char route[40];
      if (_contacts[i].out_path_len == OUT_PATH_UNKNOWN) {
        snprintf(route, sizeof(route), "flood route");
      } else {
        snprintf(route, sizeof(route), "%u hop%s", _contacts[i].out_path_len,
                 _contacts[i].out_path_len == 1 ? "" : "s");
      }
      d.setCursor(44, y + 18);
      d.print(route);
      y += CARD_STEP;
    }

    if (_contact_count == 0) {
      drawEmptyCard(d, y, "No chat contacts", "Receive adverts or add contacts by phone.");
    }

    drawHintBar(d, "Trackball select   Enter compose   Esc back");
  }

  void drawRadio(DisplayDriver& d) {
    drawHeader(d, "Radio", "Enter: advert");

    NodePrefs* p = _task->getNodePrefs();
    const int x = 10;
    const int y = 55;
    const int w = d.width() - 20;
    const int h = 140;

    d.setColor(COMPACT_PANEL);
    d.fillRoundRect(x, y, w, h, 8);
    d.setColor(COMPACT_BORDER);
    d.drawRoundRect(x, y, w, h, 8);

    char buf[48];
    d.setTextSize(1);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 69); d.print("Frequency");
    d.setColor(UIColor::primary_txt);
    snprintf(buf, sizeof(buf), "%.3f MHz", p->freq);
    d.drawTextRightAlign(d.width() - 22, 69, buf);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 93); d.print("LoRa");
    d.setColor(UIColor::primary_txt);
    snprintf(buf, sizeof(buf), "SF%d  BW %.0f  CR%d", p->sf, p->bw, p->cr);
    d.drawTextRightAlign(d.width() - 22, 93, buf);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 117); d.print("TX power");
    d.setColor(UIColor::primary_txt);
    snprintf(buf, sizeof(buf), "%d dBm", p->tx_power_dbm);
    d.drawTextRightAlign(d.width() - 22, 117, buf);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 141); d.print("Noise floor");
    d.setColor(UIColor::primary_txt);
    snprintf(buf, sizeof(buf), "%d dBm", radio_driver.getNoiseFloor());
    d.drawTextRightAlign(d.width() - 22, 141, buf);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 165); d.print("BLE companion");
    d.setColor(_task->isBluetoothEnabled() ? COMPACT_GREEN : UIColor::warning_txt);
    d.drawTextRightAlign(d.width() - 22, 165,
                         _task->isBluetoothEnabled() ? "enabled" : "disabled");

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 181); d.print("Enter sends a self advert");
    drawHintBar(d, "Enter advert   Esc back");
  }

  void drawSettings(DisplayDriver& d) {
    drawHeader(d, "Settings", "Enter: BLE");

    const int x = 10;
    const int y = 55;
    const int w = d.width() - 20;
    const int h = 140;
    d.setColor(COMPACT_PANEL);
    d.fillRoundRect(x, y, w, h, 8);
    d.setColor(COMPACT_BORDER);
    d.drawRoundRect(x, y, w, h, 8);

    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 69); d.print("Bluetooth");
    d.setColor(_task->isBluetoothEnabled() ? COMPACT_GREEN : UIColor::warning_txt);
    d.drawTextRightAlign(d.width() - 22, 69,
                         _task->isBluetoothEnabled() ? "ON" : "OFF");

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 93); d.print("Battery");
    char batt[24];
    snprintf(batt, sizeof(batt), "%d%%  %u mV",
             batteryPercent(_task->getBattMilliVolts()),
             _task->getBattMilliVolts());
    d.setColor(UIColor::primary_txt);
    d.drawTextRightAlign(d.width() - 22, 93, batt);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 117); d.print("Firmware");
    d.setColor(UIColor::primary_txt);
    d.drawTextRightAlign(d.width() - 22, 117, FIRMWARE_VERSION);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 141); d.print("Input");
    d.setColor(UIColor::primary_txt);
    d.drawTextRightAlign(d.width() - 22, 141, "trackball + keyboard");

    d.setColor(UIColor::secondary_txt);
    d.setCursor(22, 165); d.print("Enter toggles Bluetooth");
    d.setCursor(22, 181); d.print("Long center at boot: CLI rescue");
    drawHintBar(d, "Enter BLE   Esc back");
  }

  void drawComposeLines(DisplayDriver& d, int x, int y, int maxCharsPerLine) {
    char line[52];
    int lineNo = 0;
    int pos = 0;
    while (pos < _compose_len && lineNo < 5) {
      int n = 0;
      while (pos < _compose_len && n < maxCharsPerLine) {
        line[n++] = _compose[pos++];
      }
      line[n] = 0;
      d.setCursor(x, y + lineNo * 15);
      d.print(line);
      lineNo++;
    }
    if (_compose_len == 0) {
      d.setColor(UIColor::secondary_txt);
      d.setCursor(x, y);
      d.print("Type a message...");
    }
  }

  void drawCompose(DisplayDriver& d) {
    const char* target = _view == COMPOSE_CONTACT ? _compose_contact.name : _compose_channel.name;
    char pill[42];
    snprintf(pill, sizeof(pill), "To: %s", target);
    drawHeader(d, "Compose", pill);

    d.setColor(COMPACT_PANEL);
    d.fillRoundRect(10, 55, d.width() - 20, 112, 8);
    d.setColor(COMPACT_TEAL);
    d.drawRoundRect(10, 55, d.width() - 20, 112, 8);

    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    drawComposeLines(d, 20, 68, 46);

    char count[24];
    snprintf(count, sizeof(count), "%u/%u", _compose_len, (unsigned)MAX_TEXT_LEN);
    d.setColor(UIColor::secondary_txt);
    d.drawTextRightAlign(d.width() - 18, 151, count);

    d.setColor(UIColor::secondary_txt);
    d.setCursor(18, 180);
    d.print("Physical keyboard input");
    drawHintBar(d, "Enter send   Esc cancel   Del erase");
  }

  void drawFooter(DisplayDriver& d) {
    const int y = 208;
    d.setColor(UIColor::title_bkg);
    d.fillRect(0, y, d.width(), d.height() - y);
    d.setColor(COMPACT_BORDER);
    d.drawLine(0, y, d.width() - 1, y);

    const char* labels[4] = {"Compose", "Contacts", "Radio", "Settings"};
    const int w = d.width() / 4;
    d.setTextSize(1);
    for (int i = 0; i < 4; ++i) {
      bool active = (_view == CONTACTS && i == 1) || (_view == RADIO && i == 2) ||
                    (_view == SETTINGS && i == 3) ||
                    ((_view == COMPOSE_CONTACT || _view == COMPOSE_CHANNEL) && i == 0);
      if (active) {
        d.setColor(COMPACT_TEAL);
        d.fillRoundRect(i * w + 5, y + 5, w - 10, 21, 6);
        d.setColor(UIColor::primary_txt);
      } else {
        d.setColor(UIColor::secondary_txt);
      }
      d.drawTextCentered(i * w + w / 2, y + 11, labels[i]);
    }
  }

  void drawHintBar(DisplayDriver& d, const char* hint) {
    d.setColor(UIColor::title_bkg);
    d.fillRect(0, 208, d.width(), d.height() - 208);
    d.setColor(COMPACT_BORDER);
    d.drawLine(0, 208, d.width() - 1, 208);
    d.setTextSize(1);
    d.setColor(UIColor::secondary_txt);
    d.drawTextCentered(d.width() / 2, 220, hint);
  }

  void beginComposeContact(const ContactInfo& contact) {
    _compose_contact = contact;
    _compose_len = 0;
    _compose[0] = 0;
    _view = COMPOSE_CONTACT;
  }

  void beginComposeChannel(const ChannelDetails& channel, uint8_t idx) {
    _compose_channel = channel;
    _compose_channel_index = idx;
    _compose_len = 0;
    _compose[0] = 0;
    _view = COMPOSE_CHANNEL;
  }

  void sendCompose() {
    if (_compose_len == 0) {
      _task->showAlert("Message is empty", 1000);
      return;
    }

    if (_view == COMPOSE_CONTACT) {
      uint32_t expected_ack = 0;
      uint32_t est_timeout = 0;
      int result = the_mesh.sendMessage(_compose_contact, _rtc->getCurrentTime(), 0,
                                        _compose, expected_ack, est_timeout);
      if (result == MSG_SEND_FAILED) {
        _task->showAlert("Message send failed", 1200);
        return;
      }
      addCachedMessage(0xFF, _compose_contact.name, _compose, true);
      _task->showAlert(result == MSG_SEND_SENT_DIRECT ? "Sent direct" : "Sent flood", 1200);
    } else if (_view == COMPOSE_CHANNEL) {
      bool ok = the_mesh.sendGroupMessage(_rtc->getCurrentTime(), _compose_channel.channel,
                                          _task->getNodePrefs()->node_name,
                                          _compose, _compose_len);
      if (!ok) {
        _task->showAlert("Channel send failed", 1200);
        return;
      }
      addCachedMessage(0xFF, _compose_channel.name, _compose, true);
      _task->showAlert("Channel message sent", 1200);
    }

    _compose_len = 0;
    _compose[0] = 0;
    _view = HOME;
    _tab = CHATS;
    _selected = 0;
  }

  bool handleComposeInput(char c) {
    if (c == KEY_CANCEL) {
      _compose_len = 0;
      _compose[0] = 0;
      _view = HOME;
      return true;
    }
    if (c == KEY_ENTER) {
      sendCompose();
      return true;
    }
    if (c == 8 || (uint8_t)c == 127) {
      if (_compose_len > 0) {
        _compose[--_compose_len] = 0;
      }
      return true;
    }
    if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && _compose_len < MAX_TEXT_LEN) {
      _compose[_compose_len++] = c;
      _compose[_compose_len] = 0;
      return true;
    }
    return false;
  }

public:
  CompactHomeScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _tab(CHATS), _view(HOME), _selected(0),
      _message_count(0), _message_head(3), _contact_count(0), _channel_count(0),
      _compose_channel_index(0), _compose_len(0) {
    memset(_messages, 0, sizeof(_messages));
    memset(_contacts, 0, sizeof(_contacts));
    memset(_channels, 0, sizeof(_channels));
    memset(&_compose_contact, 0, sizeof(_compose_contact));
    memset(&_compose_channel, 0, sizeof(_compose_channel));
    _compose[0] = 0;
  }

  void addMessage(uint8_t path_len, const char* from, const char* text) {
    addCachedMessage(path_len, from, text, false);
    _tab = CHATS;
    _view = HOME;
  }

  void clearUnread() {
    for (int i = 0; i < 4; ++i) _messages[i].unread = 0;
  }

  int render(DisplayDriver& d) override {
    drawStatus(d);

    if (_view == CONTACTS) {
      drawContacts(d);
      return 1000;
    }
    if (_view == RADIO) {
      drawRadio(d);
      return 1000;
    }
    if (_view == SETTINGS) {
      drawSettings(d);
      return 1000;
    }
    if (_view == COMPOSE_CONTACT || _view == COMPOSE_CHANNEL) {
      drawCompose(d);
      return 250;
    }

    switch (_tab) {
      case CHATS: drawChats(d); break;
      case CHANNELS: drawChannels(d); break;
      case NODES: drawNodes(d); break;
      case MAP: drawMap(d); break;
      default: _tab = CHATS; drawChats(d); break;
    }

    drawFooter(d);
    return 1000;
  }

  bool handleInput(char c) override {
    if (_view == COMPOSE_CONTACT || _view == COMPOSE_CHANNEL) {
      return handleComposeInput(c);
    }

    if (_view == CONTACTS) {
      loadContacts();
      if (c == KEY_CANCEL) {
        _view = HOME;
        _selected = 0;
        return true;
      }
      if (c == KEY_UP && _contact_count > 0) {
        _selected = (_selected + _contact_count - 1) % _contact_count;
        return true;
      }
      if (c == KEY_DOWN && _contact_count > 0) {
        _selected = (_selected + 1) % _contact_count;
        return true;
      }
      if (c == KEY_ENTER && _contact_count > 0) {
        beginComposeContact(_contacts[_selected]);
        return true;
      }
      return false;
    }

    if (_view == RADIO) {
      if (c == KEY_CANCEL) {
        _view = HOME;
        return true;
      }
      if (c == KEY_ENTER) {
        _task->notify(UIEventType::ack);
        if (the_mesh.advert()) _task->showAlert("Advert sent", 1000);
        else _task->showAlert("Advert failed", 1000);
        return true;
      }
      return false;
    }

    if (_view == SETTINGS) {
      if (c == KEY_CANCEL) {
        _view = HOME;
        return true;
      }
      if (c == KEY_ENTER) {
        if (_task->isBluetoothEnabled()) {
          _task->disableBluetooth();
          _task->showAlert("Bluetooth disabled", 1000);
        } else {
          _task->enableBluetooth();
          _task->showAlert("Bluetooth enabled", 1000);
        }
        return true;
      }
      return false;
    }

    if (c == KEY_LEFT) {
      _tab = (_tab + TAB_COUNT - 1) % TAB_COUNT;
      _selected = 0;
      return true;
    }
    if (c == KEY_RIGHT) {
      _tab = (_tab + 1) % TAB_COUNT;
      _selected = 0;
      return true;
    }
    if (c == KEY_UP) {
      if (_selected > 0) _selected--;
      return true;
    }
    if (c == KEY_DOWN) {
      _selected++;
      return true;
    }
    if (c >= '1' && c <= '4') {
      _tab = (Tab)(c - '1');
      _selected = 0;
      return true;
    }
    if (c == 'c' || c == 'C') {
      _view = CONTACTS;
      _selected = 0;
      return true;
    }
    if (c == 'o' || c == 'O') {
      _view = CONTACTS;
      _selected = 0;
      return true;
    }
    if (c == 'r' || c == 'R') {
      _view = RADIO;
      return true;
    }
    if (c == 's' || c == 'S') {
      _view = SETTINGS;
      return true;
    }
    if (c == KEY_CANCEL) {
      _tab = CHATS;
      _selected = 0;
      return true;
    }
    if (c == KEY_ENTER && _tab == CHANNELS) {
      loadChannels();
      if (_channel_count > 0) {
        beginComposeChannel(_channels[_selected], _channel_indexes[_selected]);
      }
      return true;
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

void UITask::begin(DisplayDriver* display, SensorManager* sensors_ptr, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors_ptr;
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
