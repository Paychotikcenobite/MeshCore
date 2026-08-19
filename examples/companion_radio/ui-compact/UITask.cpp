#include "UITask.h"
#include <Wire.h>
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

// MeshCore Communicator Android dark palette converted to RGB565.
static const ColorVal MCC_BG       = 0x0084; // 5,16,32
static const ColorVal MCC_CARD     = 0x08E6; // 12,29,50
static const ColorVal MCC_CARD2    = 0x1127; // 17,38,63
static const ColorVal MCC_TEXT     = 0xFFFF;
static const ColorVal MCC_SUB      = 0xA5D9; // 166,185,207
static const ColorVal MCC_ACCENT   = 0x11EF; // 18,63,122
static const ColorVal MCC_INCOMING = 0x1988; // 31,48,70
static const ColorVal MCC_OUTGOING = 0x1274; // 19,79,166
static const ColorVal MCC_DIVIDER  = 0x21C9; // 39,57,78
static const ColorVal MCC_DANGER   = 0xDA28;
static const ColorVal MCC_GREEN    = 0x25CB;
static const ColorVal MCC_AMBER    = 0xF565;
static const ColorVal MCC_MUTED    = 0x8472;
static const ColorVal MCC_INPUT    = 0x19CB;
static const ColorVal MCC_STROKE   = 0x32CF;

static const uint8_t TOUCH_TAP = 0;
static const uint8_t TOUCH_SWIPE_UP = 1;
static const uint8_t TOUCH_SWIPE_DOWN = 2;
static const uint8_t TOUCH_SWIPE_LEFT = 3;
static const uint8_t TOUCH_SWIPE_RIGHT = 4;

static int batteryPercent(uint16_t mv) {
  int pct = ((int)mv - 3300) * 100 / 900;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

static void initialsFor(const char* name, char out[3]) {
  out[0] = 'M'; out[1] = 'C'; out[2] = 0;
  if (!name || !name[0]) return;
  out[0] = name[0]; out[1] = 0;
  const char* p = name;
  while (*p && *p != ' ') p++;
  if (*p == ' ' && p[1]) { out[1] = p[1]; out[2] = 0; }
  else if (name[1]) { out[1] = name[1]; out[2] = 0; }
}

static void ageLabel(mesh::RTCClock* rtc, uint32_t ts, char* out, size_t len) {
  if (!ts) { StrHelper::strncpy(out, "", len); return; }
  int32_t age = (int32_t)(rtc->getCurrentTime() - ts);
  if (age < 0) age = 0;
  if (age < 60) snprintf(out, len, "now");
  else if (age < 3600) snprintf(out, len, "%ldm", (long)(age / 60));
  else if (age < 86400) snprintf(out, len, "%ldh", (long)(age / 3600));
  else snprintf(out, len, "%ldd", (long)(age / 86400));
}

class CommunicatorScreen : public UIScreen {
public:
  enum View : uint8_t { HOME = 0, CHAT, NEW_CHAT, SETTINGS, RADIO, INFO, REPEATER_INFO };
  enum MainTab : uint8_t { CHATS = 0, REPEATERS = 1 };
  enum RowKind : uint8_t { ROW_NONE = 0, ROW_CONTACT, ROW_CHANNEL, ROW_REPEATER, ROW_UNKNOWN };
  enum Dirty : uint8_t { DIRTY_NONE = 0, DIRTY_COMPOSER, DIRTY_ALL };

  struct MsgEntry {
    uint32_t timestamp;
    uint8_t path_len;
    uint8_t unread;
    bool outgoing;
    char origin[32];
    char text[96];
  };

  struct Row {
    RowKind kind;
    char name[32];
    char preview[72];
    uint32_t timestamp;
    uint8_t unread;
    ContactInfo contact;
    ChannelDetails channel;
    uint8_t channel_index;
  };

private:
  static const int MSG_CACHE = 16;
  static const int MAX_LOCAL_CONTACTS = 16;
  static const int MAX_LOCAL_CHANNELS = 12;
  static const int MAX_ROWS = 24;
  static const int VISIBLE_ROWS = 3;

  UITask* _task;
  mesh::RTCClock* _rtc;
  View _view;
  MainTab _tab;
  Dirty _dirty;
  int _selected;
  int _list_offset;
  bool _search_active;
  char _search[32];
  uint8_t _search_len;

  MsgEntry _messages[MSG_CACHE];
  uint8_t _message_count;
  uint8_t _message_head;

  ContactInfo _contacts[MAX_LOCAL_CONTACTS];
  uint8_t _contact_count;
  ContactInfo _repeaters[MAX_LOCAL_CONTACTS];
  uint8_t _repeater_count;
  ChannelDetails _channels[MAX_LOCAL_CHANNELS];
  uint8_t _channel_indexes[MAX_LOCAL_CHANNELS];
  uint8_t _channel_count;

  Row _rows[MAX_ROWS];
  uint8_t _row_count;

  RowKind _active_kind;
  ContactInfo _active_contact;
  ChannelDetails _active_channel;
  uint8_t _active_channel_index;
  char _active_name[32];
  char _compose[MAX_TEXT_LEN + 1];
  uint16_t _compose_len;
  ContactInfo _info_contact;

  void fillScreen(DisplayDriver& d) {
    d.setColor(MCC_BG);
    d.fillRect(0, 0, d.width(), d.height());
  }

  void drawLogo(DisplayDriver& d) {
    d.setColor(MCC_ACCENT); d.fillCircle(21, 22, 17);
    d.setTextSize(1); d.setColor(MCC_TEXT);
    d.drawTextCentered(21, 18, "MC");
  }

  void drawRadioGlyph(DisplayDriver& d, int cx, int cy) {
    d.setColor(MCC_GREEN); d.fillCircle(cx, cy + 7, 2);
    d.drawLine(cx, cy + 4, cx, cy - 4);
    d.drawLine(cx - 4, cy + 2, cx - 7, cy - 1);
    d.drawLine(cx + 4, cy + 2, cx + 7, cy - 1);
    d.drawLine(cx - 7, cy - 3, cx - 10, cy - 7);
    d.drawLine(cx + 7, cy - 3, cx + 10, cy - 7);
  }

  void drawGear(DisplayDriver& d, int cx, int cy) {
    d.setColor(MCC_TEXT); d.drawCircle(cx, cy, 8); d.drawCircle(cx, cy, 2);
    d.drawLine(cx, cy - 12, cx, cy - 8); d.drawLine(cx, cy + 8, cx, cy + 12);
    d.drawLine(cx - 12, cy, cx - 8, cy); d.drawLine(cx + 8, cy, cx + 12, cy);
    d.drawLine(cx - 9, cy - 9, cx - 6, cy - 6); d.drawLine(cx + 6, cy + 6, cx + 9, cy + 9);
    d.drawLine(cx + 9, cy - 9, cx + 6, cy - 6); d.drawLine(cx - 6, cy + 6, cx - 9, cy + 9);
  }

  void drawAppHeader(DisplayDriver& d) {
    d.setColor(MCC_BG); d.fillRect(0, 0, d.width(), 44);
    drawLogo(d);
    d.setTextSize(2); d.setColor(MCC_TEXT);
    d.setCursor(48, 3); d.print("MeshCore");
    d.setCursor(48, 22); d.print("Communicator");

    d.setColor(MCC_CARD); d.fillRoundRect(232, 4, 40, 36, 7);
    d.setColor(MCC_DIVIDER); d.drawRoundRect(232, 4, 40, 36, 7);
    drawRadioGlyph(d, 252, 22);

    d.setColor(MCC_CARD); d.fillRoundRect(276, 4, 40, 36, 7);
    d.setColor(MCC_DIVIDER); d.drawRoundRect(276, 4, 40, 36, 7);
    drawGear(d, 296, 22);
  }

  void drawBack(DisplayDriver& d, int y = 49) {
    d.setColor(MCC_CARD2); d.fillRoundRect(6, y, 32, 28, 6);
    d.setColor(MCC_TEXT); d.drawLine(26, y + 7, 16, y + 14); d.drawLine(16, y + 14, 26, y + 21);
  }

  void drawAvatar(DisplayDriver& d, int cx, int cy, const char* name, bool channel = false) {
    d.setColor(channel ? MCC_MUTED : MCC_ACCENT); d.fillCircle(cx, cy, 14);
    char inits[3]; initialsFor(name, inits);
    d.setTextSize(1); d.setColor(MCC_TEXT);
    d.drawTextCentered(cx, cy - 4, inits);
  }

  void drawTabs(DisplayDriver& d) {
    const int y = 47, h = 26;
    int repeatCount = countRepeaters();
    char rep[28]; snprintf(rep, sizeof(rep), "Repeaters (%d)", repeatCount);
    const char* labels[2] = {"Chats", rep};
    for (int i = 0; i < 2; ++i) {
      int x = i == 0 ? 8 : 164;
      int w = 148;
      bool on = _tab == i;
      d.setColor(on ? MCC_CARD2 : MCC_BG); d.fillRoundRect(x, y, w, h, 6);
      d.setTextSize(1); d.setColor(on ? MCC_TEXT : MCC_SUB);
      d.drawTextCentered(x + w / 2, y + 9, labels[i]);
    }
  }

  void loadContactsAndChannels() {
    _contact_count = _repeater_count = 0;
    ContactInfo c;
    ContactsIterator it = the_mesh.startContactsIterator();
    while (it.hasNext(&the_mesh, c)) {
      if (c.type == ADV_TYPE_CHAT && c.name[0] && _contact_count < MAX_LOCAL_CONTACTS)
        _contacts[_contact_count++] = c;
      else if (c.type == ADV_TYPE_REPEATER && c.name[0] && _repeater_count < MAX_LOCAL_CONTACTS)
        _repeaters[_repeater_count++] = c;
    }
    _channel_count = 0;
#ifdef MAX_GROUP_CHANNELS
    for (int i = 0; i < MAX_GROUP_CHANNELS && _channel_count < MAX_LOCAL_CHANNELS; ++i) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0]) {
        _channels[_channel_count] = ch;
        _channel_indexes[_channel_count] = (uint8_t)i;
        _channel_count++;
      }
    }
