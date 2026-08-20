#include "CommunicatorAppScreen.h"
#include <string.h>

namespace {

struct UnreadNavigationState {
  CommunicatorAppScreen* owner;
  int boundary_slot;
  uint8_t count;
  char origin[32];
};

UnreadNavigationState g_unread_nav = {nullptr, -1, 0, {0}};

constexpr ColorVal unreadRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void resetUnreadNav(CommunicatorAppScreen* owner, const char* origin = nullptr) {
  g_unread_nav.owner = owner;
  g_unread_nav.boundary_slot = -1;
  g_unread_nav.count = 0;
  g_unread_nav.origin[0] = 0;
  if (origin) {
    strncpy(g_unread_nav.origin, origin, sizeof(g_unread_nav.origin) - 1);
    g_unread_nav.origin[sizeof(g_unread_nav.origin) - 1] = 0;
  }
}

} // namespace

void CommunicatorAppScreen::prepareUnreadNavigationForTouch(int16_t x, int16_t y, uint8_t gesture) {
  (void)x;
  if (gesture != COMPACT_TOUCH_TAP || _route != ROUTE_MAIN || _tab != TAB_CHATS || y < 127 || y >= 202) return;

  buildChatRows();
  int pos = _list_offset + (y - 127) / 37;
  if (pos < 0 || pos >= _row_count) return;

  const char* name = _rows[pos].name;
  resetUnreadNav(this, name);

  // Walk oldest -> newest so boundary_slot becomes the first unread message in
  // chronological order. openRow() clears unread immediately after this hook.
  for (int n = (int)_message_count - 1; n >= 0; --n) {
    int idx = (_message_head + MESSAGE_CACHE - n) % MESSAGE_CACHE;
    const MessageEntry& m = _messages[idx];
    if (!m.origin[0] || strcmp(m.origin, name) != 0 || !m.unread) continue;
    if (g_unread_nav.boundary_slot < 0) g_unread_nav.boundary_slot = idx;
    if (g_unread_nav.count < 255) ++g_unread_nav.count;
  }
}

void CommunicatorAppScreen::noteNewMessageForUnreadNavigation(const char* from_name) {
  if (!from_name || !from_name[0] || _route != ROUTE_CHAT || strcmp(from_name, _active_name) != 0) return;

  if (g_unread_nav.owner != this || strcmp(g_unread_nav.origin, _active_name) != 0) {
    resetUnreadNav(this, _active_name);
  }
  if (g_unread_nav.boundary_slot < 0) g_unread_nav.boundary_slot = _message_head;
  if (g_unread_nav.count < 255) ++g_unread_nav.count;
  _dirty = DIRTY_ALL;
}

bool CommunicatorAppScreen::handleNewestTouch(int16_t x, int16_t y, uint8_t gesture) {
  if (_route != ROUTE_CHAT || _message_scroll <= 0 || replyPending() || gesture != COMPACT_TOUCH_TAP) return false;
  if (x < 222 || y < 184 || y >= 200) return false;
  _message_scroll = 0;
  _dirty = DIRTY_ALL;
  return true;
}

bool CommunicatorAppScreen::handleNewestInput(char c) {
  // UITask calls this before the legacy screen handler. Capture the keyboard /
  // trackball Enter path here, return false, then handleInput() opens the row
  // and clears unread exactly as it did before. Search Enter is excluded.
  if (c == KEY_ENTER && _route == ROUTE_MAIN && _tab == TAB_CHATS && !_search_active) {
    buildChatRows();
    if (_selected >= 0 && _selected < _row_count) {
      const char* name = _rows[_selected].name;
      resetUnreadNav(this, name);
      for (int n = (int)_message_count - 1; n >= 0; --n) {
        int idx = (_message_head + MESSAGE_CACHE - n) % MESSAGE_CACHE;
        const MessageEntry& m = _messages[idx];
        if (!m.origin[0] || strcmp(m.origin, name) != 0 || !m.unread) continue;
        if (g_unread_nav.boundary_slot < 0) g_unread_nav.boundary_slot = idx;
        if (g_unread_nav.count < 255) ++g_unread_nav.count;
      }
    }
    return false;
  }

  if (_route != ROUTE_CHAT || _message_scroll <= 0 || c != KEY_RIGHT) return false;
  _message_scroll = 0;
  _dirty = DIRTY_ALL;
  return true;
}

