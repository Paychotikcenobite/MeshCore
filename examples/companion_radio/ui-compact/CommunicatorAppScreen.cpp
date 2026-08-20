#include "CommunicatorAppScreen.h"
#include "UITask.h"
#include "../MyMesh.h"
#include <Preferences.h>
#include <helpers/TxtDataHelpers.h>
#include <math.h>

namespace {

constexpr ColorVal rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct CompactPalette {
  ColorVal bg, card, card2, text, sub, accent, accentText, incoming, outgoing, divider, danger, input, stroke;
};

CompactPalette paletteFor(bool light) {
  if (light) {
    return {
      rgb565(247,249,252), rgb565(255,255,255), rgb565(238,243,249), rgb565(20,28,40),
      rgb565(92,108,128), rgb565(11,58,117), rgb565(11,58,117), rgb565(229,234,240),
      rgb565(31,99,198), rgb565(218,225,233), rgb565(195,45,45), rgb565(222,233,245), rgb565(176,198,221)
    };
  }
  return {
    rgb565(5,16,32), rgb565(12,29,50), rgb565(17,38,63), rgb565(255,255,255),
    rgb565(166,185,207), rgb565(18,63,122), rgb565(255,255,255), rgb565(31,48,70),
    rgb565(19,79,166), rgb565(39,57,78), rgb565(220,70,70), rgb565(27,57,91), rgb565(55,88,124)
  };
}

const ColorVal MCC_GREEN = rgb565(35,184,92);
const ColorVal MCC_AMBER = rgb565(240,174,45);
const ColorVal MCC_MUTED = rgb565(130,140,150);
const ColorVal MAP_GREEN_RECENT = rgb565(0x16,0x8A,0x49);
const ColorVal MAP_GREEN_MID = rgb565(0x63,0xB8,0x78);
const ColorVal MAP_GREEN_STALE = rgb565(0xB8,0xD8,0xBF);
const ColorVal MAP_NEVER = rgb565(0x8D,0x94,0x9E);
const ColorVal SELF_BLUE = rgb565(0x16,0x87,0xFF);

void ageLabel(mesh::RTCClock* rtc, uint32_t ts, char* out, size_t len) {
  if (!ts) { StrHelper::strncpy(out, "", len); return; }
  int32_t age = (int32_t)(rtc->getCurrentTime() - ts);
  if (age < 0) age = 0;
  if (age < 60) snprintf(out, len, "now");
  else if (age < 3600) snprintf(out, len, "%ldm", (long)(age / 60));
  else if (age < 86400) snprintf(out, len, "%ldh", (long)(age / 3600));
  else if (age < 14 * 86400) snprintf(out, len, "%ldd", (long)(age / 86400));
  else snprintf(out, len, "old");
}

ColorVal freshnessColor(mesh::RTCClock* rtc, uint32_t ts) {
  if (!ts) return MAP_NEVER;
  int32_t age = (int32_t)(rtc->getCurrentTime() - ts);
  if (age < 0) age = 0;
  if (age <= 48 * 60 * 60) return MAP_GREEN_RECENT;
  if (age <= 7 * 24 * 60 * 60) return MAP_GREEN_MID;
  return MAP_GREEN_STALE;
}

void initialsFor(const char* name, char out[3]) {
  out[0] = 'M'; out[1] = 'C'; out[2] = 0;
  if (!name || !name[0]) return;
  out[0] = name[0]; out[1] = 0;
  const char* p = name;
  while (*p && *p != ' ') ++p;
  if (*p == ' ' && p[1]) { out[1] = p[1]; out[2] = 0; }
  else if (name[1]) { out[1] = name[1]; out[2] = 0; }
}

bool validLatLon(double lat, double lon) {
  return isfinite(lat) && isfinite(lon) && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0 && !(lat == 0.0 && lon == 0.0);
}

} // namespace

CommunicatorAppScreen::CommunicatorAppScreen(UITask* task, mesh::RTCClock* rtc)
  : _task(task), _rtc(rtc), _route(ROUTE_MAIN), _route_depth(0), _tab(TAB_CHATS), _filter(FILTER_ALL),
    _dirty(DIRTY_ALL), _light_mode(false), _show_public(false), _search_active(false), _chat_search_active(false),
    _editing_alias(false), _search_len(0), _chat_search_len(0), _compose_len(0), _edit_len(0), _selected(0),
    _list_offset(0), _quick_selected(0), _message_scroll(0), _message_count(0), _message_head(MESSAGE_CACHE - 1),
    _contact_count(0), _repeater_count(0), _channel_count(0), _meta_count(0), _row_count(0),
    _active_kind(ROW_NONE), _active_channel_index(0) {
  memset(_route_stack, 0, sizeof(_route_stack));
  memset(_messages, 0, sizeof(_messages));
  memset(_contacts, 0, sizeof(_contacts));
  memset(_repeaters, 0, sizeof(_repeaters));
  memset(_channels, 0, sizeof(_channels));
  memset(_channel_indexes, 0, sizeof(_channel_indexes));
  memset(_meta, 0, sizeof(_meta));
  memset(_rows, 0, sizeof(_rows));
  memset(&_active_contact, 0, sizeof(_active_contact));
  memset(&_active_channel, 0, sizeof(_active_channel));
  memset(&_info_contact, 0, sizeof(_info_contact));
  _search[0] = _chat_search[0] = _compose[0] = _edit[0] = _active_name[0] = 0;
  _feature_title[0] = _feature_body[0] = 0;
  loadPrefs();
}

void CommunicatorAppScreen::loadPrefs() {
  Preferences p;
  if (!p.begin("mccui", true)) return;
  _light_mode = p.getBool("light", false);
  _show_public = p.getBool("world", false);
  uint8_t count = p.getUChar("mcount", 0);
  size_t bytes = p.getBytesLength("meta");
  if (bytes == sizeof(_meta)) {
    p.getBytes("meta", _meta, sizeof(_meta));
    _meta_count = count > MAX_META ? MAX_META : count;
  }
  p.end();
}

void CommunicatorAppScreen::savePrefs() {
  Preferences p;
  if (!p.begin("mccui", false)) return;
  p.putBool("light", _light_mode);
  p.putBool("world", _show_public);
  p.end();
}

void CommunicatorAppScreen::saveMeta() {
  Preferences p;
  if (!p.begin("mccui", false)) return;
  p.putUChar("mcount", _meta_count);
  p.putBytes("meta", _meta, sizeof(_meta));
  p.end();
}

void CommunicatorAppScreen::loadContactsAndChannels() {
  _contact_count = _repeater_count = 0;
  ContactInfo c;
  ContactsIterator it = the_mesh.startContactsIterator();
  while (it.hasNext(&the_mesh, c)) {
    if (c.type == ADV_TYPE_CHAT && c.name[0] && _contact_count < MAX_LOCAL_CONTACTS) _contacts[_contact_count++] = c;
    else if (c.type == ADV_TYPE_REPEATER && c.name[0] && _repeater_count < MAX_LOCAL_REPEATERS) _repeaters[_repeater_count++] = c;
  }
  _channel_count = 0;
#ifdef MAX_GROUP_CHANNELS
  for (int i = 0; i < MAX_GROUP_CHANNELS && _channel_count < MAX_LOCAL_CHANNELS; ++i) {
    ChannelDetails ch;
    if (the_mesh.getChannel(i, ch) && ch.name[0]) {
      _channels[_channel_count] = ch;
      _channel_indexes[_channel_count] = (uint8_t)i;
      ++_channel_count;
    }
  }
#endif
}

int CommunicatorAppScreen::findContactByName(const char* name) const {
  for (int i = 0; i < _contact_count; ++i) if (strcmp(_contacts[i].name, name) == 0) return i;
  return -1;
}

int CommunicatorAppScreen::findChannelByName(const char* name) const {
  for (int i = 0; i < _channel_count; ++i) if (strcmp(_channels[i].name, name) == 0) return i;
  return -1;
}

int CommunicatorAppScreen::findMetaForContact(const ContactInfo& c, bool create) {
  for (int i = 0; i < _meta_count; ++i) {
    if (_meta[i].kind == ROW_CONTACT && memcmp(_meta[i].identity, c.id.pub_key, 32) == 0) return i;
  }
  if (!create || _meta_count >= MAX_META) return -1;
  int idx = _meta_count++;
  memset(&_meta[idx], 0, sizeof(_meta[idx]));
  _meta[idx].kind = ROW_CONTACT;
  memcpy(_meta[idx].identity, c.id.pub_key, 32);
  saveMeta();
  return idx;
}

int CommunicatorAppScreen::findMetaForChannel(const ChannelDetails& c, uint8_t idx, bool create) {
  for (int i = 0; i < _meta_count; ++i) {
    if (_meta[i].kind == ROW_CHANNEL && _meta[i].channel_index == idx && memcmp(_meta[i].identity, c.channel.secret, 32) == 0) return i;
  }
  if (!create || _meta_count >= MAX_META) return -1;
  int m = _meta_count++;
  memset(&_meta[m], 0, sizeof(_meta[m]));
  _meta[m].kind = ROW_CHANNEL;
  _meta[m].channel_index = idx;
  memcpy(_meta[m].identity, c.channel.secret, 32);
  saveMeta();
  return m;
}

bool CommunicatorAppScreen::metaFlag(int idx, uint8_t mask) const {
  return idx >= 0 && idx < _meta_count && (_meta[idx].flags & mask) != 0;
}

void CommunicatorAppScreen::toggleMetaFlag(int idx, uint8_t mask) {
  if (idx < 0 || idx >= _meta_count) return;
  _meta[idx].flags ^= mask;
  saveMeta();
  _dirty = DIRTY_ALL;
}

const char* CommunicatorAppScreen::displayNameFor(const Row& row, char* out, size_t len) const {
  if (row.meta_index >= 0 && row.meta_index < _meta_count && _meta[row.meta_index].alias[0]) {
    StrHelper::strncpy(out, _meta[row.meta_index].alias, len);
  } else {
    StrHelper::strncpy(out, row.name, len);
  }
  return out;
}