#endif
  }

  int countRepeaters() {
    loadContactsAndChannels();
    return _repeater_count;
  }

  int findContactByName(const char* name) {
    for (int i = 0; i < _contact_count; ++i) if (strcmp(_contacts[i].name, name) == 0) return i;
    return -1;
  }

  int findChannelByName(const char* name) {
    for (int i = 0; i < _channel_count; ++i) if (strcmp(_channels[i].name, name) == 0) return i;
    return -1;
  }

  bool containsInsensitive(const char* hay, const char* needle) {
    if (!needle || !needle[0]) return true;
    if (!hay) return false;
    for (const char* h = hay; *h; ++h) {
      const char* a = h; const char* b = needle;
      while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) break;
        ++a; ++b;
      }
      if (!*b) return true;
    }
    return false;
  }

  bool rowNameExists(const char* name) {
    for (int i = 0; i < _row_count; ++i) if (strcmp(_rows[i].name, name) == 0) return true;
    return false;
  }

  void pushRow(const Row& r) {
    if (_row_count < MAX_ROWS) _rows[_row_count++] = r;
  }

  void buildChatRows() {
    loadContactsAndChannels();
    _row_count = 0;
    for (int n = 0; n < _message_count && _row_count < MAX_ROWS; ++n) {
      int idx = (_message_head + MSG_CACHE - n) % MSG_CACHE;
      const MsgEntry& m = _messages[idx];
      if (!m.origin[0] || rowNameExists(m.origin)) continue;
      if (!containsInsensitive(m.origin, _search) && !containsInsensitive(m.text, _search)) continue;
      Row r; memset(&r, 0, sizeof(r));
      StrHelper::strncpy(r.name, m.origin, sizeof(r.name));
      StrHelper::strncpy(r.preview, m.text, sizeof(r.preview));
      r.timestamp = m.timestamp;
      int ch = findChannelByName(m.origin);
      int ct = findContactByName(m.origin);
      if (ch >= 0) { r.kind = ROW_CHANNEL; r.channel = _channels[ch]; r.channel_index = _channel_indexes[ch]; }
      else if (ct >= 0) { r.kind = ROW_CONTACT; r.contact = _contacts[ct]; }
      else r.kind = ROW_UNKNOWN;
      for (int j = 0; j < MSG_CACHE; ++j) if (_messages[j].unread && strcmp(_messages[j].origin, m.origin) == 0) r.unread += _messages[j].unread;
      pushRow(r);
    }
    for (int i = 0; i < _channel_count && _row_count < MAX_ROWS; ++i) {
      if (rowNameExists(_channels[i].name) || !containsInsensitive(_channels[i].name, _search)) continue;
      Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_CHANNEL; r.channel = _channels[i]; r.channel_index = _channel_indexes[i];
      StrHelper::strncpy(r.name, _channels[i].name, sizeof(r.name)); StrHelper::strncpy(r.preview, "Group chat", sizeof(r.preview)); pushRow(r);
    }
    for (int i = 0; i < _contact_count && _row_count < MAX_ROWS; ++i) {
      if (rowNameExists(_contacts[i].name) || !containsInsensitive(_contacts[i].name, _search)) continue;
      Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_CONTACT; r.contact = _contacts[i];
      StrHelper::strncpy(r.name, _contacts[i].name, sizeof(r.name)); StrHelper::strncpy(r.preview, "Tap to start messaging", sizeof(r.preview)); pushRow(r);
    }
    clampOffset();
  }

  void buildRepeaterRows() {
    loadContactsAndChannels(); _row_count = 0;
    for (int i = 0; i < _repeater_count && _row_count < MAX_ROWS; ++i) {
      Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_REPEATER; r.contact = _repeaters[i];
      StrHelper::strncpy(r.name, _repeaters[i].name, sizeof(r.name));
      if (_repeaters[i].out_path_len == OUT_PATH_UNKNOWN) StrHelper::strncpy(r.preview, "Route: flood / unknown", sizeof(r.preview));
      else snprintf(r.preview, sizeof(r.preview), "Route: %u hop%s", _repeaters[i].out_path_len, _repeaters[i].out_path_len == 1 ? "" : "s");
      r.timestamp = _repeaters[i].last_advert_timestamp; pushRow(r);
    }
    clampOffset();
  }

  void buildNewRows() {
    loadContactsAndChannels(); _row_count = 0;
    for (int i = 0; i < _contact_count && _row_count < MAX_ROWS; ++i) {
      Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_CONTACT; r.contact = _contacts[i];
      StrHelper::strncpy(r.name, _contacts[i].name, sizeof(r.name)); StrHelper::strncpy(r.preview, "Direct message", sizeof(r.preview)); pushRow(r);
    }
    for (int i = 0; i < _channel_count && _row_count < MAX_ROWS; ++i) {
      Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_CHANNEL; r.channel = _channels[i]; r.channel_index = _channel_indexes[i];
      StrHelper::strncpy(r.name, _channels[i].name, sizeof(r.name)); StrHelper::strncpy(r.preview, "Group chat", sizeof(r.preview)); pushRow(r);
    }
    clampOffset(4);
  }

  void clampOffset(int visible = VISIBLE_ROWS) {
    int maxOffset = _row_count > visible ? _row_count - visible : 0;
    if (_list_offset > maxOffset) _list_offset = maxOffset;
    if (_list_offset < 0) _list_offset = 0;
  }

  void drawSearch(DisplayDriver& d) {
    d.setColor(MCC_INPUT); d.fillRoundRect(8, 77, 304, 27, 6);
    d.setColor(_search_active ? MCC_ACCENT : MCC_STROKE); d.drawRoundRect(8, 77, 304, 27, 6);
    d.setTextSize(1); d.setColor(_search[0] ? MCC_TEXT : MCC_SUB);
    d.setCursor(20, 87); d.print(_search[0] ? _search : "Search conversations");
    d.setColor(MCC_SUB); d.drawCircle(294, 89, 5); d.drawLine(298, 93, 303, 98);
  }

  void drawConversationRow(DisplayDriver& d, const Row& r, int y, bool selected = false) {
    d.setColor(MCC_CARD); d.fillRoundRect(8, y, 304, 29, 6);
    if (selected) { d.setColor(MCC_STROKE); d.drawRoundRect(8, y, 304, 29, 6); }
    drawAvatar(d, 25, y + 14, r.name, r.kind == ROW_CHANNEL);
    d.setTextSize(1); d.setColor(MCC_TEXT); d.drawTextEllipsized(45, y + 5, 170, r.name);
    d.setColor(MCC_SUB); d.drawTextEllipsized(45, y + 17, 205, r.preview);
    char age[12]; ageLabel(_rtc, r.timestamp, age, sizeof(age));
    d.setColor(MCC_SUB); d.drawTextRightAlign(301, y + 5, age);
    if (r.unread) {
      d.setColor(MCC_ACCENT); d.fillCircle(291, y + 19, 7);
      char u[5]; snprintf(u, sizeof(u), "%u", r.unread);
      d.setColor(MCC_TEXT); d.drawTextCentered(291, y + 16, u);
    }
  }

  void drawHome(DisplayDriver& d) {
    drawAppHeader(d); drawTabs(d);
    if (_tab == CHATS) {
      drawSearch(d); buildChatRows();
      if (!_row_count) {
        d.setTextSize(1); d.setColor(MCC_SUB); d.drawTextCentered(160, 145, "No conversations yet");
      } else {
        for (int i = 0; i < VISIBLE_ROWS; ++i) {
          int idx = _list_offset + i; if (idx >= _row_count) break;
          drawConversationRow(d, _rows[idx], 108 + i * 32, idx == _selected);
        }
      }
      d.setColor(MCC_ACCENT); d.fillRoundRect(8, 207, 304, 29, 6);
      d.setTextSize(1); d.setColor(MCC_TEXT); d.drawTextCentered(160, 218, "+  New conversation");
    } else {
      buildRepeaterRows();
      d.setColor(MCC_CARD2); d.fillRoundRect(8, 78, 304, 26, 6);
      d.setTextSize(1); d.setColor(MCC_TEXT); d.setCursor(18, 87); d.print("Repeaters");
      d.setColor(MCC_SUB); d.drawTextRightAlign(302, 87, "Recent / stored routes");
      if (!_row_count) {
        d.setColor(MCC_SUB); d.drawTextCentered(160, 145, "No repeaters stored or heard");
      } else {
        for (int i = 0; i < VISIBLE_ROWS; ++i) {
          int idx = _list_offset + i; if (idx >= _row_count) break;
          drawConversationRow(d, _rows[idx], 108 + i * 32, idx == _selected);
        }
      }
      d.setColor(MCC_CARD2); d.fillRoundRect(8, 207, 149, 29, 6);
      d.setColor(MCC_TEXT); d.drawTextCentered(82, 218, "Map coverage");
      d.setColor(MCC_CARD2); d.fillRoundRect(163, 207, 149, 29, 6);
      d.setColor(MCC_TEXT); d.drawTextCentered(237, 218, "Network health");
    }
  }

  void drawDetailTitle(DisplayDriver& d, const char* title, const char* subtitle = nullptr) {
    drawAppHeader(d); drawBack(d);
    d.setTextSize(2); d.setColor(MCC_TEXT); d.setCursor(48, 49); d.print(title);
    if (subtitle && subtitle[0]) { d.setTextSize(1); d.setColor(MCC_SUB); d.drawTextRightAlign(312, 61, subtitle); }
  }

  void drawNewChat(DisplayDriver& d) {
    drawDetailTitle(d, "New conversation"); buildNewRows();
    if (!_row_count) { d.setTextSize(1); d.setColor(MCC_SUB); d.drawTextCentered(160, 135, "No contacts or channels available"); return; }
    for (int i = 0; i < 4; ++i) {
      int idx = _list_offset + i; if (idx >= _row_count) break;
      drawConversationRow(d, _rows[idx], 82 + i * 36, idx == _selected);
    }
  }

  void drawChatHeader(DisplayDriver& d) {
    drawAppHeader(d); drawBack(d, 49);
    drawAvatar(d, 55, 63, _active_name, _active_kind == ROW_CHANNEL);
    d.setTextSize(1); d.setColor(MCC_TEXT); d.drawTextEllipsized(75, 53, 145, _active_name);
    d.setColor(MCC_SUB);
    if (_active_kind == ROW_CONTACT) {
      if (_active_contact.out_path_len == OUT_PATH_UNKNOWN) d.setCursor(75, 67), d.print("Route: flood / unknown");
      else { char p[28]; snprintf(p, sizeof(p), "Route: %u hop%s", _active_contact.out_path_len, _active_contact.out_path_len == 1 ? "" : "s"); d.setCursor(75, 67); d.print(p); }
    } else { d.setCursor(75, 67); d.print(_active_kind == ROW_CHANNEL ? "Group chat" : "Conversation"); }
    d.setColor(MCC_CARD2); d.fillRoundRect(231, 49, 38, 28, 6); d.setColor(MCC_TEXT); d.drawCircle(250, 63, 8); d.drawTextCentered(250, 59, "i");
    d.setColor(MCC_CARD2); d.fillRoundRect(274, 49, 38, 28, 6); d.setColor(MCC_TEXT); d.drawRect(284, 56, 18, 13); d.drawLine(290, 56, 290, 69); d.drawLine(296, 57, 296, 70);
  }

  int collectActiveMessages(int indexes[], int maxn) {
    int count = 0;
    for (int n = 0; n < _message_count && count < maxn; ++n) {
      int idx = (_message_head + MSG_CACHE - n) % MSG_CACHE;
      if (strcmp(_messages[idx].origin, _active_name) == 0) indexes[count++] = idx;
    }
    for (int i = 0; i < count / 2; ++i) { int t = indexes[i]; indexes[i] = indexes[count - 1 - i]; indexes[count - 1 - i] = t; }
    return count;
  }

  void drawBubble(DisplayDriver& d, const MsgEntry& m, int y) {
    char text[96]; StrHelper::strncpy(text, m.text, sizeof(text));
    int textW = d.getTextWidth(text); if (textW > 210) textW = 210;
    int w = textW + 18; if (w < 58) w = 58; if (w > 228) w = 228;
    int x = m.outgoing ? 312 - w : 8;
    d.setColor(m.outgoing ? MCC_OUTGOING : MCC_INCOMING); d.fillRoundRect(x, y, w, 29, 6);
    d.setTextSize(1); d.setColor(MCC_TEXT); d.drawTextEllipsized(x + 9, y + 7, w - 18, text);
    char age[12]; ageLabel(_rtc, m.timestamp, age, sizeof(age)); d.setColor(MCC_SUB); d.drawTextRightAlign(x + w - 7, y + 19, age);
  }

  void drawComposer(DisplayDriver& d) {
    d.setColor(MCC_BG); d.fillRect(0, 195, 320, 45);
    d.setColor(MCC_INPUT); d.fillRoundRect(8, 201, 198, 34, 6);
    d.setColor(MCC_STROKE); d.drawRoundRect(8, 201, 198, 34, 6);
    d.setTextSize(1); d.setColor(_compose_len ? MCC_TEXT : MCC_SUB);
    d.drawTextEllipsized(18, 214, 178, _compose_len ? _compose : "Message");
    d.setColor(MCC_CARD2); d.fillRoundRect(211, 201, 50, 34, 6); d.setColor(MCC_SUB); d.drawTextCentered(236, 214, "Voice");
    d.setColor(MCC_ACCENT); d.fillRoundRect(266, 201, 46, 34, 6);
    d.setColor(MCC_TEXT); d.drawLine(279, 211, 300, 218); d.drawLine(300, 218, 279, 225); d.drawLine(300, 218, 284, 218);
  }

  void drawChat(DisplayDriver& d) {
    drawChatHeader(d);
    d.setColor(MCC_BG); d.fillRect(0, 80, 320, 115);
    int idx[3]; int count = collectActiveMessages(idx, 3);
    if (!count) { d.setTextSize(1); d.setColor(MCC_SUB); d.drawTextCentered(160, 132, "No messages yet"); }
    else for (int i = 0; i < count; ++i) drawBubble(d, _messages[idx[i]], 87 + i * 34);
    drawComposer(d);
  }

  void drawSettings(DisplayDriver& d) {
    drawDetailTitle(d, "Settings");
    const int x = 10, w = 300;
    d.setColor(MCC_CARD); d.fillRoundRect(x, 84, w, 36, 6);
    d.setTextSize(1); d.setColor(MCC_TEXT); d.setCursor(22, 94); d.print("Bluetooth");
    d.setColor(MCC_SUB); d.setCursor(22, 107); d.print("Companion connection");
    d.setColor(_task->isBluetoothEnabled() ? MCC_GREEN : MCC_MUTED); d.fillRoundRect(259, 91, 38, 20, 10);
    d.setColor(MCC_TEXT); d.drawTextCentered(278, 98, _task->isBluetoothEnabled() ? "ON" : "OFF");

    d.setColor(MCC_CARD); d.fillRoundRect(x, 126, w, 36, 6); d.setColor(MCC_TEXT); d.setCursor(22, 136); d.print("Battery");
    char b[30]; snprintf(b, sizeof(b), "%d%%  %u mV", batteryPercent(_task->getBattMilliVolts()), _task->getBattMilliVolts()); d.setColor(MCC_SUB); d.drawTextRightAlign(298, 144, b);

    d.setColor(MCC_CARD); d.fillRoundRect(x, 168, w, 36, 6); d.setColor(MCC_TEXT); d.setCursor(22, 178); d.print("Firmware"); d.setColor(MCC_SUB); d.drawTextRightAlign(298, 186, FIRMWARE_VERSION);

    d.setColor(MCC_CARD); d.fillRoundRect(x, 210, w, 26, 6); d.setColor(MCC_SUB); d.setCursor(22, 219); d.print("Input"); d.setColor(MCC_TEXT); d.drawTextRightAlign(298, 219, _task->touchReady() ? "Touch + keyboard" : "Keyboard + trackball");
  }

  void drawRadio(DisplayDriver& d) {
    drawDetailTitle(d, "Radio status");
    NodePrefs* p = _task->getNodePrefs(); char buf[50];
    d.setColor(MCC_CARD); d.fillRoundRect(10, 84, 300, 47, 6);
    d.setTextSize(1); d.setColor(MCC_SUB); d.setCursor(22, 94); d.print("Connection & device");
    d.setColor(MCC_TEXT); d.setCursor(22, 109); d.print("Local SX1262 radio"); d.setColor(MCC_GREEN); d.drawTextRightAlign(298, 109, "Ready");
    d.setColor(MCC_SUB); d.setCursor(22, 121); d.print(_task->isBluetoothEnabled() ? "Bluetooth enabled" : "Bluetooth disabled");

    d.setColor(MCC_CARD); d.fillRoundRect(10, 137, 300, 50, 6);
    d.setColor(MCC_SUB); d.setCursor(22, 147); d.print("LoRa radio");
    snprintf(buf, sizeof(buf), "%.3f MHz", p->freq); d.setColor(MCC_TEXT); d.setCursor(22, 161); d.print(buf);
    snprintf(buf, sizeof(buf), "SF%u  BW %.1f  CR%u  TX %d dBm", p->sf, p->bw, p->cr, p->tx_power_dbm); d.setColor(MCC_SUB); d.setCursor(22, 175); d.print(buf);

    d.setColor(MCC_ACCENT); d.fillRoundRect(10, 195, 300, 36, 6); d.setColor(MCC_TEXT); d.drawTextCentered(160, 208, "Send self advert");
  }

  void drawInfo(DisplayDriver& d) {
    drawDetailTitle(d, "Conversation details");
    d.setColor(MCC_CARD); d.fillRoundRect(10, 84, 300, 48, 6);
    d.setTextSize(1); d.setColor(MCC_SUB); d.setCursor(22, 94); d.print("Conversation");
    d.setColor(MCC_TEXT); d.setCursor(22, 109); d.print(_active_name);
    d.setColor(MCC_SUB); d.setCursor(22, 121); d.print(_active_kind == ROW_CHANNEL ? "Group channel" : "Direct contact");
    if (_active_kind == ROW_CONTACT) {
      d.setColor(MCC_CARD); d.fillRoundRect(10, 139, 300, 48, 6); d.setColor(MCC_SUB); d.setCursor(22, 149); d.print("Radio status");
      d.setColor(MCC_TEXT); if (_active_contact.out_path_len == OUT_PATH_UNKNOWN) d.setCursor(22, 165), d.print("Route: flood / no stored path");
      else { char p[40]; snprintf(p, sizeof(p), "Route: %u hop%s", _active_contact.out_path_len, _active_contact.out_path_len == 1 ? "" : "s"); d.setCursor(22, 165); d.print(p); }
      char a[20]; ageLabel(_rtc, _active_contact.last_advert_timestamp, a, sizeof(a)); d.setColor(MCC_SUB); d.drawTextRightAlign(298, 176, a);
      d.setColor(MCC_CARD); d.fillRoundRect(10, 194, 300, 37, 6); d.setColor(MCC_SUB); d.setCursor(22, 205); d.print("Location");
      if (_active_contact.gps_lat || _active_contact.gps_lon) { char loc[52]; snprintf(loc, sizeof(loc), "%.4f, %.4f", _active_contact.gps_lat / 1000000.0, _active_contact.gps_lon / 1000000.0); d.setColor(MCC_TEXT); d.drawTextRightAlign(298, 217, loc); }
      else { d.setColor(MCC_SUB); d.drawTextRightAlign(298, 217, "No advertised location"); }
    }
  }

  void drawRepeaterInfo(DisplayDriver& d) {
    drawDetailTitle(d, "Repeater");
    d.setColor(MCC_CARD); d.fillRoundRect(10, 84, 300, 54, 6); d.setTextSize(1); d.setColor(MCC_TEXT); d.setCursor(22, 96); d.print(_info_contact.name);
    d.setColor(MCC_SUB); char age[20]; ageLabel(_rtc, _info_contact.last_advert_timestamp, age, sizeof(age)); d.setCursor(22, 113); d.print("Last heard"); d.drawTextRightAlign(298, 113, age);
    if (_info_contact.out_path_len == OUT_PATH_UNKNOWN) d.setCursor(22, 126), d.print("Route: flood / unknown");
    else { char p[32]; snprintf(p, sizeof(p), "Route: %u hop%s", _info_contact.out_path_len, _info_contact.out_path_len == 1 ? "" : "s"); d.setCursor(22, 126); d.print(p); }
    d.setColor(MCC_CARD); d.fillRoundRect(10, 146, 300, 52, 6); d.setColor(MCC_SUB); d.setCursor(22, 158); d.print("Advertised location");
    if (_info_contact.gps_lat || _info_contact.gps_lon) { char loc[52]; snprintf(loc, sizeof(loc), "%.4f, %.4f", _info_contact.gps_lat / 1000000.0, _info_contact.gps_lon / 1000000.0); d.setColor(MCC_TEXT); d.setCursor(22, 176); d.print(loc); }
    else { d.setCursor(22, 176); d.print("Unavailable"); }
  }

  void clearUnreadFor(const char* name) {
    for (int i = 0; i < MSG_CACHE; ++i) if (strcmp(_messages[i].origin, name) == 0) _messages[i].unread = 0;
  }

  void openRow(const Row& r) {
    _active_kind = r.kind; StrHelper::strncpy(_active_name, r.name, sizeof(_active_name));
    if (r.kind == ROW_CONTACT) _active_contact = r.contact;
    if (r.kind == ROW_CHANNEL) { _active_channel = r.channel; _active_channel_index = r.channel_index; }
    _compose_len = 0; _compose[0] = 0; clearUnreadFor(r.name); _view = CHAT; _list_offset = 0; _selected = 0; _dirty = DIRTY_ALL;
  }

  bool sendCompose() {
    if (!_compose_len) { _task->showAlert("Message is empty", 900); return false; }
    if (_active_kind == ROW_CONTACT) {
      uint32_t expected_ack = 0, est_timeout = 0;
      int result = the_mesh.sendMessage(_active_contact, _rtc->getCurrentTime(), 0, _compose, expected_ack, est_timeout);
      if (result == MSG_SEND_FAILED) { _task->showAlert("Message send failed", 1200); return false; }
      addCachedMessage(_active_name, _compose, true, 0xFF);
      _task->showAlert(result == MSG_SEND_SENT_DIRECT ? "Sent direct" : "Sent flood", 900);
    } else if (_active_kind == ROW_CHANNEL) {
      bool ok = the_mesh.sendGroupMessage(_rtc->getCurrentTime(), _active_channel.channel, _task->getNodePrefs()->node_name, _compose, _compose_len);
      if (!ok) { _task->showAlert("Channel send failed", 1200); return false; }
      addCachedMessage(_active_name, _compose, true, 0xFF); _task->showAlert("Channel message sent", 900);
    } else { _task->showAlert("Conversation is read-only", 1000); return false; }
    _compose_len = 0; _compose[0] = 0; _dirty = DIRTY_ALL; return true;
  }

  void addCachedMessage(const char* origin, const char* text, bool outgoing, uint8_t path_len) {
    _message_head = (_message_head + 1) % MSG_CACHE;
    if (_message_count < MSG_CACHE) _message_count++;
    MsgEntry& m = _messages[_message_head]; memset(&m, 0, sizeof(m));
    m.timestamp = _rtc->getCurrentTime(); m.path_len = path_len; m.outgoing = outgoing; m.unread = outgoing ? 0 : 1;
    StrHelper::strncpy(m.origin, origin ? origin : "Unknown", sizeof(m.origin)); StrHelper::strncpy(m.text, text ? text : "", sizeof(m.text));
  }

  void setView(View v) { _view = v; _list_offset = 0; _selected = 0; _search_active = false; _dirty = DIRTY_ALL; }

