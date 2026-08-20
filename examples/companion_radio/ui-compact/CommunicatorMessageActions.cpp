#include "CommunicatorAppScreen.h"
#include "UITask.h"
#include "../MyMesh.h"
#include <string.h>

namespace {

struct MessageActionState {
  CommunicatorAppScreen* owner;
  int slot;
  uint8_t page;      // 0 menu, 1 details, 2 delete confirmation
  uint8_t selected;
};

MessageActionState g_message_action = {nullptr, -1, 0, 0};

constexpr ColorVal actionRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

} // namespace

bool CommunicatorAppScreen::messageActionActive() const {
  return g_message_action.owner == this && g_message_action.slot >= 0 && g_message_action.slot < MESSAGE_CACHE;
}

bool CommunicatorAppScreen::tryOpenMessageActions(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture != COMPACT_TOUCH_LONG_PRESS || _route != ROUTE_CHAT || y < 80 || y >= 199) return false;

  int top = _chat_search_active ? 108 : 80;
  int indexes[4];
  int maxn = _chat_search_active ? 2 : 3;
  int count = collectActiveMessages(indexes, maxn);
  int bubble_y = top + 5;

  for (int i = 0; i < count; ++i, bubble_y += 35) {
    MessageEntry& m = _messages[indexes[i]];
    if (y < bubble_y || y >= bubble_y + 31) continue;

    // Match the existing bubble width closely enough that a long-press on the
    // blank chat background does not accidentally select a message.
    int tw = (int)strlen(m.text) * 6;
    if (tw > 205) tw = 205;
    int w = tw + 18;
    if (w < 72) w = 72;
    if (w > 226) w = 226;
    int bx = m.outgoing ? 312 - w : 8;
    if (x < bx || x >= bx + w) continue;

    g_message_action.owner = this;
    g_message_action.slot = indexes[i];
    g_message_action.page = 0;
    g_message_action.selected = 0;
    _dirty = DIRTY_ALL;
    return true;
  }
  return false;
}