bool CommunicatorAppScreen::containsInsensitive(const char* hay, const char* needle) const {
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

bool CommunicatorAppScreen::rowNameExists(const char* name) const {
  for (int i = 0; i < _row_count; ++i) if (strcmp(_rows[i].name, name) == 0) return true;
  return false;
}

void CommunicatorAppScreen::pushRow(const Row& row) {
  if (_row_count < MAX_ROWS) _rows[_row_count++] = row;
}

bool CommunicatorAppScreen::rowNeedsAttention(const Row& row) const { return row.attention > 0; }

void CommunicatorAppScreen::sortChatRows() {
  for (int i = 0; i < _row_count; ++i) {
    for (int j = i + 1; j < _row_count; ++j) {
      bool pi = metaFlag(_rows[i].meta_index, 0x02);
      bool pj = metaFlag(_rows[j].meta_index, 0x02);
      bool swap = (!pi && pj) || (pi == pj && _rows[j].timestamp > _rows[i].timestamp);
      if (swap) { Row t = _rows[i]; _rows[i] = _rows[j]; _rows[j] = t; }
    }
  }
}

void CommunicatorAppScreen::buildChatRows() {
  loadContactsAndChannels();
  _row_count = 0;

  for (int n = 0; n < _message_count && _row_count < MAX_ROWS; ++n) {
    int mi = (_message_head + MESSAGE_CACHE - n) % MESSAGE_CACHE;
    const MessageEntry& m = _messages[mi];
    if (!m.origin[0] || rowNameExists(m.origin)) continue;
    int ch = findChannelByName(m.origin);
    int ct = findContactByName(m.origin);
    Row r; memset(&r, 0, sizeof(r)); r.meta_index = -1;
    StrHelper::strncpy(r.name, m.origin, sizeof(r.name));
    StrHelper::strncpy(r.preview, m.text, sizeof(r.preview));
    r.timestamp = m.timestamp;
    if (ch >= 0) {
      r.kind = ROW_CHANNEL; r.channel = _channels[ch]; r.channel_index = _channel_indexes[ch];
      r.meta_index = (int8_t)findMetaForChannel(r.channel, r.channel_index, true);
      if (!_show_public && r.channel_index == 0) continue;
    } else if (ct >= 0) {
      r.kind = ROW_CONTACT; r.contact = _contacts[ct];
      r.meta_index = (int8_t)findMetaForContact(r.contact, true);
    } else r.kind = ROW_UNKNOWN;
    if (metaFlag(r.meta_index, 0x08)) continue;
    for (int j = 0; j < MESSAGE_CACHE; ++j) {
      if (strcmp(_messages[j].origin, m.origin) == 0) {
        if (_messages[j].unread) r.unread += _messages[j].unread;
        if (_messages[j].outgoing && _messages[j].send_state == SEND_FAILED) ++r.attention;
      }
    }
    char shown[32]; displayNameFor(r, shown, sizeof(shown));
    if (!containsInsensitive(shown, _search) && !containsInsensitive(r.preview, _search)) continue;
    if (_filter == FILTER_FAVORITES && !metaFlag(r.meta_index, 0x01)) continue;
    if (_filter == FILTER_UNREAD && !r.unread) continue;
    if (_filter == FILTER_ATTENTION && !rowNeedsAttention(r)) continue;
    pushRow(r);
  }

  for (int i = 0; i < _channel_count && _row_count < MAX_ROWS; ++i) {
    if (!_show_public && _channel_indexes[i] == 0) continue;
    if (rowNameExists(_channels[i].name)) continue;
    Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_CHANNEL; r.channel = _channels[i]; r.channel_index = _channel_indexes[i];
    r.meta_index = (int8_t)findMetaForChannel(r.channel, r.channel_index, true);
    if (metaFlag(r.meta_index, 0x08)) continue;
    StrHelper::strncpy(r.name, _channels[i].name, sizeof(r.name));
    StrHelper::strncpy(r.preview, r.channel_index == 0 ? "Public / World" : "Group chat", sizeof(r.preview));
    char shown[32]; displayNameFor(r, shown, sizeof(shown));
    if (!containsInsensitive(shown, _search)) continue;
    if (_filter == FILTER_FAVORITES && !metaFlag(r.meta_index, 0x01)) continue;
    if (_filter == FILTER_UNREAD || _filter == FILTER_ATTENTION) continue;
    pushRow(r);
  }

  for (int i = 0; i < _contact_count && _row_count < MAX_ROWS; ++i) {
    if (rowNameExists(_contacts[i].name)) continue;
    Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_CONTACT; r.contact = _contacts[i];
    r.meta_index = (int8_t)findMetaForContact(r.contact, true);
    if (metaFlag(r.meta_index, 0x08)) continue;
    StrHelper::strncpy(r.name, _contacts[i].name, sizeof(r.name));
    StrHelper::strncpy(r.preview, "Tap to start messaging", sizeof(r.preview));
    char shown[32]; displayNameFor(r, shown, sizeof(shown));
    if (!containsInsensitive(shown, _search)) continue;
    if (_filter == FILTER_FAVORITES && !metaFlag(r.meta_index, 0x01)) continue;
    if (_filter == FILTER_UNREAD || _filter == FILTER_ATTENTION) continue;
    pushRow(r);
  }

  sortChatRows();
  clampOffset(2);
}

void CommunicatorAppScreen::buildRepeaterRows() {
  loadContactsAndChannels();
  _row_count = 0;
  for (int i = 0; i < _repeater_count && _row_count < MAX_ROWS; ++i) {
    Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_REPEATER; r.contact = _repeaters[i]; r.meta_index = -1;
    StrHelper::strncpy(r.name, _repeaters[i].name, sizeof(r.name));
    if (_repeaters[i].out_path_len == OUT_PATH_UNKNOWN) StrHelper::strncpy(r.preview, "Route: flood / unknown", sizeof(r.preview));
    else snprintf(r.preview, sizeof(r.preview), "Route: %u hop%s", _repeaters[i].out_path_len, _repeaters[i].out_path_len == 1 ? "" : "s");
    r.timestamp = _repeaters[i].last_advert_timestamp;
    pushRow(r);
  }
  for (int i = 0; i < _row_count; ++i) {
    for (int j = i + 1; j < _row_count; ++j) {
      if (_rows[j].timestamp > _rows[i].timestamp) { Row t = _rows[i]; _rows[i] = _rows[j]; _rows[j] = t; }
    }
  }
  clampOffset(2);
}

void CommunicatorAppScreen::buildNewConversationRows() {
  loadContactsAndChannels();
  _row_count = 0;
  for (int i = 0; i < _contact_count && _row_count < MAX_ROWS; ++i) {
    Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_CONTACT; r.contact = _contacts[i];
    r.meta_index = (int8_t)findMetaForContact(r.contact, true);
    StrHelper::strncpy(r.name, _contacts[i].name, sizeof(r.name));
    StrHelper::strncpy(r.preview, "Direct message", sizeof(r.preview));
    pushRow(r);
  }
  for (int i = 0; i < _channel_count && _row_count < MAX_ROWS; ++i) {
    if (!_show_public && _channel_indexes[i] == 0) continue;
    Row r; memset(&r, 0, sizeof(r)); r.kind = ROW_CHANNEL; r.channel = _channels[i]; r.channel_index = _channel_indexes[i];
    r.meta_index = (int8_t)findMetaForChannel(r.channel, r.channel_index, true);
    StrHelper::strncpy(r.name, _channels[i].name, sizeof(r.name));
    StrHelper::strncpy(r.preview, r.channel_index == 0 ? "Public / World" : "Group chat", sizeof(r.preview));
    pushRow(r);
  }
  for (int i = 0; i < _row_count; ++i) {
    for (int j = i + 1; j < _row_count; ++j) {
      if (metaFlag(_rows[j].meta_index, 0x01) && !metaFlag(_rows[i].meta_index, 0x01)) { Row t = _rows[i]; _rows[i] = _rows[j]; _rows[j] = t; }
    }
  }
  clampOffset(3);
}

void CommunicatorAppScreen::clampOffset(int visible) {
  int maxOffset = _row_count > visible ? _row_count - visible : 0;
  if (_list_offset < 0) _list_offset = 0;
  if (_list_offset > maxOffset) _list_offset = maxOffset;
  if (_selected < 0) _selected = 0;
  if (_selected >= _row_count && _row_count) _selected = _row_count - 1;
}

void CommunicatorAppScreen::pushRoute(Route route) {
  if (_route_depth < sizeof(_route_stack) / sizeof(_route_stack[0])) _route_stack[_route_depth++] = _route;
  _route = route; _selected = 0; _list_offset = 0; _dirty = DIRTY_ALL;
}

void CommunicatorAppScreen::replaceRoute(Route route) {
  _route = route; _selected = 0; _list_offset = 0; _dirty = DIRTY_ALL;
}

void CommunicatorAppScreen::goBack() {
  if (_route_depth) _route = _route_stack[--_route_depth];
  else _route = ROUTE_MAIN;
  _selected = 0; _list_offset = 0; _search_active = false; _chat_search_active = false; _dirty = DIRTY_ALL;
}

void CommunicatorAppScreen::goHome() {
  _route = ROUTE_MAIN; _route_depth = 0; _selected = 0; _list_offset = 0; _search_active = false; _chat_search_active = false; _dirty = DIRTY_ALL;
}

void CommunicatorAppScreen::openRow(const Row& r) {
  _active_kind = r.kind;
  StrHelper::strncpy(_active_name, r.name, sizeof(_active_name));
  if (r.kind == ROW_CONTACT) _active_contact = r.contact;
  if (r.kind == ROW_CHANNEL) { _active_channel = r.channel; _active_channel_index = r.channel_index; }
  _compose_len = 0; _compose[0] = 0; _message_scroll = 0;
  clearUnreadFor(r.name);
  pushRoute(ROUTE_CHAT);
}

void CommunicatorAppScreen::openFeature(const char* title, const char* body) {
  StrHelper::strncpy(_feature_title, title ? title : "Feature", sizeof(_feature_title));
  StrHelper::strncpy(_feature_body, body ? body : "", sizeof(_feature_body));
  pushRoute(ROUTE_FEATURE_NOTE);
}

void CommunicatorAppScreen::beginAliasEdit() {
  int meta = -1;
  if (_active_kind == ROW_CONTACT) meta = findMetaForContact(_active_contact, true);
  else if (_active_kind == ROW_CHANNEL) meta = findMetaForChannel(_active_channel, _active_channel_index, true);
  _edit_len = 0; _edit[0] = 0;
  if (meta >= 0 && _meta[meta].alias[0]) StrHelper::strncpy(_edit, _meta[meta].alias, sizeof(_edit));
  else StrHelper::strncpy(_edit, _active_name, sizeof(_edit));
  _edit_len = strlen(_edit);
  pushRoute(ROUTE_ALIAS_EDIT);
}

void CommunicatorAppScreen::commitAliasEdit() {
  int meta = -1;
  if (_active_kind == ROW_CONTACT) meta = findMetaForContact(_active_contact, true);
  else if (_active_kind == ROW_CHANNEL) meta = findMetaForChannel(_active_channel, _active_channel_index, true);
  if (meta >= 0) {
    if (strcmp(_edit, _active_name) == 0) _meta[meta].alias[0] = 0;
    else StrHelper::strncpy(_meta[meta].alias, _edit, sizeof(_meta[meta].alias));
    saveMeta();
    _task->showAlert("Local name saved", 900);
  }
  goBack();
}

void CommunicatorAppScreen::clearUnreadFor(const char* name) {
  for (int i = 0; i < MESSAGE_CACHE; ++i) if (strcmp(_messages[i].origin, name) == 0) _messages[i].unread = 0;
}

void CommunicatorAppScreen::deleteLocalMessagesFor(const char* name) {
  for (int i = 0; i < MESSAGE_CACHE; ++i) if (strcmp(_messages[i].origin, name) == 0) memset(&_messages[i], 0, sizeof(_messages[i]));
  _dirty = DIRTY_ALL;
}

void CommunicatorAppScreen::addCachedMessage(const char* origin, const char* text, bool outgoing, uint8_t path_len, SendState state) {
  _message_head = (_message_head + 1) % MESSAGE_CACHE;
  if (_message_count < MESSAGE_CACHE) ++_message_count;
  MessageEntry& m = _messages[_message_head]; memset(&m, 0, sizeof(m));
  m.timestamp = _rtc->getCurrentTime(); m.path_len = path_len; m.outgoing = outgoing; m.unread = outgoing ? 0 : 1; m.send_state = state;
  StrHelper::strncpy(m.origin, origin ? origin : "Unknown", sizeof(m.origin));
  StrHelper::strncpy(m.text, text ? text : "", sizeof(m.text));
}

void CommunicatorAppScreen::addMessage(uint8_t path_len, const char* from, const char* text) {
  addCachedMessage(from, text, false, path_len, SEND_NONE);
  _dirty = DIRTY_ALL;
}

void CommunicatorAppScreen::clearUnread() {
  for (int i = 0; i < MESSAGE_CACHE; ++i) _messages[i].unread = 0;
  _dirty = DIRTY_ALL;
}

void CommunicatorAppScreen::markAllDirty() { _dirty = DIRTY_ALL; }

bool CommunicatorAppScreen::sendCompose() {
  if (!_compose_len) { _task->showAlert("Message is empty", 800); return false; }
  bool ok = false;
  if (_active_kind == ROW_CONTACT) {
    uint32_t expected_ack = 0, est_timeout = 0;
    int result = the_mesh.sendMessage(_active_contact, _rtc->getCurrentTime(), 0, _compose, expected_ack, est_timeout);
    ok = result != MSG_SEND_FAILED;
  } else if (_active_kind == ROW_CHANNEL) {
    ok = the_mesh.sendGroupMessage(_rtc->getCurrentTime(), _active_channel.channel, _task->getNodePrefs()->node_name, _compose, _compose_len);
  }
  if (_active_kind != ROW_CONTACT && _active_kind != ROW_CHANNEL) {
    _task->showAlert("Conversation is read-only", 900); return false;
  }
  addCachedMessage(_active_name, _compose, true, 0xFF, ok ? SEND_SENT : SEND_FAILED);
  _compose_len = 0; _compose[0] = 0; _message_scroll = 0; _dirty = DIRTY_ALL;
  _task->showAlert(ok ? "Sent" : "Failed - needs attention", ok ? 700 : 1200);
  return ok;
}

int CommunicatorAppScreen::collectActiveMessages(int indexes[], int maxn) const {
  int total = 0;
  int temp[MESSAGE_CACHE];
  for (int n = 0; n < _message_count && total < MESSAGE_CACHE; ++n) {
    int idx = (_message_head + MESSAGE_CACHE - n) % MESSAGE_CACHE;
    if (_messages[idx].origin[0] && strcmp(_messages[idx].origin, _active_name) == 0) temp[total++] = idx;
  }
  int skip = _message_scroll;
  int count = 0;
  for (int n = skip; n < total && count < maxn; ++n) indexes[count++] = temp[n];
  for (int i = 0; i < count / 2; ++i) { int t = indexes[i]; indexes[i] = indexes[count - 1 - i]; indexes[count - 1 - i] = t; }
  return count;
}

void CommunicatorAppScreen::fillScreen(DisplayDriver& d) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.bg); d.fillRect(0, 0, d.width(), d.height());
}

