#include "../MyMesh.h"
#include <string.h>

#ifdef MESHCORE_COMPACT_UI

namespace {

struct CompactSendStart {
  bool ready;
  char origin[32];
  uint32_t ack;
  uint32_t timeout_ms;
};

CompactSendStart g_compact_send_start = {};
constexpr unsigned long kAckEntryStaleMs = 120000UL;

} // namespace

int MyMesh::sendCompactMessage(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text,
                               uint32_t& expected_ack, uint32_t& est_timeout) {
  expected_ack = 0;
  est_timeout = 0;

  ContactInfo* stored = lookupContactByPubKey(recipient.id.pub_key, PUB_KEY_SIZE);
  if (!stored) return MSG_SEND_FAILED;

  const unsigned long now = _ms->getMillis();
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; ++i) {
    if (expected_ack_table[i].ack && (unsigned long)(now - expected_ack_table[i].msg_sent) > kAckEntryStaleMs) {
      expected_ack_table[i].ack = 0;
      expected_ack_table[i].contact = nullptr;
    }
  }

  int slot = -1;
  for (int offset = 0; offset < EXPECTED_ACK_TABLE_SIZE; ++offset) {
    int idx = (next_ack_idx + offset) % EXPECTED_ACK_TABLE_SIZE;
    if (!expected_ack_table[idx].ack) {
      slot = idx;
      break;
    }
  }
  if (slot < 0) return MSG_SEND_FAILED;

  int result = BaseChatMesh::sendMessage(*stored, timestamp, attempt, text, expected_ack, est_timeout);
  if (result == MSG_SEND_FAILED) return result;

  if (expected_ack) {
    expected_ack_table[slot].msg_sent = now;
    expected_ack_table[slot].ack = expected_ack;
    expected_ack_table[slot].contact = stored;
    next_ack_idx = (slot + 1) % EXPECTED_ACK_TABLE_SIZE;

    memset(&g_compact_send_start, 0, sizeof(g_compact_send_start));
    g_compact_send_start.ready = true;
    strncpy(g_compact_send_start.origin, stored->name, sizeof(g_compact_send_start.origin) - 1);
    g_compact_send_start.ack = expected_ack;
    g_compact_send_start.timeout_ms = est_timeout;
  }

  return result;
}

bool MyMesh::compactSendStartPending() const {
  return g_compact_send_start.ready;
}

bool MyMesh::takeCompactSendStart(char* origin, size_t origin_len, uint32_t& ack, uint32_t& timeout_ms) {
  if (!g_compact_send_start.ready) return false;
  if (origin && origin_len) {
    strncpy(origin, g_compact_send_start.origin, origin_len - 1);
    origin[origin_len - 1] = 0;
  }
  ack = g_compact_send_start.ack;
  timeout_ms = g_compact_send_start.timeout_ms;
  g_compact_send_start.ready = false;
  return true;
}

bool MyMesh::isCompactAckPending(uint32_t ack) const {
  if (!ack) return false;
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; ++i) {
    if (expected_ack_table[i].ack == ack) return true;
  }
  return false;
}

void MyMesh::releaseCompactAck(uint32_t ack) {
  if (!ack) return;
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; ++i) {
    if (expected_ack_table[i].ack == ack) {
      expected_ack_table[i].ack = 0;
      expected_ack_table[i].contact = nullptr;
      return;
    }
  }
}

#endif // MESHCORE_COMPACT_UI