bool CommunicatorAppScreen::handleMessageActionTouch(int16_t x, int16_t y, uint8_t gesture) {
  if (!messageActionActive()) return false;
  if (gesture != COMPACT_TOUCH_TAP) return true;

  auto closeModal = [&]() {
    g_message_action.owner = nullptr;
    g_message_action.slot = -1;
    g_message_action.page = 0;
    g_message_action.selected = 0;
    _dirty = DIRTY_ALL;
  };

  if (g_message_action.slot < 0 || g_message_action.slot >= MESSAGE_CACHE ||
      (!_messages[g_message_action.slot].origin[0] && !_messages[g_message_action.slot].text[0])) {
    closeModal();
    return true;
  }

  MessageEntry& m = _messages[g_message_action.slot];

  if (g_message_action.page == 1) {
    if (y >= 194 || x < 24 || x > 296) {
      g_message_action.page = 0;
      g_message_action.selected = 0;
      _dirty = DIRTY_ALL;
    }
    return true;
  }

  if (g_message_action.page == 2) {
    if (y >= 176 && y <= 218) {
      if (x < 160) {
        g_message_action.page = 0;
        g_message_action.selected = 0;
        _dirty = DIRTY_ALL;
        return true;
      }

      if (m.delivery_ack) the_mesh.releaseCompactAck(m.delivery_ack);
      memset(&m, 0, sizeof(m));
      persistenceCheckpoint(true); // appends the per-message tombstone now
      closeModal();
      _task->showAlert("Message deleted locally", 900);
      return true;
    }
    return true;
  }

  if (x < 24 || x > 296 || y < 48 || y > 232) {
    closeModal();
    return true;
  }

  int action = -1;
  if (y >= 88 && y < 113) action = 0;
  else if (y >= 115 && y < 140) action = 1;
  else if (y >= 142 && y < 167) action = 2;
  else if (y >= 169 && y < 194) action = 3;
  else if (y >= 196 && y < 222) action = 4;
  if (action < 0) return true;
  g_message_action.selected = (uint8_t)action;

  if (action == 0) {
    g_message_action.page = 1;
    _dirty = DIRTY_ALL;
    return true;
  }

  if (action == 1) {
    if (!beginReplyToMessage(g_message_action.slot)) {
      _task->showAlert("Could not create reply reference", 1100);
      return true;
    }
    closeModal();
    _task->showAlert("Local reply reference set", 900);
    return true;
  }

  if (action == 2) {
    bool can_stop = m.outgoing && m.send_state == SEND_SENDING && _active_kind == ROW_CONTACT;
    bool can_retry = m.outgoing && m.send_state == SEND_FAILED &&
                     (_active_kind == ROW_CONTACT || _active_kind == ROW_CHANNEL);

    if (can_stop) {
      // A send has already crossed into MeshCore. We can stop local ACK
      // tracking, but we cannot retract a packet that may already be on-air.
      if (m.delivery_ack) the_mesh.releaseCompactAck(m.delivery_ack);
      m.delivery_ack = 0;
      m.delivery_deadline_ms = 0;
      m.send_state = SEND_STOPPED;
      persistenceCheckpoint(true);
      closeModal();
      _task->showAlert("Stopped waiting - packet already sent", 1300);
      return true;
    }

    if (!can_retry) {
      _task->showAlert("Retry is only for failed sends", 1000);
      return true;
    }

    bool ok = false;
    if (_active_kind == ROW_CONTACT) {
      // Refresh the contact record by stable public key so a route update since
      // the conversation opened is used by the retry.
      loadContactsAndChannels();
      for (int i = 0; i < _contact_count; ++i) {
        if (memcmp(_contacts[i].id.pub_key, _active_contact.id.pub_key, 32) == 0) {
          _active_contact = _contacts[i];
          break;
        }
      }

      uint32_t expected_ack = 0, timeout_ms = 0;
      // attempt=4 is MeshCore's explicit retry form and makes the retry packet
      // distinguishable from the original attempt even inside the same RTC second.
      int result = the_mesh.sendMessage(_active_contact, _rtc->getCurrentTime(), 4,
                                        m.text, expected_ack, timeout_ms);
      ok = result != MSG_SEND_FAILED;
      if (ok) {
        char staged_origin[32] = {0};
        uint32_t staged_ack = 0, staged_timeout = 0;
        if (the_mesh.takeCompactSendStart(staged_origin, sizeof(staged_origin), staged_ack, staged_timeout) && staged_ack) {
          m.send_state = SEND_SENDING;
          m.delivery_ack = staged_ack;
          m.delivery_deadline_ms = millis() + (staged_timeout ? staged_timeout : 5000U);
          m.path_len = _active_contact.out_path_len;
        } else {
          // Never turn an untracked retry into a success claim.
          m.send_state = SEND_FAILED;
          ok = false;
        }
      }
    } else if (_active_kind == ROW_CHANNEL) {
      // Group transport has no per-peer ACK. Successful here means accepted by
      // the local MeshCore group send path, not delivered to every group member.
      ok = the_mesh.sendGroupMessage(_rtc->getCurrentTime(), _active_channel.channel,
                                     _task->getNodePrefs()->node_name, m.text, strlen(m.text));
      m.send_state = ok ? SEND_SENT : SEND_FAILED;
      m.delivery_ack = 0;
      m.delivery_deadline_ms = 0;
      m.path_len = OUT_PATH_UNKNOWN;
    }

    if (!ok) m.send_state = SEND_FAILED;
    persistenceCheckpoint(true);
    closeModal();
    _task->showAlert(ok ? (_active_kind == ROW_CONTACT ? "Retry sending" : "Group retry queued")
                        : "Retry failed to queue", ok ? 900 : 1200);
    return true;
  }

  if (action == 3) {
    g_message_action.page = 2;
    _dirty = DIRTY_ALL;
    return true;
  }

  closeModal();
  return true;
}