void CommunicatorAppScreen::drawLogo(DisplayDriver& d) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.accent); d.fillCircle(19, 20, 15); d.setTextSize(1); d.setColor(p.accentText); d.drawTextCentered(19, 16, "MC");
}

void CommunicatorAppScreen::drawRadioGlyph(DisplayDriver& d, int cx, int cy) {
  d.setColor(MCC_GREEN); d.fillCircle(cx, cy + 6, 2); d.drawLine(cx, cy + 3, cx, cy - 4); d.drawLine(cx - 4, cy + 1, cx - 7, cy - 2); d.drawLine(cx + 4, cy + 1, cx + 7, cy - 2); d.drawLine(cx - 7, cy - 4, cx - 10, cy - 7); d.drawLine(cx + 7, cy - 4, cx + 10, cy - 7);
}

void CommunicatorAppScreen::drawGear(DisplayDriver& d, int cx, int cy) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.text); d.drawCircle(cx, cy, 7); d.drawCircle(cx, cy, 2); d.drawLine(cx, cy - 11, cx, cy - 7); d.drawLine(cx, cy + 7, cx, cy + 11); d.drawLine(cx - 11, cy, cx - 7, cy); d.drawLine(cx + 7, cy, cx + 11, cy);
}

void CommunicatorAppScreen::drawAppHeader(DisplayDriver& d) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.bg); d.fillRect(0, 0, 320, 41); drawLogo(d);
  d.setTextSize(1); d.setColor(p.text); d.setCursor(41, 7); d.print("MeshCore"); d.setCursor(41, 21); d.print("Communicator");
  d.setColor(p.card); d.fillRoundRect(238, 4, 36, 33, 6); d.setColor(p.divider); d.drawRoundRect(238, 4, 36, 33, 6); drawRadioGlyph(d, 256, 21);
  d.setColor(p.card); d.fillRoundRect(279, 4, 36, 33, 6); d.setColor(p.divider); d.drawRoundRect(279, 4, 36, 33, 6); drawGear(d, 297, 21);
}

void CommunicatorAppScreen::drawBack(DisplayDriver& d, int y) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.card2); d.fillRoundRect(6, y, 31, 29, 6); d.setColor(p.text); d.drawLine(25, y + 7, 15, y + 14); d.drawLine(15, y + 14, 25, y + 21);
}

void CommunicatorAppScreen::drawAvatar(DisplayDriver& d, int cx, int cy, const char* name, bool channel) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(channel ? rgb565(77,93,125) : p.accent); d.fillCircle(cx, cy, 13); char initials[3]; initialsFor(name, initials); d.setTextSize(1); d.setColor(rgb565(255,255,255)); d.drawTextCentered(cx, cy - 4, initials);
}

void CommunicatorAppScreen::drawTabs(DisplayDriver& d) {
  CompactPalette p = paletteFor(_light_mode); char rep[24]; snprintf(rep, sizeof(rep), "Repeaters %u", _repeater_count);
  const char* labels[2] = {"Chats", rep};
  for (int i = 0; i < 2; ++i) {
    int x = i ? 162 : 6; int w = 152; bool on = _tab == i;
    d.setColor(on ? p.card2 : p.bg); d.fillRoundRect(x, 44, w, 27, 6); d.setColor(on ? p.text : p.sub); d.setTextSize(1); d.drawTextCentered(x + w / 2, 54, labels[i]);
    if (on) { d.setColor(p.accent); d.fillRect(x + 18, 69, w - 36, 2); }
  }
}

void CommunicatorAppScreen::drawSearch(DisplayDriver& d) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.input); d.fillRoundRect(7, 75, 306, 25, 6); d.setColor(_search_active ? p.accent : p.stroke); d.drawRoundRect(7, 75, 306, 25, 6); d.setColor(_search[0] ? p.text : p.sub); d.setTextSize(1); d.drawTextEllipsized(17, 84, 270, _search[0] ? _search : "Search conversations"); d.setColor(p.sub); d.drawCircle(296, 86, 5); d.drawLine(300, 90, 304, 94);
}

