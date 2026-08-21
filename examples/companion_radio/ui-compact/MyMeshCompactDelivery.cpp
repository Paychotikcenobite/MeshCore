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

struct CompactAttemptStart {
  bool ready;
  char origin[32];
  uint8_t attempt;
  uint8_t path_len;
};

struct CompactAckTrack {
  uint32_t ack;
  uint8_t slot;
  unsigned long msg_sent;
  bool confirmed;
};

CompactSendStart g_compact_send_start = {};
CompactAttemptStart g_compact_attempt_start = {};
CompactAckTrack g_compact_ack_tracks[EXPECTED_ACK_TABLE_SIZE] = {};
constexpr unsigned long kAckEntryStaleMs = 120000UL;
constexpr uint8_t kInvalidAckSlot = 0xFF;

int findCompactTrack(uint32_t ack) {
  if (!ack) return -1;
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; ++i) {
    if (g_compact_ack_tracks[i].ack == ack) return i;
  }
  return -1;
}

int findFreeCompactTrack() {
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; ++i) {
    if (!g_compact_ack_tracks[i].ack) return i;
  }
  return -1;
}

void markCompactSlotLost(int slot, uint32_t ack, unsigned long msg_sent) {
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; ++i) {
    CompactAckTrack& track = g_compact_ack_tracks[i];
    if (track.ack == ack && track.slot == slot && track.msg_sent == msg_sent) {
      // Keep the tracker alive so the UI remains in Sending until its own
      // deadline, but make it impossible for a later zero in this shared slot
      // to be misread as confirmation of the old compact message.
      track.slot = kInvalidAckSlot;
      return;
    }
  }
}

} // namespace

bool MyMesh::sendCompactGroupMessage(uint32_t timestamp, mesh::GroupChannel& channel, const char* sender_name,
                                     const char* text, int text_len) {
  // BaseChatMesh group text is always a flood send and has no receiver ACK.
  // This wrapper exists so the Compact UI has an explicit, target-gated send
  // boundary without changing the MeshCore packet format or phone/BLE path.
  return BaseChatMesh::sendGroupMessage(timestamp, channel, sender_name, text, text_len);
}

int MyMesh::sendCompactMessage(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text,
                               uint32_t& expected_ack, uint32_t& est_timeout) {
  expected_ack = 0;
  est_timeout = 0;
  memset(&g_compact_send_start, 0, sizeof(g_compact_send_start));

  // Stage the attempted packet metadata before any failure return. This lets
  // the UI persist both attempt number and planned route even when packet
  // creation/table capacity fails and no ACK token can be registered.
  memset(&g_compact_attempt_start, 0, sizeof(g_compact_attempt_start));
  g_compact_attempt_start.ready = true;
  strncpy(g_compact_attempt_start.origin, recipient.name, sizeof(g_compact_attempt_start.origin) - 1);
  g_compact_attempt_start.attempt = attempt;
  g_compact_attempt_start.path_len = recipient.out_path_len;

  ContactInfo* stored = lookupContactByPubKey(recipient.id.pub_key, PUB_KEY_SIZE);
  if (!stored) return MSG_SEND_FAILED;

  const unsigned long cleanup_now = _ms->getMillis();
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; ++i) {
    if (expected_ack_table[i].ack && (unsigned long)(cleanup_now - expected_ack_table[i].msg_sent) > kAckEntryStaleMs) {
      markCompactSlotLost(i, expected_ack_table[i].ack, expected_ack_table[i].msg_sent);
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
  int track_slot = findFreeCompactTrack();
  if (slot < 0 || track_slot < 0) return MSG_SEND_FAILED;

  int result = BaseChatMesh::sendMessage(*stored, timestamp, attempt, text, expected_ack, est_timeout);
  if (result == MSG_SEND_FAILED) return result;

  if (expected_ack) {
    // Match the established phone/BLE path: register the ACK generation after
    // sendMessage() has returned from packet handoff.
    const unsigned long msg_sent = _ms->getMillis();
    expected_ack_table[slot].msg_sent = msg_sent;
    expected_ack_table[slot].ack = expected_ack;
    expected_ack_table[slot].contact = stored;
    next_ack_idx = (slot + 1) % EXPECTED_ACK_TABLE_SIZE;

    g_compact_ack_tracks[track_slot].ack = expected_ack;
    g_compact_ack_tracks[track_slot].slot = (uint8_t)slot;
    g_compact_ack_tracks[track_slot].msg_sent = msg_sent;
    g_compact_ack_tracks[track_slot].confirmed = false;

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

bool MyMesh::takeCompactAttemptStart(char* origin, size_t origin_len, uint8_t& attempt, uint8_t& path_len) {
  if (!g_compact_attempt_start.ready) return false;
  if (origin && origin_len) {
    strncpy(origin, g_compact_attempt_start.origin, origin_len - 1);
    origin[origin_len - 1] = 0;
  }
  attempt = g_compact_attempt_start.attempt;
  path_len = g_compact_attempt_start.path_len;
  g_compact_attempt_start.ready = false;
  return true;
}

void MyMesh::noteCompactAckReceived(uint32_t ack, unsigned long msg_sent) {
  int track_idx = findCompactTrack(ack);
  if (track_idx < 0) return;
  CompactAckTrack& track = g_compact_ack_tracks[track_idx];
  if (track.msg_sent == msg_sent) track.confirmed = true;
}

bool MyMesh::isCompactAckPending(uint32_t ack) const {
  int track_idx = findCompactTrack(ack);
  if (track_idx < 0) {
    // Unknown must never mean delivered. The caller will eventually time out.
    return true;
  }

  CompactAckTrack& track = g_compact_ack_tracks[track_idx];
  if (track.confirmed) {
    memset(&track, 0, sizeof(track));
    return false;
  }
  if (track.slot >= EXPECTED_ACK_TABLE_SIZE) return true;

  const AckTableEntry& entry = expected_ack_table[track.slot];
  if (entry.msg_sent != track.msg_sent) {
    // The shared circular ACK table was reused by another sender. Keep this
    // compact message pending until its deadline rather than falsely treating
    // the missing token as an ACK.
    track.slot = kInvalidAckSlot;
    return true;
  }
  if (entry.ack == ack) return true;
  if (entry.ack == 0) {
    // processAck() clears only the ACK field and leaves msg_sent unchanged, so
    // zero + the original generation timestamp is a positive confirmation.
    memset(&track, 0, sizeof(track));
    return false;
  }

  // A different nonzero ACK in the same generation is also unsafe to call
  // delivered; fail conservatively at the message deadline.
  track.slot = kInvalidAckSlot;
  return true;
}

void MyMesh::releaseCompactAck(uint32_t ack) {
  int track_idx = findCompactTrack(ack);
  if (track_idx < 0) return;

  CompactAckTrack& track = g_compact_ack_tracks[track_idx];
  if (track.slot < EXPECTED_ACK_TABLE_SIZE) {
    AckTableEntry& entry = expected_ack_table[track.slot];
    if (entry.msg_sent == track.msg_sent && entry.ack == ack) {
      entry.ack = 0;
      entry.contact = nullptr;
    }
  }
  memset(&track, 0, sizeof(track));
}

#endif // MESHCORE_COMPACT_UI