bool CommunicatorAppScreen::handleMessageActionInput(char c) {
  if (!messageActionActive()) return false;

  if (c == KEY_CANCEL) {
    if (g_message_action.page == 0) {
      g_message_action.owner = nullptr;
      g_message_action.slot = -1;
    } else {
      g_message_action.page = 0;
      g_message_action.selected = 0;
    }
    _dirty = DIRTY_ALL;
    return true;
  }

  if (g_message_action.page == 0) {
    if (c == KEY_UP) {
      if (g_message_action.selected) --g_message_action.selected;
      _dirty = DIRTY_ALL;
      return true;
    }
    if (c == KEY_DOWN) {
      if (g_message_action.selected < 4) ++g_message_action.selected;
      _dirty = DIRTY_ALL;
      return true;
    }
    if (c == KEY_ENTER) {
      static const int ys[5] = {100, 127, 154, 181, 208};
      return handleMessageActionTouch(160, ys[g_message_action.selected], COMPACT_TOUCH_TAP);
    }
    return true;
  }

  if (g_message_action.page == 1) {
    if (c == KEY_ENTER) {
      g_message_action.page = 0;
      g_message_action.selected = 0;
      _dirty = DIRTY_ALL;
    }
    return true;
  }

  if (g_message_action.page == 2) {
    if (c == KEY_ENTER) return handleMessageActionTouch(220, 196, COMPACT_TOUCH_TAP);
    return true;
  }

  return true;
}