void CommunicatorAppScreen::drawFilterChips(DisplayDriver& d) {
  CompactPalette p = paletteFor(_light_mode); const char* labels[4] = {"All", "Favorites", "Unread", "Attention"};
  const int xs[4] = {7, 84, 161, 238};
  for (int i = 0; i < 4; ++i) {
    bool on = _filter == i; d.setColor(on ? p.card2 : p.bg); d.fillRoundRect(xs[i], 103, 74, 22, 5); d.setColor(on ? p.accent : p.divider); d.drawRoundRect(xs[i], 103, 74, 22, 5); d.setColor(on ? p.text : p.sub); d.setTextSize(1); d.drawTextCentered(xs[i] + 37, 111, labels[i]);
  }
}

void CommunicatorAppScreen::drawConversationRow(DisplayDriver& d, const Row& r, int y, int h, bool selected) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.card); d.fillRoundRect(7, y, 306, h, 6); if (selected) { d.setColor(p.stroke); d.drawRoundRect(7, y, 306, h, 6); }
  char shown[32]; displayNameFor(r, shown, sizeof(shown)); drawAvatar(d, 26, y + h / 2, shown, r.kind == ROW_CHANNEL);
  d.setTextSize(1); d.setColor(p.text); int nx = 45;
  if (metaFlag(r.meta_index, 0x01)) { d.setCursor(nx, y + 5); d.print("*"); nx += 8; }
  if (metaFlag(r.meta_index, 0x02)) { d.setCursor(nx, y + 5); d.print("P"); nx += 8; }
  d.drawTextEllipsized(nx, y + 5, 175 - (nx - 45), shown);
  d.setColor(r.attention ? p.danger : p.sub); d.drawTextEllipsized(45, y + 19, 205, r.attention ? "Failed message - tap to retry" : r.preview);
  char age[12]; ageLabel(_rtc, r.timestamp, age, sizeof(age)); d.setColor(p.sub); d.drawTextRightAlign(302, y + 5, age);
  if (r.unread) { d.setColor(p.accent); d.fillCircle(291, y + 23, 7); char u[5]; snprintf(u, sizeof(u), "%u", r.unread); d.setColor(rgb565(255,255,255)); d.drawTextCentered(291, y + 20, u); }
}

void CommunicatorAppScreen::drawRepeaterRow(DisplayDriver& d, const Row& r, int y, int h, bool selected) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.card); d.fillRoundRect(7, y, 306, h, 6); if (selected) { d.setColor(p.stroke); d.drawRoundRect(7, y, 306, h, 6); }
  d.setColor(freshnessColor(_rtc, r.timestamp)); d.fillCircle(21, y + h / 2, 6); d.setTextSize(1); d.setColor(p.text); d.drawTextEllipsized(35, y + 5, 190, r.name); d.setColor(p.sub); d.drawTextEllipsized(35, y + 19, 205, r.preview); char age[12]; ageLabel(_rtc, r.timestamp, age, sizeof(age)); d.drawTextRightAlign(302, y + 5, age); d.setColor(p.sub); d.setCursor(296, y + 19); d.print(">");
}

void CommunicatorAppScreen::drawDetailTitle(DisplayDriver& d, const char* title, const char* subtitle) {
  drawAppHeader(d); drawBack(d, 45); CompactPalette p = paletteFor(_light_mode); d.setTextSize(1); d.setColor(p.text); d.drawTextEllipsized(47, 50, 170, title); if (subtitle && subtitle[0]) { d.setColor(p.sub); d.drawTextRightAlign(312, 62, subtitle); }
}

void CommunicatorAppScreen::drawButton(DisplayDriver& d, int x, int y, int w, int h, const char* label, bool primary, bool enabled) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(enabled ? (primary ? p.accent : p.card2) : p.card); d.fillRoundRect(x, y, w, h, 6); d.setColor(enabled ? (primary ? p.accent : p.divider) : p.divider); d.drawRoundRect(x, y, w, h, 6); d.setColor(enabled ? (primary ? rgb565(255,255,255) : p.text) : p.sub); d.setTextSize(1); d.drawTextCentered(x + w / 2, y + h / 2 - 4, label);
}

void CommunicatorAppScreen::drawInfoCard(DisplayDriver& d, int x, int y, int w, int h, const char* title, const char* value, const char* sub) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.card); d.fillRoundRect(x, y, w, h, 6); d.setTextSize(1); d.setColor(p.sub); d.drawTextEllipsized(x + 11, y + 7, w - 22, title); d.setColor(p.text); d.drawTextEllipsized(x + 11, y + 20, w - 22, value ? value : ""); if (sub && sub[0]) { d.setColor(p.sub); d.drawTextEllipsized(x + 11, y + 33, w - 22, sub); }
}

void CommunicatorAppScreen::drawWrapped(DisplayDriver& d, int x, int y, int max_width, int lines, const char* text) {
  if (!text || !text[0]) return;
  CompactPalette p = paletteFor(_light_mode); d.setTextSize(1); d.setColor(p.sub);
  int maxChars = max_width / 6; if (maxChars < 8) maxChars = 8;
  const char* cur = text;
  for (int line = 0; line < lines && *cur; ++line) {
    char buf[64]; int n = 0, lastSpace = -1;
    while (cur[n] && n < maxChars && n < (int)sizeof(buf) - 1) { if (cur[n] == ' ') lastSpace = n; ++n; }
    if (cur[n] && lastSpace > 4) n = lastSpace;
    memcpy(buf, cur, n); buf[n] = 0;
    while (n > 0 && buf[n-1] == ' ') buf[--n] = 0;
    d.drawTextEllipsized(x, y + line * 13, max_width, buf);
    cur += n; while (*cur == ' ') ++cur;
  }
}

void CommunicatorAppScreen::drawMain(DisplayDriver& d) {
  drawAppHeader(d); loadContactsAndChannels(); drawTabs(d); CompactPalette p = paletteFor(_light_mode);
  if (_tab == TAB_CHATS) {
    drawSearch(d); drawFilterChips(d); buildChatRows();
    if (!_row_count) { d.setTextSize(1); d.setColor(p.sub); const char* empty = _filter == FILTER_FAVORITES ? "No favorite conversations" : _filter == FILTER_UNREAD ? "No unread conversations" : _filter == FILTER_ATTENTION ? "Nothing needs attention" : "No conversations yet"; d.drawTextCentered(160, 154, empty); }
    else for (int i = 0; i < 2; ++i) { int idx = _list_offset + i; if (idx >= _row_count) break; drawConversationRow(d, _rows[idx], 129 + i * 37, 35, idx == _selected); }
    drawButton(d, 7, 207, 306, 29, "+  New conversation", true, true);
  } else {
    buildRepeaterRows(); drawButton(d, 7, 76, 306, 25, "Refresh repeaters", false, true); drawButton(d, 7, 104, 150, 25, "Map coverage", false, true); drawButton(d, 163, 104, 150, 25, "Network health", false, true);
    d.setColor(p.sub); d.setTextSize(1); d.setCursor(11, 137); d.print("Known repeaters"); d.setColor(p.card2); d.fillRoundRect(213, 133, 47, 19, 5); d.setColor(p.text); d.drawTextCentered(236, 139, "Recent"); d.setColor(p.bg); d.fillRoundRect(264, 133, 49, 19, 5); d.setColor(p.sub); d.drawTextCentered(288, 139, "Distance");
    if (!_row_count) { d.setColor(p.sub); d.drawTextCentered(160, 188, "No repeaters stored or heard"); }
    else for (int i = 0; i < 2; ++i) { int idx = _list_offset + i; if (idx >= _row_count) break; drawRepeaterRow(d, _rows[idx], 156 + i * 39, 37, idx == _selected); }
  }
}

void CommunicatorAppScreen::drawChatToolbar(DisplayDriver& d) {
  CompactPalette p = paletteFor(_light_mode); drawBack(d, 45); char shown[32]; StrHelper::strncpy(shown, _active_name, sizeof(shown));
  if (_active_kind == ROW_CONTACT) { int m = findMetaForContact(_active_contact, false); if (m >= 0 && _meta[m].alias[0]) StrHelper::strncpy(shown, _meta[m].alias, sizeof(shown)); }
  if (_active_kind == ROW_CHANNEL) { int m = findMetaForChannel(_active_channel, _active_channel_index, false); if (m >= 0 && _meta[m].alias[0]) StrHelper::strncpy(shown, _meta[m].alias, sizeof(shown)); }
  drawAvatar(d, 53, 59, shown, _active_kind == ROW_CHANNEL); d.setTextSize(1); d.setColor(p.text); d.drawTextEllipsized(72, 48, 137, shown);
  d.setColor(p.sub); if (_active_kind == ROW_CONTACT) { char age[18]; ageLabel(_rtc, _active_contact.last_advert_timestamp, age, sizeof(age)); char heard[28]; snprintf(heard, sizeof(heard), "Heard %s", age[0] ? age : "unknown"); d.drawTextEllipsized(72, 63, 137, heard); } else d.drawTextEllipsized(72, 63, 137, _active_channel_index == 0 ? "Public / World" : "Group chat");
  drawButton(d, 214, 45, 31, 29, "S", false, true); drawButton(d, 249, 45, 31, 29, "i", false, true); drawButton(d, 284, 45, 31, 29, "M", false, _active_kind == ROW_CONTACT);
}

void CommunicatorAppScreen::drawComposer(DisplayDriver& d) {
  CompactPalette p = paletteFor(_light_mode); d.setColor(p.bg); d.fillRect(0, 199, 320, 41); d.setColor(p.input); d.fillRoundRect(6, 203, 193, 33, 6); d.setColor(p.stroke); d.drawRoundRect(6, 203, 193, 33, 6); d.setTextSize(1); d.setColor(_compose_len ? p.text : p.sub); d.drawTextEllipsized(16, 215, 173, _compose_len ? _compose : "Message"); drawButton(d, 204, 203, 49, 33, "Voice", false, true); drawButton(d, 258, 203, 56, 33, "Send", true, true);
}