public:
  CommunicatorScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _view(HOME), _tab(CHATS), _dirty(DIRTY_ALL), _selected(0), _list_offset(0),
      _search_active(false), _search_len(0), _message_count(0), _message_head(MSG_CACHE - 1), _contact_count(0),
      _repeater_count(0), _channel_count(0), _row_count(0), _active_kind(ROW_NONE), _active_channel_index(0), _compose_len(0) {
    memset(_messages, 0, sizeof(_messages)); memset(_contacts, 0, sizeof(_contacts)); memset(_repeaters, 0, sizeof(_repeaters));
    memset(_channels, 0, sizeof(_channels)); memset(_rows, 0, sizeof(_rows)); memset(&_active_contact, 0, sizeof(_active_contact));
    memset(&_active_channel, 0, sizeof(_active_channel)); memset(&_info_contact, 0, sizeof(_info_contact));
    _search[0] = _active_name[0] = _compose[0] = 0;
  }

  void markAllDirty() { _dirty = DIRTY_ALL; }
  void clearUnread() { for (int i = 0; i < MSG_CACHE; ++i) _messages[i].unread = 0; _dirty = DIRTY_ALL; }
  void addMessage(uint8_t path_len, const char* from, const char* text) { addCachedMessage(from, text, false, path_len); _dirty = DIRTY_ALL; }

  int render(DisplayDriver& d) override {
    if (_dirty == DIRTY_NONE) return 3600000;
    if (_dirty == DIRTY_COMPOSER && _view == CHAT) { drawComposer(d); _dirty = DIRTY_NONE; return 3600000; }
    fillScreen(d);
    switch (_view) {
      case HOME: drawHome(d); break;
      case CHAT: drawChat(d); break;
      case NEW_CHAT: drawNewChat(d); break;
      case SETTINGS: drawSettings(d); break;
      case RADIO: drawRadio(d); break;
      case INFO: drawInfo(d); break;
      case REPEATER_INFO: drawRepeaterInfo(d); break;
    }
    _dirty = DIRTY_NONE;
    return 3600000;
  }

  bool handleInput(char c) override {
    if (_view == CHAT) {
      if (c == KEY_CANCEL) { setView(HOME); return true; }
      if (c == KEY_ENTER) { sendCompose(); return true; }
      if (c == 8 || (uint8_t)c == 127) { if (_compose_len) _compose[--_compose_len] = 0; _dirty = DIRTY_COMPOSER; return true; }
      if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && _compose_len < MAX_TEXT_LEN) { _compose[_compose_len++] = c; _compose[_compose_len] = 0; _dirty = DIRTY_COMPOSER; return true; }
      return false;
    }
    if (_view == HOME && _tab == CHATS && _search_active) {
      if (c == KEY_CANCEL) { _search_active = false; _dirty = DIRTY_ALL; return true; }
      if (c == 8 || (uint8_t)c == 127) { if (_search_len) _search[--_search_len] = 0; _dirty = DIRTY_ALL; return true; }
      if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && _search_len < sizeof(_search) - 1) { _search[_search_len++] = c; _search[_search_len] = 0; _list_offset = 0; _dirty = DIRTY_ALL; return true; }
    }
    if (c == KEY_CANCEL) { if (_view != HOME) setView(HOME); else { _search_len = 0; _search[0] = 0; _dirty = DIRTY_ALL; } return true; }
    if (_view == HOME) {
      if (c == KEY_LEFT || c == KEY_RIGHT) { _tab = _tab == CHATS ? REPEATERS : CHATS; _list_offset = _selected = 0; _dirty = DIRTY_ALL; return true; }
      if (c == KEY_UP) { if (_list_offset > 0) _list_offset--; _dirty = DIRTY_ALL; return true; }
      if (c == KEY_DOWN) { _list_offset++; _dirty = DIRTY_ALL; return true; }
      if (c == KEY_ENTER) {
        if (_tab == CHATS) buildChatRows(); else buildRepeaterRows();
        if (_row_count) { int idx = _list_offset; if (_tab == CHATS) openRow(_rows[idx]); else { _info_contact = _rows[idx].contact; setView(REPEATER_INFO); } }
        return true;
      }
    } else if (_view == NEW_CHAT) {
      buildNewRows();
      if (c == KEY_UP && _list_offset > 0) { _list_offset--; _dirty = DIRTY_ALL; return true; }
      if (c == KEY_DOWN) { _list_offset++; clampOffset(4); _dirty = DIRTY_ALL; return true; }
      if (c == KEY_ENTER && _row_count) { int idx = _list_offset; openRow(_rows[idx]); return true; }
    } else if (_view == RADIO && c == KEY_ENTER) {
      if (the_mesh.advert()) _task->showAlert("Advert sent", 900); else _task->showAlert("Advert failed", 1000); return true;
    } else if (_view == SETTINGS && c == KEY_ENTER) {
      if (_task->isBluetoothEnabled()) _task->disableBluetooth(); else _task->enableBluetooth(); _dirty = DIRTY_ALL; return true;
    }
    return false;
  }

  bool handleTouch(int16_t x, int16_t y, uint8_t gesture) {
    if (gesture != TOUCH_TAP) {
      if (_view == HOME || _view == NEW_CHAT) {
        if (gesture == TOUCH_SWIPE_UP) _list_offset++;
        else if (gesture == TOUCH_SWIPE_DOWN) _list_offset--;
        else if (_view == HOME && gesture == TOUCH_SWIPE_LEFT) _tab = REPEATERS;
        else if (_view == HOME && gesture == TOUCH_SWIPE_RIGHT) _tab = CHATS;
        if (_view == HOME) { if (_tab == CHATS) buildChatRows(); else buildRepeaterRows(); clampOffset(); }
        else { buildNewRows(); clampOffset(4); }
        _dirty = DIRTY_ALL; return true;
      }
      return false;
    }

    if (y < 44) {
      if (x >= 276) { setView(SETTINGS); return true; }
      if (x >= 232) { setView(RADIO); return true; }
    }

    if (_view == HOME) {
      if (y >= 45 && y <= 74) { _tab = x < 160 ? CHATS : REPEATERS; _list_offset = _selected = 0; _search_active = false; _dirty = DIRTY_ALL; return true; }
      if (_tab == CHATS && y >= 76 && y <= 105) { _search_active = true; _dirty = DIRTY_ALL; return true; }
      if (y >= 108 && y < 204) {
        if (_tab == CHATS) buildChatRows(); else buildRepeaterRows();
        int row = (y - 108) / 32; int idx = _list_offset + row;
        if (idx < _row_count) { if (_tab == CHATS) openRow(_rows[idx]); else { _info_contact = _rows[idx].contact; setView(REPEATER_INFO); } }
        return true;
      }
      if (_tab == CHATS && y >= 205) { setView(NEW_CHAT); return true; }
      if (_tab == REPEATERS && y >= 205) { _task->showAlert(x < 160 ? "Coverage map comes next" : "Network health comes next", 1100); return true; }
    }

    if (_view == CHAT) {
      if (y >= 47 && y <= 79 && x < 40) { setView(HOME); return true; }
      if (y >= 47 && y <= 79 && x >= 228) { setView(INFO); return true; }
      if (y >= 198 && x >= 266) { sendCompose(); return true; }
      if (y >= 198 && x >= 208 && x < 266) { _task->showAlert("Voice is not enabled in this T-Deck build", 1200); return true; }
      return true;
    }

    if (_view == NEW_CHAT) {
      if (y >= 47 && y <= 79 && x < 40) { setView(HOME); return true; }
      if (y >= 82) { buildNewRows(); int row = (y - 82) / 36; int idx = _list_offset + row; if (row < 4 && idx < _row_count) openRow(_rows[idx]); return true; }
    }

    if (_view == SETTINGS) {
      if (y >= 47 && y <= 79 && x < 40) { setView(HOME); return true; }
      if (y >= 84 && y <= 122) { if (_task->isBluetoothEnabled()) _task->disableBluetooth(); else _task->enableBluetooth(); _dirty = DIRTY_ALL; return true; }
    }

    if (_view == RADIO) {
      if (y >= 47 && y <= 79 && x < 40) { setView(HOME); return true; }
      if (y >= 192) { if (the_mesh.advert()) _task->showAlert("Advert sent", 900); else _task->showAlert("Advert failed", 1000); return true; }
    }

    if (_view == INFO || _view == REPEATER_INFO) {
      if (y >= 47 && y <= 79 && x < 40) { if (_view == INFO) { _view = CHAT; _dirty = DIRTY_ALL; } else setView(HOME); return true; }
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors_ptr, NodePrefs* node_prefs) {
  _display = display; _sensors = sensors_ptr; _node_prefs = node_prefs; _msgcount = 0;
  _ui_started_at = millis(); _auto_off = millis() + AUTO_OFF_MILLIS; _alert_expiry = 0;
  _keyboard_poll_at = _trackball_poll_at = _touch_poll_at = 0; _touch_down = false;
#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
  pinMode(TDECK_TRACK_RIGHT, INPUT_PULLUP); pinMode(TDECK_TRACK_UP, INPUT_PULLUP);
  pinMode(TDECK_TRACK_LEFT, INPUT_PULLUP); pinMode(TDECK_TRACK_DOWN, INPUT_PULLUP);
  pinMode(TDECK_TOUCH_INT, INPUT);
  if (_display) _display->turnOn();
  initTouch();
  home = new CommunicatorScreen(this, &rtc_clock); curr = home; _next_refresh = 0;
}

void UITask::setCurrScreen(UIScreen* c) { curr = c; _next_refresh = 0; }

void UITask::showAlert(const char* text, int duration_millis) {
  StrHelper::strncpy(_alert, text ? text : "", sizeof(_alert)); _alert_expiry = millis() + duration_millis; _next_refresh = 0;
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount; if (msgcount == 0 && home) ((CommunicatorScreen*)home)->clearUnread(); _next_refresh = 0;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount; if (home) ((CommunicatorScreen*)home)->addMessage(path_len, from_name, text);
  if (_display) { if (!_display->isOn()) _display->turnOn(); _auto_off = millis() + AUTO_OFF_MILLIS; _next_refresh = 0; }
}

void UITask::notify(UIEventType t) { (void)t; }

bool UITask::gtRead(uint16_t reg, uint8_t* dest, size_t len) {
  if (!_touch_addr || !dest || !len) return false;
  Wire.beginTransmission(_touch_addr); Wire.write((uint8_t)(reg >> 8)); Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom((uint8_t)_touch_addr, (uint8_t)len);
  if (got < len) { while (Wire.available()) Wire.read(); return false; }
  for (size_t i = 0; i < len; ++i) dest[i] = (uint8_t)Wire.read();
  return true;
}

bool UITask::gtWriteByte(uint16_t reg, uint8_t value) {
  if (!_touch_addr) return false;
  Wire.beginTransmission(_touch_addr); Wire.write((uint8_t)(reg >> 8)); Wire.write((uint8_t)(reg & 0xFF)); Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool UITask::initTouch() {
  const uint8_t candidates[2] = {0x5D, 0x14};
  uint8_t id[4];
  for (int i = 0; i < 2; ++i) {
    _touch_addr = candidates[i];
    if (gtRead(0x8140, id, sizeof(id))) { _touch_down = false; return true; }
  }
  _touch_addr = 0; return false;
}

bool UITask::pollTouch(int16_t& x, int16_t& y, uint8_t& gesture) {
  if (!_touch_addr || millis() < _touch_poll_at) return false;
  _touch_poll_at = millis() + 12;
  uint8_t status = 0;
  if (!gtRead(0x814E, &status, 1)) return false;
  bool ready = (status & 0x80) != 0;
  uint8_t count = status & 0x0F;
  if (ready && count > 0) {
    uint8_t p[8];
    if (gtRead(0x814F, p, sizeof(p))) {
      int16_t rawX = (int16_t)(p[1] | (p[2] << 8));
      int16_t rawY = (int16_t)(p[3] | (p[4] << 8));
      // Match LILYGO's official T-Deck GT911 transform: swap XY, then mirror Y.
      _touch_x = rawY; _touch_y = 239 - rawX;
      if (_touch_x < 0) _touch_x = 0; if (_touch_x > 319) _touch_x = 319;
      if (_touch_y < 0) _touch_y = 0; if (_touch_y > 239) _touch_y = 239;
      if (!_touch_down) { _touch_start_x = _touch_x; _touch_start_y = _touch_y; _touch_down = true; }
      _touch_last_seen = millis();
    }
    gtWriteByte(0x814E, 0);
    return false;
  }
  if (ready) gtWriteByte(0x814E, 0);
  if (_touch_down && (ready || millis() - _touch_last_seen > 120)) {
    _touch_down = false; x = _touch_x; y = _touch_y;
    int dx = _touch_x - _touch_start_x, dy = _touch_y - _touch_start_y;
    if (abs(dx) < 28 && abs(dy) < 28) gesture = TOUCH_TAP;
    else if (abs(dy) >= abs(dx)) gesture = dy < 0 ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
    else gesture = dx < 0 ? TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT;
    return true;
  }
  return false;
}

char UITask::pollTrackball() {
  if (millis() < _trackball_poll_at) return 0; _trackball_poll_at = millis() + 8;
  struct S { int pin; char key; bool last; };
  static bool init = false; static S s[4] = {{TDECK_TRACK_RIGHT,KEY_RIGHT,true},{TDECK_TRACK_UP,KEY_UP,true},{TDECK_TRACK_LEFT,KEY_LEFT,true},{TDECK_TRACK_DOWN,KEY_DOWN,true}};
  if (!init) { for (auto& v : s) v.last = digitalRead(v.pin); init = true; return 0; }
  for (auto& v : s) { bool now = digitalRead(v.pin); if (v.last && !now) { v.last = now; return v.key; } v.last = now; }
  return 0;
}

char UITask::pollKeyboard() {
  if (millis() < _keyboard_poll_at) return 0; _keyboard_poll_at = millis() + 20;
  Wire.beginTransmission(TDECK_KEYBOARD_ADDR); if (Wire.endTransmission() != 0) return 0;
  Wire.requestFrom((uint8_t)TDECK_KEYBOARD_ADDR, (uint8_t)1); if (!Wire.available()) return 0;
  char c = (char)Wire.read(); if (!c) return 0; if (c == '\n' || c == '\r') return KEY_ENTER; if (c == 27) return KEY_CANCEL; return c;
}

char UITask::checkDisplayOn(char c) {
  if (_display && !_display->isOn()) { _display->turnOn(); if (home) ((CommunicatorScreen*)home)->markAllDirty(); _next_refresh = 0; _auto_off = millis() + AUTO_OFF_MILLIS; return 0; }
  _auto_off = millis() + AUTO_OFF_MILLIS; return c;
}

char UITask::pollInput() {
  char c = 0;
#if defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) c = KEY_ENTER;
  else if (ev == BUTTON_EVENT_LONG_PRESS && millis() - _ui_started_at < 8000) { the_mesh.enterCLIRescue(); return 0; }
#endif
  if (!c) c = pollTrackball(); if (!c) c = pollKeyboard(); if (!c) return 0; return checkDisplayOn(c);
}

void UITask::loop() {
  if (_alert_expiry && millis() >= _alert_expiry) {
    _alert_expiry = 0; if (home) ((CommunicatorScreen*)home)->markAllDirty(); _next_refresh = 0;
  }

  int16_t tx = 0, ty = 0; uint8_t gesture = TOUCH_TAP;
  if (pollTouch(tx, ty, gesture)) {
    if (_display && !_display->isOn()) { _display->turnOn(); if (home) ((CommunicatorScreen*)home)->markAllDirty(); }
    else if (home) ((CommunicatorScreen*)home)->handleTouch(tx, ty, gesture);
    _auto_off = millis() + AUTO_OFF_MILLIS; _next_refresh = 0;
  }

  char c = pollInput();
  if (c && curr) { curr->handleInput(c); _next_refresh = 0; }
  if (curr) curr->poll();

  if (_display && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame(); int delay_ms = curr->render(*_display);
      if (_alert_expiry) {
        const int x = 38, y = 95, w = 244, h = 46;
        _display->setColor(MCC_CARD2); _display->fillRoundRect(x, y, w, h, 8);
        _display->setColor(MCC_ACCENT); _display->drawRoundRect(x, y, w, h, 8);
        _display->setTextSize(1); _display->setColor(MCC_TEXT); _display->drawTextCentered(160, y + 18, _alert);
        _next_refresh = _alert_expiry;
      } else _next_refresh = millis() + (unsigned long)delay_ms;
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) _display->turnOff();
#endif
  }
}