void CommunicatorAppScreen::drawMessageActionOverlay(DisplayDriver& d) {
  if (!messageActionActive()) return;
  if (g_message_action.slot < 0 || g_message_action.slot >= MESSAGE_CACHE) return;

  MessageEntry& m = _messages[g_message_action.slot];
  const ColorVal panel = _light_mode ? actionRgb565(255,255,255) : actionRgb565(13,30,51);
  const ColorVal row = _light_mode ? actionRgb565(238,243,249) : actionRgb565(20,42,67);
  const ColorVal text = _light_mode ? actionRgb565(20,28,40) : actionRgb565(255,255,255);
  const ColorVal sub = _light_mode ? actionRgb565(92,108,128) : actionRgb565(166,185,207);
  const ColorVal accent = _light_mode ? actionRgb565(11,58,117) : actionRgb565(35,85,150);
  const ColorVal danger = actionRgb565(210,55,55);
  const ColorVal stroke = _light_mode ? actionRgb565(176,198,221) : actionRgb565(55,88,124);

  d.setColor(panel);
  d.fillRoundRect(24, 48, 272, 184, 9);
  d.setColor(stroke);
  d.drawRoundRect(24, 48, 272, 184, 9);
  d.setTextSize(1);
  d.setColor(text);

  if (g_message_action.page == 2) {
    d.drawTextCentered(160, 66, "Delete this message?");
    d.setColor(sub);
    d.drawTextCentered(160, 91, "This removes only the T-Deck copy.");
    d.drawTextCentered(160, 106, "No MeshCore packet is transmitted.");
    d.setColor(row); d.fillRoundRect(40,176,112,40,6); d.fillRoundRect(168,176,112,40,6);
    d.setColor(stroke); d.drawRoundRect(40,176,112,40,6); d.drawRoundRect(168,176,112,40,6);
    d.setColor(text); d.drawTextCentered(96,191,"Cancel");
    d.setColor(danger); d.drawTextCentered(224,191,"Delete locally");
    return;
  }

  if (g_message_action.page == 1) {
    d.drawTextCentered(160, 60, m.outgoing ? "Delivery details" : "Reception details");
    d.setColor(text);
    d.drawTextEllipsized(38, 76, 244, m.text);

    char status[88];
    if (!m.outgoing) {
      strcpy(status, "Received by this T-Deck");
    } else if (_active_kind == ROW_CHANNEL) {
      if (m.send_state == SEND_FAILED) strcpy(status, "Failed: local group send failed");
      else if (m.send_state == SEND_SENDING) strcpy(status, "Sending: local group send pending");
      else strcpy(status, "Sent: no per-peer group ACK exists");
    } else {
      if (m.send_state == SEND_CONFIRMED) strcpy(status, "Confirmed: MeshCore ACK received");
      else if (m.send_state == SEND_SENDING) strcpy(status, "Sending: awaiting MeshCore ACK");
      else if (m.send_state == SEND_STOPPED) strcpy(status, "Stopped locally; RF packet was already sent");
      else if (m.send_state == SEND_FAILED) strcpy(status, "Failed: no ACK or queue failure");
      else if (m.send_state == SEND_SENT) strcpy(status, "Sent: no retained peer-ACK proof");
      else strcpy(status, "Queued / local state only");
    }
    d.setColor(sub);
    drawWrapped(d, 38, 96, 244, 2, status);

    char route[80];
    if (!m.outgoing) {
      if (m.path_len == OUT_PATH_UNKNOWN) strcpy(route, "Receive path: unknown / flood metadata unavailable");
      else snprintf(route, sizeof(route), "Receive path: %u hop%s", m.path_len, m.path_len == 1 ? "" : "s");
    } else if (_active_kind == ROW_CHANNEL) {
      strcpy(route, "Group delivery is intentionally not peer-confirmed");
    } else if (m.path_len == OUT_PATH_UNKNOWN) {
      strcpy(route, "Send route: flood / no stored direct path");
    } else {
      snprintf(route, sizeof(route), "Send route: directed, %u hop%s", m.path_len, m.path_len == 1 ? "" : "s");
    }
    drawWrapped(d, 38, 124, 244, 2, route);

    char reply[80];
    if (replyTargetForMessage(g_message_action.slot)) {
      char preview[52];
      getReplyTargetPreview(g_message_action.slot, preview, sizeof(preview));
      snprintf(reply, sizeof(reply), "Local reply to: %.48s", preview);
    } else {
      strcpy(reply, "Local reply reference: none");
    }
    d.drawTextEllipsized(38, 154, 244, reply);

    char stamp[48];
    snprintf(stamp, sizeof(stamp), "Message timestamp: %lu", (unsigned long)m.timestamp);
    d.drawTextEllipsized(38, 171, 244, stamp);
    d.setColor(row); d.fillRoundRect(78,198,164,23,6);
    d.setColor(stroke); d.drawRoundRect(78,198,164,23,6);
    d.setColor(text); d.drawTextCentered(160,205,"Back");
    return;
  }

  d.drawTextCentered(160, 56, "Message actions");
  d.setColor(sub);
  d.drawTextEllipsized(38, 72, 244, m.text);

  bool can_stop = m.outgoing && m.send_state == SEND_SENDING && _active_kind == ROW_CONTACT;
  bool can_retry = m.outgoing && m.send_state == SEND_FAILED &&
                   (_active_kind == ROW_CONTACT || _active_kind == ROW_CHANNEL);
  const char* labels[5] = {
    m.outgoing ? "Delivery details" : "Reception details",
    "Reply (local reference)",
    can_stop ? "Stop waiting (local)" : "Retry failed send",
    "Delete locally",
    "Close"
  };
  const int ys[5] = {88,115,142,169,196};
  const int hs[5] = {25,25,25,25,26};

  for (int i = 0; i < 5; ++i) {
    bool enabled = i != 2 || can_retry || can_stop;
    d.setColor(i == g_message_action.selected ? accent : row);
    d.fillRoundRect(35, ys[i], 250, hs[i], 5);
    d.setColor(stroke);
    d.drawRoundRect(35, ys[i], 250, hs[i], 5);
    d.setColor(!enabled ? sub : (i == 3 ? danger : text));
    d.drawTextCentered(160, ys[i] + hs[i]/2 - 4, labels[i]);
  }
}