void CommunicatorAppScreen::drawChat(DisplayDriver& d) {
  drawAppHeader(d); drawChatToolbar(d); CompactPalette p = paletteFor(_light_mode); int top = 80;
  if (_chat_search_active) { d.setColor(p.input); d.fillRoundRect(7, 79, 306, 25, 6); d.setColor(p.accent); d.drawRoundRect(7, 79, 306, 25, 6); d.setColor(_chat_search[0] ? p.text : p.sub); d.drawTextEllipsized(17, 88, 285, _chat_search[0] ? _chat_search : "Search this conversation"); top = 108; }
  d.setColor(p.bg); d.fillRect(0, top, 320, 199 - top);
  int idx[4]; int maxn = _chat_search_active ? 2 : 3; int count = collectActiveMessages(idx, maxn);
  int y = top + 5;
  if (!count) { d.setTextSize(1); d.setColor(p.sub); d.drawTextCentered(160, top + 38, _chat_search_active && _chat_search[0] ? "No matching messages" : "No messages yet"); }
  for (int i = 0; i < count; ++i) {
    const MessageEntry& m = _messages[idx[i]];
    if (_chat_search_active && _chat_search[0] && !containsInsensitive(m.text, _chat_search)) continue;
    int textWidth = d.getTextWidth(m.text); if (textWidth > 205) textWidth = 205; int w = textWidth + 18; if (w < 72) w = 72; if (w > 226) w = 226; int x = m.outgoing ? 312 - w : 8;
    d.setColor(m.outgoing ? p.outgoing : p.incoming); d.fillRoundRect(x, y, w, 31, 6); d.setTextSize(1); d.setColor(m.outgoing && _light_mode ? rgb565(255,255,255) : p.text); d.drawTextEllipsized(x + 8, y + 6, w - 16, m.text); char age[12]; ageLabel(_rtc, m.timestamp, age, sizeof(age)); d.setColor(m.outgoing && _light_mode ? rgb565(225,235,250) : p.sub); d.drawTextRightAlign(x + w - 7, y + 19, age); if (m.outgoing) { d.setColor(m.send_state == SEND_FAILED ? p.danger : (m.outgoing && _light_mode ? rgb565(235,245,255) : p.sub)); d.setCursor(x + 8, y + 19); d.print(m.send_state == SEND_FAILED ? "Failed" : "Sent"); }
    y += 35;
  }
  drawComposer(d);
}

void CommunicatorAppScreen::drawNewConversation(DisplayDriver& d) {
  drawDetailTitle(d, "New conversation"); CompactPalette p = paletteFor(_light_mode); drawButton(d, 7, 80, 150, 27, "Add contact", false, true); drawButton(d, 163, 80, 150, 27, "Group chat", false, true); d.setTextSize(1); d.setColor(p.sub); d.setCursor(11, 114); d.print("People & groups"); buildNewConversationRows(); if (!_row_count) d.drawTextCentered(160, 158, "No contacts or groups configured"); else for (int i = 0; i < 3; ++i) { int idx = _list_offset + i; if (idx >= _row_count) break; drawConversationRow(d, _rows[idx], 125 + i * 36, 34, idx == _selected); }
}

void CommunicatorAppScreen::drawSettings(DisplayDriver& d) {
  drawDetailTitle(d, "Settings"); CompactPalette p = paletteFor(_light_mode); const char* titles[5] = {"Connection & radio", "Messaging", "Location & maps", "Appearance", "Data & backup"}; const char* subs[5] = {"On-device SX1262 radio", _show_public ? "Public / World visible" : "Public / World hidden", "Coverage and sharing", _light_mode ? "Light" : "Dark", "Local history / export"};
  for (int i = 0; i < 5; ++i) { int y = 80 + i * 30; d.setColor(p.card); d.fillRoundRect(7, y, 306, 27, 6); d.setColor(p.text); d.setTextSize(1); d.setCursor(16, y + 5); d.print(titles[i]); d.setColor(p.sub); d.drawTextRightAlign(302, y + 16, subs[i]); }
  d.setColor(p.sub); d.drawTextCentered(160, 232, "Advanced settings below Data & backup");
}

void CommunicatorAppScreen::drawMessagingSettings(DisplayDriver& d) {
  drawDetailTitle(d, "Messaging"); CompactPalette p = paletteFor(_light_mode); drawInfoCard(d, 7, 81, 306, 43, "Public / World visible", _show_public ? "On" : "Off", "Tap to change"); drawInfoCard(d, 7, 130, 306, 43, "Direct-message routing", "Automatic", "Stored directed route, then firmware fallback"); drawInfoCard(d, 7, 179, 306, 43, "Notifications", "On-device unread indicators", "Android notifications do not apply here");
}

void CommunicatorAppScreen::drawLocationSettings(DisplayDriver& d) {
  drawDetailTitle(d, "Location & maps"); CompactPalette p = paletteFor(_light_mode); NodePrefs* n = _task->getNodePrefs(); char loc[48]; if (validLatLon(n->node_lat, n->node_lon)) snprintf(loc, sizeof(loc), "%.4f, %.4f", n->node_lat, n->node_lon); else StrHelper::strncpy(loc, "No current position", sizeof(loc)); drawInfoCard(d, 7, 81, 306, 43, "Use location for coverage maps", loc, "Standard T-Deck uses external GPS/manual position"); drawInfoCard(d, 7, 130, 306, 43, "Include location in node adverts", n->advert_loc_policy == ADVERT_LOC_SHARE ? "On" : "Off", "Tap to change"); drawInfoCard(d, 7, 179, 306, 43, "Remote location requests", n->telemetry_mode_loc == TELEM_MODE_DENY ? "Denied" : "Allowed", "Tap to change");
}

void CommunicatorAppScreen::drawAppearanceSettings(DisplayDriver& d) {
  drawDetailTitle(d, "Appearance"); drawInfoCard(d, 7, 81, 306, 48, "Theme", _light_mode ? "Light" : "Dark", "Tap to switch"); drawInfoCard(d, 7, 136, 306, 48, "Communicator styling", "beta219 compact language", "Same hierarchy, adapted to 320 x 240");
}

void CommunicatorAppScreen::drawAdvancedSettings(DisplayDriver& d) {
  drawDetailTitle(d, "Advanced settings"); drawInfoCard(d, 7, 81, 306, 43, "Bluetooth companion", _task->isBluetoothEnabled() ? "Enabled" : "Disabled", "Tap to change"); drawInfoCard(d, 7, 130, 306, 43, "Touch input", _task->touchReady() ? "GT911 ready" : "GT911 failed", "Touch is primary navigation"); drawInfoCard(d, 7, 179, 306, 43, "Physical keyboard", "Enabled", "Primary text entry");
}

void CommunicatorAppScreen::drawRadio(DisplayDriver& d) {
  drawDetailTitle(d, "Radio Status"); CompactPalette p = paletteFor(_light_mode); NodePrefs* n = _task->getNodePrefs(); char val[48]; snprintf(val, sizeof(val), "%s", n->node_name[0] ? n->node_name : "T-Deck"); drawInfoCard(d, 7, 80, 306, 43, "Current radio", val, "On-device SX1262 - verified by firmware"); snprintf(val, sizeof(val), "%d%%  %u mV", (int)((_task->getBattMilliVolts() - 3300) * 100 / 900), _task->getBattMilliVolts()); drawInfoCard(d, 7, 129, 148, 43, "Battery", val); snprintf(val, sizeof(val), "%.3f MHz", n->freq); drawInfoCard(d, 162, 129, 151, 43, "LoRa", val); drawButton(d, 7, 178, 98, 28, "Self advert", true, true); drawButton(d, 111, 178, 98, 28, "Statistics", false, true); drawButton(d, 215, 178, 98, 28, "Device detail", false, true); d.setColor(p.sub); d.setTextSize(1); char radio[64]; snprintf(radio, sizeof(radio), "SF%u  BW %.1f  CR%u  TX %d dBm", n->sf, n->bw, n->cr, n->tx_power_dbm); d.drawTextCentered(160, 218, radio);
}

void CommunicatorAppScreen::drawRadioStats(DisplayDriver& d) {
  drawDetailTitle(d, "Radio statistics"); loadContactsAndChannels(); char v[40]; snprintf(v, sizeof(v), "%u", _contact_count); drawInfoCard(d, 7, 81, 148, 43, "People", v); snprintf(v, sizeof(v), "%u", _repeater_count); drawInfoCard(d, 162, 81, 151, 43, "Repeaters", v); snprintf(v, sizeof(v), "%u", _channel_count); drawInfoCard(d, 7, 130, 148, 43, "Channels", v); snprintf(v, sizeof(v), "%u / %d", _message_count, MESSAGE_CACHE); drawInfoCard(d, 162, 130, 151, 43, "UI message cache", v); drawInfoCard(d, 7, 179, 306, 43, "Packet / airtime counters", "Not exposed to this UI yet", "Can be bridged from the radio runtime without RF traffic");
}

void CommunicatorAppScreen::drawDeviceDetails(DisplayDriver& d) {
  drawDetailTitle(d, "Device details"); NodePrefs* n = _task->getNodePrefs(); char a[64], b[64]; snprintf(a, sizeof(a), "%s", n->node_name[0] ? n->node_name : "T-Deck"); drawInfoCard(d, 7, 81, 306, 38, "Advertised name", a); snprintf(a, sizeof(a), "%s", FIRMWARE_VERSION); drawInfoCard(d, 7, 124, 306, 38, "Firmware", a); snprintf(a, sizeof(a), "%.3f MHz / %.1f kHz", n->freq, n->bw); snprintf(b, sizeof(b), "SF%u CR%u TX %d dBm", n->sf, n->cr, n->tx_power_dbm); drawInfoCard(d, 7, 167, 306, 49, "Radio configuration", a, b);
}

