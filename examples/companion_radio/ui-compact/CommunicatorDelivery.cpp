#include "CommunicatorAppScreen.h"
#include "UITask.h"
#include "../MyMesh.h"
#include <string.h>

namespace {

constexpr ColorVal compactRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

bool deadlinePassed(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

} // namespace

void CommunicatorAppScreen::reconcileDirectSendState() {
  bool changed = false;
  char origin[32] = {0};
  uint32_t ack = 0;
  uint32_t timeout_ms = 0;

  if (the_mesh.takeCompactSendStart(origin, sizeof(origin), ack, timeout_ms)) {
    // sendCompose() appends the local bubble immediately after sendMessage()
    // returns. Bind the staged ACK to that newest matching bubble before the
    // next frame or persistence checkpoint can present it as delivered.
    for (int n = 0; n < _message_count; ++n) {
      int idx = (_message_head + MESSAGE_CACHE - n) % MESSAGE_CACHE;
      MessageEntry& m = _messages[idx];
      if (m.outgoing && m.send_state == SEND_SENT && m.delivery_ack == 0 &&
          strcmp(m.origin, origin) == 0) {
        m.send_state = SEND_SENDING;
        m.delivery_ack = ack;
        uint32_t wait_ms = timeout_ms ? timeout_ms : 5000U;
        m.delivery_deadline_ms = millis() + wait_ms;
        if (_active_kind == ROW_CONTACT && strcmp(m.origin, _active_name) == 0) {
          // Persist the route that was actually selected for this send. OUT_PATH_UNKNOWN
          // remains a truthful marker for flood/no stored direct route.
          m.path_len = _active_contact.out_path_len;
        }
        changed = true;
        if (_route == ROUTE_CHAT && strcmp(m.origin, _active_name) == 0) _task->showAlert("Sending", 900);
        break;
      }
    }
  }

  const uint32_t now = millis();
  for (int i = 0; i < MESSAGE_CACHE; ++i) {
    MessageEntry& m = _messages[i];
    if (!m.outgoing || m.send_state != SEND_SENDING) continue;

    // A persisted SENDING state has no live ACK token after reboot. Treat that
    // as failed/unknown instead of ever claiming delivery that cannot be proven.
    if (!m.delivery_ack) {
      m.send_state = SEND_FAILED;
      m.delivery_deadline_ms = 0;
      changed = true;
      continue;
    }

    if (!the_mesh.isCompactAckPending(m.delivery_ack)) {
      // CONFIRMED is deliberately distinct from legacy SEND_SENT. Older compact
      // builds wrote SEND_SENT immediately, so only a live ACK transition may
      // produce this stronger state.
      m.send_state = SEND_CONFIRMED;
      m.delivery_ack = 0;
      m.delivery_deadline_ms = 0;
      changed = true;
      if (_route == ROUTE_CHAT && strcmp(m.origin, _active_name) == 0) _task->showAlert("Confirmed", 750);
      continue;
    }

    if (m.delivery_deadline_ms && deadlinePassed(now, m.delivery_deadline_ms)) {
      the_mesh.releaseCompactAck(m.delivery_ack);
      m.send_state = SEND_FAILED;
      m.delivery_ack = 0;
      m.delivery_deadline_ms = 0;
      changed = true;
      if (_route == ROUTE_CHAT && strcmp(m.origin, _active_name) == 0) _task->showAlert("Failed - no ACK", 1200);
    }
  }

  if (changed) _dirty = DIRTY_ALL;
}

void CommunicatorAppScreen::drawDirectSendOverlay(DisplayDriver& d) {
  if (_route != ROUTE_CHAT) return;

  int top = _chat_search_active ? 108 : 80;
  int indexes[4];
  int maxn = _chat_search_active ? 2 : 3;
  int count = collectActiveMessages(indexes, maxn);
  int y = top + 5;

  const ColorVal bubble = _light_mode ? compactRgb565(31, 99, 198) : compactRgb565(19, 79, 166);
  const ColorVal label = _light_mode ? compactRgb565(255, 255, 255) : compactRgb565(225, 235, 250);

  for (int i = 0; i < count; ++i) {
    const MessageEntry& m = _messages[indexes[i]];
    if (m.outgoing && (m.send_state == SEND_SENDING || m.send_state == SEND_CONFIRMED)) {
      int tw = d.getTextWidth(m.text);
      if (tw > 205) tw = 205;
      int w = tw + 18;
      if (w < 72) w = 72;
      if (w > 226) w = 226;
      int x = 312 - w;

      // drawChat() still paints its legacy lower metadata line. Cover only that
      // line for states that need stronger wording than the old Sent label.
      d.setColor(bubble);
      d.fillRect(x + 5, y + 17, w - 10, 11);
      d.setTextSize(1);
      d.setColor(label);
      d.setCursor(x + 8, y + 19);
      d.print(m.send_state == SEND_SENDING ? "Sending" : "Confirmed");
    }
    y += 35;
  }
}