void CommunicatorAppScreen::drawNewMessagesOverlay(DisplayDriver& d) {
  if (_route != ROUTE_CHAT) return;

  const bool state_matches = g_unread_nav.owner == this &&
                             g_unread_nav.origin[0] &&
                             strcmp(g_unread_nav.origin, _active_name) == 0;
  if (state_matches && g_unread_nav.boundary_slot >= 0) {
    const MessageEntry& boundary = _messages[g_unread_nav.boundary_slot];
    if (!boundary.origin[0] || strcmp(boundary.origin, _active_name) != 0) {
      // The bounded ring reused the remembered slot. Drop the ephemeral divider
      // rather than attaching "New messages" to unrelated content.
      resetUnreadNav(this, _active_name);
    }
  }

  int top = _chat_search_active ? 108 : 80;
  int indexes[4];
  int maxn = _chat_search_active ? 2 : 3;
  int count = collectActiveMessages(indexes, maxn);
  bool boundary_visible = false;

  const ColorVal accent = _light_mode ? unreadRgb565(11,58,117) : unreadRgb565(43,112,194);
  const ColorVal badge = _light_mode ? unreadRgb565(232,240,249) : unreadRgb565(24,49,78);
  const ColorVal text = _light_mode ? unreadRgb565(20,50,82) : unreadRgb565(232,242,252);
  const ColorVal stroke = _light_mode ? unreadRgb565(150,180,211) : unreadRgb565(61,101,142);

  if (g_unread_nav.owner == this && strcmp(g_unread_nav.origin, _active_name) == 0 &&
      g_unread_nav.boundary_slot >= 0 && g_unread_nav.count) {
    for (int i = 0; i < count; ++i) {
      if (indexes[i] != g_unread_nav.boundary_slot) continue;
      boundary_visible = true;
      int bubble_y = top + 5 + i * 35;
      int line_y = bubble_y - 2;
      d.setColor(accent);
      d.fillRect(10, line_y, 300, 1);
      d.setColor(badge);
      d.fillRoundRect(111, bubble_y - 7, 98, 12, 5);
      d.setColor(stroke);
      d.drawRoundRect(111, bubble_y - 7, 98, 12, 5);
      d.setTextSize(1);
      d.setColor(text);
      d.drawTextCentered(160, bubble_y - 4, g_unread_nav.count == 1 ? "New message" : "New messages");
      break;
    }
  }

  // The legacy renderer leaves a narrow strip above the composer. Use it for
  // navigation rather than covering message text. Reply owns this strip while
  // its local-reference chip is present, so suppress these pills in that case.
  if (!replyPending()) {
    const int pill_y = 187;
    if (g_unread_nav.owner == this && strcmp(g_unread_nav.origin, _active_name) == 0 &&
        g_unread_nav.count && !boundary_visible) {
      char label[20];
      snprintf(label, sizeof(label), "%u new", (unsigned)g_unread_nav.count);
      d.setColor(badge); d.fillRoundRect(8, pill_y, 58, 11, 4);
      d.setColor(stroke); d.drawRoundRect(8, pill_y, 58, 11, 4);
      d.setColor(text); d.setTextSize(1); d.drawTextCentered(37, pill_y + 2, label);
    }
    if (_message_scroll > 0) {
      d.setColor(badge); d.fillRoundRect(222, pill_y, 91, 11, 4);
      d.setColor(stroke); d.drawRoundRect(222, pill_y, 91, 11, 4);
      d.setColor(text); d.setTextSize(1); d.drawTextCentered(267, pill_y + 2, "Newest >");
    }
  }
}