void CommunicatorAppScreen::drawConversationDetails(DisplayDriver& d) {
  drawDetailTitle(d, _active_kind == ROW_CHANNEL ? "Group details" : "Conversation details"); CompactPalette p = paletteFor(_light_mode); char shown[32]; StrHelper::strncpy(shown, _active_name, sizeof(shown)); int meta = -1; if (_active_kind == ROW_CONTACT) meta = findMetaForContact(_active_contact, true); else if (_active_kind == ROW_CHANNEL) meta = findMetaForChannel(_active_channel, _active_channel_index, true); if (meta >= 0 && _meta[meta].alias[0]) StrHelper::strncpy(shown, _meta[meta].alias, sizeof(shown)); drawInfoCard(d, 7, 81, 306, 43, "Conversation", shown, _active_kind == ROW_CHANNEL ? "Group chat" : "Direct contact"); char state[64]; snprintf(state, sizeof(state), "%s  %s  %s", metaFlag(meta,0x01)?"Favorite":"Not favorite", metaFlag(meta,0x02)?"Pinned":"Unpinned", metaFlag(meta,0x04)?"Muted":"Alerts on"); drawInfoCard(d, 7, 130, 306, 43, "Organization", state, "Long-press chat row to change"); if (_active_kind == ROW_CONTACT) { char route[52]; if (_active_contact.out_path_len == OUT_PATH_UNKNOWN) StrHelper::strncpy(route, "Flood / no stored path", sizeof(route)); else snprintf(route, sizeof(route), "%u hop%s", _active_contact.out_path_len, _active_contact.out_path_len == 1 ? "" : "s"); drawInfoCard(d, 7, 179, 306, 43, "Radio observation", route, validLatLon(_active_contact.gps_lat / 1000000.0, _active_contact.gps_lon / 1000000.0) ? "Advertised location available" : "No advertised location"); } else { d.setColor(p.card); d.fillRoundRect(7, 179, 306, 43, 6); d.setColor(p.sub); d.setCursor(18, 189); d.print("Group administration"); d.setColor(p.text); d.setCursor(18, 204); d.print("Channel identity is stored on this T-Deck"); }
}

void CommunicatorAppScreen::drawRepeaterDetails(DisplayDriver& d) {
  drawDetailTitle(d, "Repeater details"); char age[20], route[48], loc[52]; ageLabel(_rtc, _info_contact.last_advert_timestamp, age, sizeof(age)); if (_info_contact.out_path_len == OUT_PATH_UNKNOWN) StrHelper::strncpy(route, "Flood / unknown", sizeof(route)); else snprintf(route, sizeof(route), "%u hop%s", _info_contact.out_path_len, _info_contact.out_path_len == 1 ? "" : "s"); double lat = _info_contact.gps_lat / 1000000.0, lon = _info_contact.gps_lon / 1000000.0; if (validLatLon(lat, lon)) snprintf(loc, sizeof(loc), "%.4f, %.4f", lat, lon); else StrHelper::strncpy(loc, "Unavailable", sizeof(loc)); drawInfoCard(d, 7, 81, 306, 43, _info_contact.name, age[0] ? age : "Not observed", "Last heard"); drawInfoCard(d, 7, 130, 306, 43, "Cached route", route); drawInfoCard(d, 7, 179, 205, 43, "Recorded location", loc); drawButton(d, 218, 179, 95, 43, "Map", false, validLatLon(lat, lon));
}

void CommunicatorAppScreen::drawRepeaterMap(DisplayDriver& d) {
  drawDetailTitle(d, "Map coverage", "Local"); CompactPalette p = paletteFor(_light_mode); loadContactsAndChannels(); NodePrefs* n = _task->getNodePrefs();
  double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180; int pts = 0;
  for (int i = 0; i < _repeater_count; ++i) { double lat = _repeaters[i].gps_lat / 1000000.0, lon = _repeaters[i].gps_lon / 1000000.0; if (!validLatLon(lat, lon)) continue; minLat = min(minLat, lat); maxLat = max(maxLat, lat); minLon = min(minLon, lon); maxLon = max(maxLon, lon); ++pts; }
  bool self = validLatLon(n->node_lat, n->node_lon); if (self) { minLat = min(minLat, n->node_lat); maxLat = max(maxLat, n->node_lat); minLon = min(minLon, n->node_lon); maxLon = max(maxLon, n->node_lon); }
  d.setColor(p.card); d.fillRoundRect(7, 80, 306, 132, 6); d.setColor(p.divider); d.drawRoundRect(14, 87, 292, 111, 4);
  if (!pts) { d.setColor(p.sub); d.setTextSize(1); d.drawTextCentered(160, 134, "No repeater coordinates available"); }
  else {
    double dlat = maxLat - minLat; if (dlat < 0.001) dlat = 0.001; double dlon = maxLon - minLon; if (dlon < 0.001) dlon = 0.001;
    for (int i = 0; i < _repeater_count; ++i) { double lat = _repeaters[i].gps_lat / 1000000.0, lon = _repeaters[i].gps_lon / 1000000.0; if (!validLatLon(lat, lon)) continue; int x = 22 + (int)(((lon - minLon) / dlon) * 276.0); int y = 94 + (int)(((maxLat - lat) / dlat) * 96.0); d.setColor(freshnessColor(_rtc, _repeaters[i].last_advert_timestamp)); d.fillCircle(x, y, 4); }
    if (self) { int x = 22 + (int)(((n->node_lon - minLon) / dlon) * 276.0); int y = 94 + (int)(((maxLat - n->node_lat) / dlat) * 96.0); d.setColor(SELF_BLUE); d.fillCircle(x, y, 5); }
  }
  d.setTextSize(1); d.setColor(MAP_GREEN_RECENT); d.setCursor(12, 219); d.print("<48h"); d.setColor(MAP_GREEN_MID); d.setCursor(62, 219); d.print("<7d"); d.setColor(MAP_GREEN_STALE); d.setCursor(102, 219); d.print("stale"); d.setColor(SELF_BLUE); d.setCursor(158, 219); d.print("this T-Deck"); d.setColor(p.sub); char count[40]; snprintf(count, sizeof(count), "%d mapped repeaters", pts); d.drawTextRightAlign(311, 219, count);
}

void CommunicatorAppScreen::drawNetworkHealth(DisplayDriver& d) {
  drawDetailTitle(d, "Network health", "Passive"); loadContactsAndChannels(); int direct = 0, one = 0, mid = 0, far = 0, unknown = 0; for (int i = 0; i < _repeater_count; ++i) { uint8_t h = _repeaters[i].out_path_len; if (h == OUT_PATH_UNKNOWN) ++unknown; else if (h == 0) ++direct; else if (h == 1) ++one; else if (h <= 3) ++mid; else ++far; } char v[32]; snprintf(v, sizeof(v), "%u repeaters", _repeater_count); drawInfoCard(d, 7, 81, 306, 38, "Observed network", v); snprintf(v, sizeof(v), "Direct %d   1-hop %d", direct, one); drawInfoCard(d, 7, 124, 306, 38, "Route distribution", v); snprintf(v, sizeof(v), "2-3 hops %d   4+ %d", mid, far); drawInfoCard(d, 7, 167, 306, 38, "Longer routes", v); snprintf(v, sizeof(v), "%d", unknown); drawInfoCard(d, 7, 210, 306, 26, "Flood / unknown", v);
}

void CommunicatorAppScreen::drawContactMap(DisplayDriver& d) {
  drawDetailTitle(d, "Contact location"); CompactPalette p = paletteFor(_light_mode); NodePrefs* n = _task->getNodePrefs(); double lat = _active_contact.gps_lat / 1000000.0, lon = _active_contact.gps_lon / 1000000.0; bool contact = validLatLon(lat, lon); bool self = validLatLon(n->node_lat, n->node_lon); d.setColor(p.card); d.fillRoundRect(7, 80, 306, 132, 6); d.setColor(p.divider); d.drawRoundRect(14, 87, 292, 111, 4); if (!contact) { d.setColor(p.sub); d.drawTextCentered(160, 133, "No location available for this contact"); } else if (!self) { d.setColor(p.accent); d.fillCircle(160, 132, 6); char loc[52]; snprintf(loc, sizeof(loc), "%.5f, %.5f", lat, lon); d.setColor(p.text); d.drawTextCentered(160, 151, loc); d.setColor(p.sub); d.drawTextCentered(160, 166, "No local position - contact centered"); } else { double minLat = min(lat, n->node_lat), maxLat = max(lat, n->node_lat), minLon = min(lon, n->node_lon), maxLon = max(lon, n->node_lon); double dlat = maxLat - minLat; if (dlat < .001) dlat = .001; double dlon = maxLon - minLon; if (dlon < .001) dlon = .001; int cx = 32 + (int)(((lon - minLon) / dlon) * 256.0), cy = 100 + (int)(((maxLat - lat) / dlat) * 80.0); int sx = 32 + (int)(((n->node_lon - minLon) / dlon) * 256.0), sy = 100 + (int)(((maxLat - n->node_lat) / dlat) * 80.0); d.setColor(p.accent); d.fillCircle(cx, cy, 6); d.setColor(SELF_BLUE); d.fillCircle(sx, sy, 5); d.setColor(p.sub); d.drawLine(cx, cy, sx, sy); }
  d.setColor(p.sub); d.setTextSize(1); d.setCursor(12, 220); d.print("Blue: this T-Deck   Accent: contact");
}

void CommunicatorAppScreen::drawQuickMenu(DisplayDriver& d) {
  drawAppHeader(d); CompactPalette p = paletteFor(_light_mode); d.setColor(p.card2); d.fillRoundRect(44, 44, 232, 190, 8); d.setColor(p.divider); d.drawRoundRect(44, 44, 232, 190, 8); d.setTextSize(1); d.setColor(p.text); d.drawTextEllipsized(56, 52, 208, _active_name);
  int meta = -1; if (_active_kind == ROW_CONTACT) meta = findMetaForContact(_active_contact, true); else if (_active_kind == ROW_CHANNEL) meta = findMetaForChannel(_active_channel, _active_channel_index, true);
  const char* labels[7] = {metaFlag(meta,0x01)?"Unfavorite":"Favorite", metaFlag(meta,0x02)?"Unpin":"Pin", metaFlag(meta,0x04)?"Unmute":"Mute", "Mark read / unread", _active_kind==ROW_CHANNEL?"Rename group":"Rename contact", metaFlag(meta,0x08)?"Unarchive":"Archive", "Delete local history"};
  for (int i = 0; i < 7; ++i) { int y = 69 + i * 23; if (i == _quick_selected) { d.setColor(p.card); d.fillRoundRect(50, y - 3, 220, 22, 4); } d.setColor(i == 6 ? p.danger : p.text); d.setCursor(58, y + 3); d.print(labels[i]); }
}

void CommunicatorAppScreen::drawAliasEdit(DisplayDriver& d) {
  drawDetailTitle(d, _active_kind == ROW_CHANNEL ? "Rename group" : "Contact nickname"); CompactPalette p = paletteFor(_light_mode); d.setColor(p.input); d.fillRoundRect(10, 91, 300, 36, 6); d.setColor(p.accent); d.drawRoundRect(10, 91, 300, 36, 6); d.setColor(p.text); d.drawTextEllipsized(20, 105, 280, _edit); d.setColor(p.sub); d.setTextSize(1); d.drawTextCentered(160, 145, "Type a local display name with the keyboard"); d.drawTextCentered(160, 160, "Enter saves - Esc cancels"); if (_active_kind == ROW_CHANNEL) d.drawTextCentered(160, 183, "This does not change the configured channel name");
}

void CommunicatorAppScreen::drawFeatureNote(DisplayDriver& d) {
  drawDetailTitle(d, _feature_title); CompactPalette p = paletteFor(_light_mode); d.setColor(p.card); d.fillRoundRect(8, 82, 304, 130, 7); drawWrapped(d, 19, 95, 282, 7, _feature_body); drawButton(d, 85, 218, 150, 18, "Back", false, true);
}

int CommunicatorAppScreen::render(DisplayDriver& d) {
  if (_dirty == DIRTY_NONE) return 3600000;
  if (_dirty == DIRTY_COMPOSER && _route == ROUTE_CHAT) { drawComposer(d); _dirty = DIRTY_NONE; return 3600000; }
  fillScreen(d);
  switch (_route) {
    case ROUTE_MAIN: drawMain(d); break;
    case ROUTE_CHAT: drawChat(d); break;
    case ROUTE_NEW_CONVERSATION: drawNewConversation(d); break;
    case ROUTE_SETTINGS: drawSettings(d); break;
    case ROUTE_SETTINGS_MESSAGING: drawMessagingSettings(d); break;
    case ROUTE_SETTINGS_LOCATION: drawLocationSettings(d); break;
    case ROUTE_SETTINGS_APPEARANCE: drawAppearanceSettings(d); break;
    case ROUTE_SETTINGS_ADVANCED: drawAdvancedSettings(d); break;
    case ROUTE_RADIO: drawRadio(d); break;
    case ROUTE_RADIO_STATS: drawRadioStats(d); break;
    case ROUTE_DEVICE_DETAILS: drawDeviceDetails(d); break;
    case ROUTE_CONVERSATION_DETAILS: drawConversationDetails(d); break;
    case ROUTE_REPEATER_DETAILS: drawRepeaterDetails(d); break;
    case ROUTE_REPEATER_MAP: drawRepeaterMap(d); break;
    case ROUTE_NETWORK_HEALTH: drawNetworkHealth(d); break;
    case ROUTE_CONTACT_MAP: drawContactMap(d); break;
    case ROUTE_QUICK_MENU: drawQuickMenu(d); break;
    case ROUTE_ALIAS_EDIT: drawAliasEdit(d); break;
    case ROUTE_FEATURE_NOTE: drawFeatureNote(d); break;
  }
  _dirty = DIRTY_NONE;
  return 3600000;
}

bool CommunicatorAppScreen::handleInput(char c) {
  if (_route == ROUTE_ALIAS_EDIT) {
    if (c == KEY_CANCEL) { goBack(); return true; }
    if (c == KEY_ENTER) { commitAliasEdit(); return true; }
    if (c == 8 || (uint8_t)c == 127) { if (_edit_len) _edit[--_edit_len] = 0; _dirty = DIRTY_ALL; return true; }
    if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && _edit_len < sizeof(_edit) - 1) { _edit[_edit_len++] = c; _edit[_edit_len] = 0; _dirty = DIRTY_ALL; return true; }
    return false;
  }
  if (_route == ROUTE_CHAT) {
    if (_chat_search_active) {
      if (c == KEY_CANCEL || c == KEY_ENTER) { _chat_search_active = false; _dirty = DIRTY_ALL; return true; }
      if (c == 8 || (uint8_t)c == 127) { if (_chat_search_len) _chat_search[--_chat_search_len] = 0; _dirty = DIRTY_ALL; return true; }
      if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && _chat_search_len < sizeof(_chat_search)-1) { _chat_search[_chat_search_len++] = c; _chat_search[_chat_search_len] = 0; _dirty = DIRTY_ALL; return true; }
      return false;
    }
    if (c == KEY_CANCEL) { goBack(); return true; }
    if (c == KEY_ENTER) { sendCompose(); return true; }
    if (c == 8 || (uint8_t)c == 127) { if (_compose_len) _compose[--_compose_len] = 0; _dirty = DIRTY_COMPOSER; return true; }
    if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && _compose_len < MAX_TEXT_LEN) { _compose[_compose_len++] = c; _compose[_compose_len] = 0; _dirty = DIRTY_COMPOSER; return true; }
    if (c == KEY_UP) { ++_message_scroll; if (_message_scroll > MESSAGE_CACHE - 1) _message_scroll = MESSAGE_CACHE - 1; _dirty = DIRTY_ALL; return true; }
    if (c == KEY_DOWN) { if (_message_scroll) --_message_scroll; _dirty = DIRTY_ALL; return true; }
    return false;
  }
  if (_route == ROUTE_MAIN && _tab == TAB_CHATS && _search_active) {
    if (c == KEY_CANCEL || c == KEY_ENTER) { _search_active = false; _dirty = DIRTY_ALL; return true; }
    if (c == 8 || (uint8_t)c == 127) { if (_search_len) _search[--_search_len] = 0; _list_offset = 0; _dirty = DIRTY_ALL; return true; }
    if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && _search_len < sizeof(_search)-1) { _search[_search_len++] = c; _search[_search_len] = 0; _list_offset = 0; _dirty = DIRTY_ALL; return true; }
  }
  if (c == KEY_CANCEL) { if (_route == ROUTE_MAIN) { _search_len = 0; _search[0] = 0; _filter = FILTER_ALL; _dirty = DIRTY_ALL; } else goBack(); return true; }
  if (_route == ROUTE_MAIN) {
    if (c == KEY_LEFT || c == KEY_RIGHT) { _tab = _tab == TAB_CHATS ? TAB_REPEATERS : TAB_CHATS; _selected = _list_offset = 0; _dirty = DIRTY_ALL; return true; }
    if (c == KEY_UP) { if (_selected > 0) --_selected; if (_selected < _list_offset) _list_offset = _selected; _dirty = DIRTY_ALL; return true; }
    if (c == KEY_DOWN) { buildChatRows(); if (_tab == TAB_REPEATERS) buildRepeaterRows(); if (_selected + 1 < _row_count) ++_selected; if (_selected >= _list_offset + 2) ++_list_offset; _dirty = DIRTY_ALL; return true; }
    if (c == KEY_ENTER) { if (_tab == TAB_CHATS) buildChatRows(); else buildRepeaterRows(); if (_selected >= 0 && _selected < _row_count) { if (_tab == TAB_CHATS) openRow(_rows[_selected]); else { _info_contact = _rows[_selected].contact; pushRoute(ROUTE_REPEATER_DETAILS); } } return true; }
  }
  return false;
}

bool CommunicatorAppScreen::touchMain(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture == COMPACT_TOUCH_SWIPE_UP) { ++_list_offset; clampOffset(2); _dirty = DIRTY_ALL; return true; }
  if (gesture == COMPACT_TOUCH_SWIPE_DOWN) { --_list_offset; clampOffset(2); _dirty = DIRTY_ALL; return true; }
  if (gesture != COMPACT_TOUCH_TAP && gesture != COMPACT_TOUCH_LONG_PRESS) return false;
  if (y >= 43 && y <= 72) { _tab = x < 160 ? TAB_CHATS : TAB_REPEATERS; _selected = _list_offset = 0; _dirty = DIRTY_ALL; return true; }
  if (_tab == TAB_CHATS) {
    if (y >= 74 && y <= 101 && gesture == COMPACT_TOUCH_TAP) { _search_active = true; _dirty = DIRTY_ALL; return true; }
    if (y >= 101 && y <= 126 && gesture == COMPACT_TOUCH_TAP) { int idx = (x - 6) / 77; if (idx < 0) idx = 0; if (idx > 3) idx = 3; _filter = (ChatFilter)idx; _list_offset = 0; _dirty = DIRTY_ALL; return true; }
    if (y >= 127 && y < 201) {
      buildChatRows(); int pos = _list_offset + (y - 127) / 37; if (pos >= 0 && pos < _row_count) {
        if (gesture == COMPACT_TOUCH_LONG_PRESS) {
          const Row& r = _rows[pos]; _active_kind = r.kind; StrHelper::strncpy(_active_name, r.name, sizeof(_active_name)); if (r.kind == ROW_CONTACT) _active_contact = r.contact; if (r.kind == ROW_CHANNEL) { _active_channel = r.channel; _active_channel_index = r.channel_index; } _quick_selected = 0; pushRoute(ROUTE_QUICK_MENU);
        } else openRow(_rows[pos]);
        return true;
      }
    }
    if (y >= 204 && gesture == COMPACT_TOUCH_TAP) { pushRoute(ROUTE_NEW_CONVERSATION); return true; }
  } else {
    if (y >= 74 && y <= 102 && gesture == COMPACT_TOUCH_TAP) { buildRepeaterRows(); _task->showAlert("Repeater list refreshed", 800); _dirty = DIRTY_ALL; return true; }
    if (y >= 103 && y <= 131 && gesture == COMPACT_TOUCH_TAP) { if (x < 160) pushRoute(ROUTE_REPEATER_MAP); else pushRoute(ROUTE_NETWORK_HEALTH); return true; }
    if (y >= 154) { buildRepeaterRows(); int pos = _list_offset + (y - 154) / 39; if (pos >= 0 && pos < _row_count && gesture == COMPACT_TOUCH_TAP) { _info_contact = _rows[pos].contact; pushRoute(ROUTE_REPEATER_DETAILS); return true; } }
  }
  return false;
}

bool CommunicatorAppScreen::touchChat(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture == COMPACT_TOUCH_SWIPE_UP) { ++_message_scroll; _dirty = DIRTY_ALL; return true; }
  if (gesture == COMPACT_TOUCH_SWIPE_DOWN) { if (_message_scroll) --_message_scroll; _dirty = DIRTY_ALL; return true; }
  if (gesture != COMPACT_TOUCH_TAP && gesture != COMPACT_TOUCH_LONG_PRESS) return false;
  if (y >= 44 && y <= 77) {
    if (x < 40) { goBack(); return true; }
    if (x >= 210 && x < 248) { _chat_search_active = !_chat_search_active; _dirty = DIRTY_ALL; return true; }
    if (x >= 248 && x < 283) { pushRoute(ROUTE_CONVERSATION_DETAILS); return true; }
    if (x >= 283 && _active_kind == ROW_CONTACT) { pushRoute(ROUTE_CONTACT_MAP); return true; }
  }
  if (y >= 200) {
    if (x >= 204 && x < 257) { openFeature("Voice messages", "The standard T-Deck has an ES7210 microphone and I2S speaker path, so voice is implementable. This build has not yet ported Communicator's Opus codec and MCC voice transport; the control is retained in the same composer position until that transport is added."); return true; }
    if (x >= 257) { sendCompose(); return true; }
  }
  if (gesture == COMPACT_TOUCH_LONG_PRESS && y >= 80 && y < 199) { openFeature("Message actions", "Reply, copy, reception details, send details, retry, and local delete depend on persistent per-message IDs and delivery metadata. The T-Deck UI will expose the same compact menu after the local history/ACK store is added."); return true; }
  return false;
}

bool CommunicatorAppScreen::touchNewConversation(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture == COMPACT_TOUCH_SWIPE_UP) { ++_list_offset; clampOffset(3); _dirty = DIRTY_ALL; return true; }
  if (gesture == COMPACT_TOUCH_SWIPE_DOWN) { --_list_offset; clampOffset(3); _dirty = DIRTY_ALL; return true; }
  if (gesture != COMPACT_TOUCH_TAP) return false;
  if (y >= 79 && y <= 109) {
    if (x < 160) openFeature("Add contact", "The T-Deck has no camera, so it cannot reproduce the Android QR scanner. The equivalent standalone flow will accept a MeshCore contact link or 64-hex public key from the physical keyboard, SD card, or BLE import; outbound contact QR display can still be implemented on the screen.");
    else openFeature("Group chat", "Configured MeshCore channels already appear below and can be used normally. Create/join/edit needs the same verified key encoding and read-back transaction used by the Android app; that transaction is not yet exposed through this on-device UI.");
    return true;
  }
  if (y >= 122) { buildNewConversationRows(); int pos = _list_offset + (y - 122) / 36; if (pos >= 0 && pos < _row_count) { openRow(_rows[pos]); return true; } }
  return false;
}

bool CommunicatorAppScreen::touchSettings(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture != COMPACT_TOUCH_TAP) return false;
  if (y >= 44 && y <= 78 && x < 42) { goBack(); return true; }
  if (_route == ROUTE_SETTINGS) {
    int row = (y - 78) / 30;
    if (row == 0) pushRoute(ROUTE_RADIO);
    else if (row == 1) pushRoute(ROUTE_SETTINGS_MESSAGING);
    else if (row == 2) pushRoute(ROUTE_SETTINGS_LOCATION);
    else if (row == 3) pushRoute(ROUTE_SETTINGS_APPEARANCE);
    else if (row == 4) openFeature("Data & backup", "Android's encrypted Room database and Keystore are not present on ESP32-S3. Persistent history can be stored on microSD/SPIFFS with AES-GCM, but it cannot honestly claim Android-Keystore-equivalent protection unless flash-encryption/eFuse security is deliberately provisioned. The proposed T-Deck version keeps export/import on SD and labels the protection level accurately.");
    else if (y >= 224) pushRoute(ROUTE_SETTINGS_ADVANCED);
    return true;
  }
  if (_route == ROUTE_SETTINGS_MESSAGING && y >= 80 && y <= 126) { _show_public = !_show_public; savePrefs(); _dirty = DIRTY_ALL; return true; }
  if (_route == ROUTE_SETTINGS_LOCATION) {
    NodePrefs* n = _task->getNodePrefs();
    if (y >= 128 && y <= 176) { n->advert_loc_policy = n->advert_loc_policy == ADVERT_LOC_SHARE ? ADVERT_LOC_NONE : ADVERT_LOC_SHARE; the_mesh.savePrefs(); _dirty = DIRTY_ALL; return true; }
    if (y >= 177 && y <= 224) { n->telemetry_mode_loc = n->telemetry_mode_loc == TELEM_MODE_DENY ? TELEM_MODE_ALLOW_ALL : TELEM_MODE_DENY; the_mesh.savePrefs(); _dirty = DIRTY_ALL; return true; }
  }
  if (_route == ROUTE_SETTINGS_APPEARANCE && y >= 80 && y <= 132) { _light_mode = !_light_mode; savePrefs(); _dirty = DIRTY_ALL; return true; }
  if (_route == ROUTE_SETTINGS_ADVANCED && y >= 80 && y <= 128) { if (_task->isBluetoothEnabled()) _task->disableBluetooth(); else _task->enableBluetooth(); _dirty = DIRTY_ALL; return true; }
  return false;
}

bool CommunicatorAppScreen::touchRadio(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture != COMPACT_TOUCH_TAP) return false;
  if (y >= 44 && y <= 78 && x < 42) { goBack(); return true; }
  if (_route == ROUTE_RADIO && y >= 176 && y <= 210) {
    if (x < 107) { bool ok = the_mesh.advert(); _task->showAlert(ok ? "Self advert queued" : "Advert failed", 900); }
    else if (x < 213) pushRoute(ROUTE_RADIO_STATS);
    else pushRoute(ROUTE_DEVICE_DETAILS);
    return true;
  }
  return false;
}

bool CommunicatorAppScreen::touchDetail(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture != COMPACT_TOUCH_TAP) return false;
  if (y >= 44 && y <= 78 && x < 42) { goBack(); return true; }
  if (_route == ROUTE_REPEATER_DETAILS && y >= 176 && x >= 212) { _active_contact = _info_contact; _active_kind = ROW_CONTACT; pushRoute(ROUTE_CONTACT_MAP); return true; }
  if (_route == ROUTE_FEATURE_NOTE && y >= 214) { goBack(); return true; }
  return false;
}

bool CommunicatorAppScreen::touchQuickMenu(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture != COMPACT_TOUCH_TAP) return false;
  if (x < 44 || x > 276 || y < 44 || y > 235) { goBack(); return true; }
  int action = (y - 66) / 23; if (action < 0 || action > 6) return true;
  int meta = -1; if (_active_kind == ROW_CONTACT) meta = findMetaForContact(_active_contact, true); else if (_active_kind == ROW_CHANNEL) meta = findMetaForChannel(_active_channel, _active_channel_index, true);
  if (action == 0) toggleMetaFlag(meta, 0x01);
  else if (action == 1) toggleMetaFlag(meta, 0x02);
  else if (action == 2) toggleMetaFlag(meta, 0x04);
  else if (action == 3) { bool anyUnread = false; for (int i = 0; i < MESSAGE_CACHE; ++i) if (strcmp(_messages[i].origin, _active_name) == 0 && _messages[i].unread) anyUnread = true; for (int i = 0; i < MESSAGE_CACHE; ++i) if (strcmp(_messages[i].origin, _active_name) == 0) _messages[i].unread = anyUnread ? 0 : 1; }
  else if (action == 4) { beginAliasEdit(); return true; }
  else if (action == 5) toggleMetaFlag(meta, 0x08);
  else if (action == 6) { deleteLocalMessagesFor(_active_name); _task->showAlert("Local history cleared", 900); }
  goBack(); return true;
}

bool CommunicatorAppScreen::handleTouch(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture == COMPACT_TOUCH_TAP && y <= 42) {
    if (x >= 234 && x < 277) { pushRoute(ROUTE_RADIO); return true; }
    if (x >= 277) { pushRoute(ROUTE_SETTINGS); return true; }
  }
  switch (_route) {
    case ROUTE_MAIN: return touchMain(x,y,gesture);
    case ROUTE_CHAT: return touchChat(x,y,gesture);
    case ROUTE_NEW_CONVERSATION: return touchNewConversation(x,y,gesture);
    case ROUTE_SETTINGS: case ROUTE_SETTINGS_MESSAGING: case ROUTE_SETTINGS_LOCATION: case ROUTE_SETTINGS_APPEARANCE: case ROUTE_SETTINGS_ADVANCED: return touchSettings(x,y,gesture);
    case ROUTE_RADIO: case ROUTE_RADIO_STATS: case ROUTE_DEVICE_DETAILS: return touchRadio(x,y,gesture) || touchDetail(x,y,gesture);
    case ROUTE_QUICK_MENU: return touchQuickMenu(x,y,gesture);
    case ROUTE_ALIAS_EDIT: return false;
    default: return touchDetail(x,y,gesture);
  }
}
