#include "CommunicatorAppScreen.h"
#include "UITask.h"
#include <SPIFFS.h>
#include <FS.h>
#include <string.h>
#include <stddef.h>

namespace {

constexpr int kCacheSlots = 96;
constexpr int kMaxDrafts = 16;
constexpr uint32_t kHistoryMagic = 0x3343434D;   // MCC3
constexpr uint32_t kRecordMagic = 0x314A434D;    // MCJ1
constexpr uint32_t kDraftMagic = 0x3144434D;     // MCD1
constexpr uint16_t kSchemaVersion = 1;
constexpr size_t kCompactAtBytes = 192 * 1024;
const char* kHistoryPath = "/mcc_history_v1.bin";
const char* kHistoryTmp = "/mcc_history_v1.tmp";
const char* kHistoryBak = "/mcc_history_v1.bak";
const char* kHistoryBad = "/mcc_history_v1.bad";
const char* kDraftPath = "/mcc_drafts_v1.bin";
const char* kDraftTmp = "/mcc_drafts_v1.tmp";
const char* kDraftBak = "/mcc_drafts_v1.bak";
const char* kDraftBad = "/mcc_drafts_v1.bad";

#pragma pack(push, 1)
struct HistoryHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t record_size;
  uint16_t live_limit;
  uint16_t reserved;
  uint32_t crc;
};

struct HistoryRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint64_t id;
  uint64_t reply_to;
  uint32_t timestamp;
  uint8_t conv_kind;       // 1=direct contact, 2=channel, 3=unresolved fallback
  uint8_t flags;           // bit0 outgoing, bit1 unread, bit2 deleted
  uint8_t send_state;
  uint8_t path_len;
  uint8_t conv_key[32];
  char origin[32];         // last-known display name only; never the primary key
  char text[144];
  uint32_t crc;
};

struct DraftHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t record_size;
  uint16_t count;
  uint16_t reserved;
  uint32_t crc;
};

struct DraftRecord {
  uint8_t conv_kind;
  uint8_t reserved[3];
  uint8_t conv_key[32];
  char text[MAX_TEXT_LEN + 1];
  uint32_t crc;
};
#pragma pack(pop)

struct PersistenceState {
  bool begun = false;
  bool needs_compact = false;
  bool storage_ok = true;
  unsigned long last_checkpoint = 0;
  unsigned long last_draft_write = 0;
  uint64_t next_id = 1;
  uint64_t ids[kCacheSlots] = {0};
  uint64_t reply_to[kCacheSlots] = {0};
  uint32_t content_hash[kCacheSlots] = {0};
  uint32_t persist_hash[kCacheSlots] = {0};
  uint8_t conv_kind[kCacheSlots] = {0};
  uint8_t conv_key[kCacheSlots][32] = {{0}};
  DraftRecord drafts[kMaxDrafts] = {};
  uint8_t draft_count = 0;
  bool active_draft_valid = false;
  uint8_t active_draft_kind = 0;
  uint8_t active_draft_key[32] = {0};
  // Reply is a local UI relationship. The target is an existing stable history
  // ID; no bytes are added to MeshCore's plain/group text RF payload.
  uint64_t pending_reply_to = 0;
  char pending_reply_preview[56] = {0};
};

PersistenceState g;

constexpr ColorVal replyRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
  }
  return ~crc;
}

uint32_t fnv1a(const void* data, size_t len, uint32_t seed = 2166136261u) {
  const uint8_t* p = (const uint8_t*)data;
  uint32_t h = seed;
  for (size_t i = 0; i < len; ++i) { h ^= p[i]; h *= 16777619u; }
  return h;
}

void fallbackKey(const char* name, uint8_t key[32]) {
  uint32_t h = fnv1a(name ? name : "", name ? strlen(name) : 0);
  for (int i = 0; i < 8; ++i) {
    uint32_t v = fnv1a(&h, sizeof(h), 2166136261u + (uint32_t)i * 0x9E3779B9u);
    memcpy(key + i * 4, &v, 4);
    h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
  }
}

HistoryHeader makeHistoryHeader() {
  HistoryHeader h{};
  h.magic = kHistoryMagic;
  h.version = kSchemaVersion;
  h.record_size = sizeof(HistoryRecord);
  h.live_limit = kCacheSlots;
  h.crc = crc32((const uint8_t*)&h, offsetof(HistoryHeader, crc));
  return h;
}

bool validHistoryHeader(const HistoryHeader& h) {
  return h.magic == kHistoryMagic && h.version == kSchemaVersion &&
         h.record_size == sizeof(HistoryRecord) && h.live_limit == kCacheSlots &&
         h.crc == crc32((const uint8_t*)&h, offsetof(HistoryHeader, crc));
}

bool validRecord(const HistoryRecord& r) {
  return r.magic == kRecordMagic && r.version == kSchemaVersion && r.size == sizeof(HistoryRecord) &&
         r.id != 0 && r.crc == crc32((const uint8_t*)&r, offsetof(HistoryRecord, crc));
}

DraftHeader makeDraftHeader(uint16_t count) {
  DraftHeader h{};
  h.magic = kDraftMagic;
  h.version = kSchemaVersion;
  h.record_size = sizeof(DraftRecord);
  h.count = count;
  h.crc = crc32((const uint8_t*)&h, offsetof(DraftHeader, crc));
  return h;
}

bool validDraftHeader(const DraftHeader& h) {
  return h.magic == kDraftMagic && h.version == kSchemaVersion && h.record_size == sizeof(DraftRecord) &&
         h.count <= kMaxDrafts && h.crc == crc32((const uint8_t*)&h, offsetof(DraftHeader, crc));
}

bool sameKey(uint8_t kindA, const uint8_t a[32], uint8_t kindB, const uint8_t b[32]) {
  return kindA == kindB && kindA != 0 && memcmp(a, b, 32) == 0;
}

bool replaceAtomically(const char* tmp, const char* dst, const char* bak) {
  SPIFFS.remove(bak);
  bool had = SPIFFS.exists(dst);
  if (had && !SPIFFS.rename(dst, bak)) return false;
  if (!SPIFFS.rename(tmp, dst)) {
    if (had && SPIFFS.exists(bak)) SPIFFS.rename(bak, dst);
    return false;
  }
  if (SPIFFS.exists(bak)) SPIFFS.remove(bak);
  return true;
}

bool ensureHistoryFile() {
  if (SPIFFS.exists(kHistoryPath)) return true;
  File f = SPIFFS.open(kHistoryPath, FILE_WRITE);
  if (!f) return false;
  HistoryHeader h = makeHistoryHeader();
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h);
  f.flush();
  f.close();
  return ok;
}

bool appendHistoryRecord(HistoryRecord& r) {
  if (!ensureHistoryFile()) return false;
  r.magic = kRecordMagic;
  r.version = kSchemaVersion;
  r.size = sizeof(HistoryRecord);
  r.crc = crc32((const uint8_t*)&r, offsetof(HistoryRecord, crc));
  File f = SPIFFS.open(kHistoryPath, FILE_APPEND);
  if (!f) return false;
  bool ok = f.write((const uint8_t*)&r, sizeof(r)) == sizeof(r);
  f.flush();
  f.close();
  return ok;
}

int findDraft(uint8_t kind, const uint8_t key[32]) {
  for (int i = 0; i < g.draft_count; ++i) if (sameKey(g.drafts[i].conv_kind, g.drafts[i].conv_key, kind, key)) return i;
  return -1;
}

bool writeDraftsAtomic() {
  File f = SPIFFS.open(kDraftTmp, FILE_WRITE);
  if (!f) return false;
  DraftHeader h = makeDraftHeader(g.draft_count);
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h);
  for (int i = 0; ok && i < g.draft_count; ++i) {
    g.drafts[i].crc = crc32((const uint8_t*)&g.drafts[i], offsetof(DraftRecord, crc));
    ok = f.write((const uint8_t*)&g.drafts[i], sizeof(DraftRecord)) == sizeof(DraftRecord);
  }
  f.flush();
  f.close();
  if (!ok) { SPIFFS.remove(kDraftTmp); return false; }
  return replaceAtomically(kDraftTmp, kDraftPath, kDraftBak);
}

void loadDrafts() {
  g.draft_count = 0;
  if (!SPIFFS.exists(kDraftPath)) return;
  File f = SPIFFS.open(kDraftPath, FILE_READ);
  if (!f) { g.storage_ok = false; return; }
  DraftHeader h{};
  bool ok = f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && validDraftHeader(h);
  if (ok) {
    for (int i = 0; i < h.count; ++i) {
      DraftRecord d{};
      if (f.read((uint8_t*)&d, sizeof(d)) != sizeof(d) ||
          d.crc != crc32((const uint8_t*)&d, offsetof(DraftRecord, crc)) || d.conv_kind == 0) { ok = false; break; }
      g.drafts[g.draft_count++] = d;
    }
  }
  f.close();
  if (!ok) {
    SPIFFS.remove(kDraftBad);
    SPIFFS.rename(kDraftPath, kDraftBad);
    g.draft_count = 0;
    g.storage_ok = false;
  }
}

uint32_t messageContentHash(uint32_t ts, bool outgoing, const char* origin, const char* text) {
  uint32_t h = fnv1a(&ts, sizeof(ts));
  h = fnv1a(&outgoing, sizeof(outgoing), h);
  h = fnv1a(origin, origin ? strlen(origin) : 0, h);
  return fnv1a(text, text ? strlen(text) : 0, h);
}

uint32_t messagePersistHash(uint32_t contentHash, uint8_t unread, uint8_t sendState, uint8_t pathLen,
                            uint8_t kind, const uint8_t key[32], uint64_t replyTo) {
  uint32_t h = fnv1a(&contentHash, sizeof(contentHash));
  h = fnv1a(&unread, sizeof(unread), h);
  h = fnv1a(&sendState, sizeof(sendState), h);
  h = fnv1a(&pathLen, sizeof(pathLen), h);
  h = fnv1a(&kind, sizeof(kind), h);
  h = fnv1a(key, 32, h);
  return fnv1a(&replyTo, sizeof(replyTo), h);
}

} // namespace

void CommunicatorAppScreen::openSettingsSingleTop() {
  if (_route == ROUTE_SETTINGS) { _dirty = DIRTY_ALL; return; }
  for (int i = (int)_route_depth - 1; i >= 0; --i) {
    if (_route_stack[i] == ROUTE_SETTINGS) {
      _route = ROUTE_SETTINGS;
      _route_depth = (uint8_t)i;
      _selected = _list_offset = 0;
      _search_active = _chat_search_active = false;
      _dirty = DIRTY_ALL;
      return;
    }
  }
  pushRoute(ROUTE_SETTINGS);
}

void CommunicatorAppScreen::openRadioSingleTop() {
  if (_route == ROUTE_RADIO) { _dirty = DIRTY_ALL; return; }
  for (int i = (int)_route_depth - 1; i >= 0; --i) {
    if (_route_stack[i] == ROUTE_RADIO) {
      _route = ROUTE_RADIO;
      _route_depth = (uint8_t)i;
      _selected = _list_offset = 0;
      _search_active = _chat_search_active = false;
      _dirty = DIRTY_ALL;
      return;
    }
  }
  pushRoute(ROUTE_RADIO);
}

void CommunicatorAppScreen::navigateBack() {
  clearPendingReply();
  if (_route == ROUTE_MAIN) { _dirty = DIRTY_ALL; return; }
  goBack();
}

void CommunicatorAppScreen::navigateHome() {
  clearPendingReply();
  goHome();
}

bool CommunicatorAppScreen::beginReplyToMessage(int slot) {
  if (slot < 0 || slot >= kCacheSlots) return false;
  if (!_messages[slot].origin[0] && !_messages[slot].text[0]) return false;

  // Ensure the target already has its durable ID before the compose relationship
  // is staged. This does not transmit anything over LoRa.
  persistenceCheckpoint(true);
  if (!g.ids[slot]) return false;

  g.pending_reply_to = g.ids[slot];
  StrHelper::strncpy(g.pending_reply_preview, _messages[slot].text, sizeof(g.pending_reply_preview));
  _dirty = DIRTY_ALL;
  return true;
}

void CommunicatorAppScreen::clearPendingReply() {
  g.pending_reply_to = 0;
  g.pending_reply_preview[0] = 0;
  _dirty = DIRTY_ALL;
}

bool CommunicatorAppScreen::replyPending() const {
  return g.pending_reply_to != 0;
}

bool CommunicatorAppScreen::handleReplyTouch(int16_t x, int16_t y, uint8_t gesture) {
  (void)x;
  if (!replyPending() || _route != ROUTE_CHAT || gesture != COMPACT_TOUCH_TAP) return false;
  if (y >= 180 && y < 202) {
    clearPendingReply();
    _task->showAlert("Reply reference cancelled", 800);
    return true;
  }
  return false;
}

void CommunicatorAppScreen::drawReplyComposerOverlay(DisplayDriver& d) {
  if (!replyPending() || _route != ROUTE_CHAT) return;
  const ColorVal bg = _light_mode ? replyRgb565(232,240,249) : replyRgb565(24,49,78);
  const ColorVal stroke = _light_mode ? replyRgb565(150,180,211) : replyRgb565(61,101,142);
  const ColorVal text = _light_mode ? replyRgb565(28,54,83) : replyRgb565(226,238,250);
  d.setColor(bg);
  d.fillRoundRect(6, 181, 308, 19, 5);
  d.setColor(stroke);
  d.drawRoundRect(6, 181, 308, 19, 5);
  d.setTextSize(1);
  d.setColor(text);
  char line[76];
  snprintf(line, sizeof(line), "Reply (local): %.44s  [tap to cancel]", g.pending_reply_preview);
  d.drawTextEllipsized(13, 187, 294, line);
}

uint64_t CommunicatorAppScreen::replyTargetForMessage(int slot) const {
  if (slot < 0 || slot >= kCacheSlots) return 0;
  return g.reply_to[slot];
}

bool CommunicatorAppScreen::getReplyTargetPreview(int slot, char* out, size_t len) const {
  if (!out || !len) return false;
  out[0] = 0;
  uint64_t target = replyTargetForMessage(slot);
  if (!target) return false;
  for (int i = 0; i < kCacheSlots; ++i) {
    if (g.ids[i] == target && (_messages[i].origin[0] || _messages[i].text[0])) {
      StrHelper::strncpy(out, _messages[i].text, len);
      return true;
    }
  }
  StrHelper::strncpy(out, "Original message unavailable", len);
  return true;
}

void CommunicatorAppScreen::redrawHeaderActionIcons(DisplayDriver& d) {
  const ColorVal card = _light_mode ? (ColorVal)0xFFFF : (ColorVal)0x08E6;
  const ColorVal divider = _light_mode ? (ColorVal)0xDED7 : (ColorVal)0x29CF;
  const ColorVal text = _light_mode ? (ColorVal)0x14E5 : (ColorVal)0xFFFF;
  const ColorVal wireless = (ColorVal)0x25CB;

  // Repaint the button surfaces so none of the old abstract glyph remains.
  d.setColor(card); d.fillRoundRect(238, 4, 36, 33, 6);
  d.setColor(divider); d.drawRoundRect(238, 4, 36, 33, 6);
  d.setColor(card); d.fillRoundRect(279, 4, 36, 33, 6);
  d.setColor(divider); d.drawRoundRect(279, 4, 36, 33, 6);

  // Material/Wi-Fi-like radio glyph: three progressively smaller arcs + dot.
  const int cx = 256, cy = 19;
  d.setColor(wireless);
  d.drawLine(cx-12,cy-7,cx-8,cy-10); d.drawLine(cx-8,cy-10,cx-4,cy-12);
  d.drawLine(cx-4,cy-12,cx,cy-13); d.drawLine(cx,cy-13,cx+4,cy-12);
  d.drawLine(cx+4,cy-12,cx+8,cy-10); d.drawLine(cx+8,cy-10,cx+12,cy-7);
  d.drawLine(cx-8,cy-2,cx-4,cy-5); d.drawLine(cx-4,cy-5,cx,cy-6);
  d.drawLine(cx,cy-6,cx+4,cy-5); d.drawLine(cx+4,cy-5,cx+8,cy-2);
  d.drawLine(cx-4,cy+3,cx,cy+1); d.drawLine(cx,cy+1,cx+4,cy+3);
  d.fillCircle(cx,cy+8,2);

  // Compact Android-style settings gear: center ring with eight teeth.
  const int gx = 297, gy = 20;
  d.setColor(text);
  d.drawCircle(gx,gy,7); d.drawCircle(gx,gy,3);
  d.drawLine(gx,gy-11,gx,gy-7); d.drawLine(gx,gy+7,gx,gy+11);
  d.drawLine(gx-11,gy,gx-7,gy); d.drawLine(gx+7,gy,gx+11,gy);
  d.drawLine(gx-8,gy-8,gx-5,gy-5); d.drawLine(gx+5,gy+5,gx+8,gy+8);
  d.drawLine(gx+5,gy-5,gx+8,gy-8); d.drawLine(gx-8,gy+8,gx-5,gy+5);
}

bool CommunicatorAppScreen::handlePersistentDataTouch(int16_t x, int16_t y, uint8_t gesture) {
  (void)x;
  if (gesture != COMPACT_TOUCH_TAP || _route != ROUTE_SETTINGS) return false;
  // Data & backup is row 4 on the Settings root. Piece 3 now provides durable
  // history/drafts; export/encryption remain later roadmap work.
  if (y >= 179 && y < 204) {
    openFeature("Data & backup",
      "Message history, unread state, send metadata and drafts now persist in a versioned append journal on internal SPIFFS. A corrupt or partial tail is recovered and the journal is bounded by compaction. SD export/import and AES-GCM backup encryption are later backup/security work.");
    return true;
  }
  return false;
}

void CommunicatorAppScreen::persistenceBegin() {
  static_assert(kCacheSlots == 96, "Piece 3 persistence slot count must match CommunicatorAppScreen MESSAGE_CACHE");
  memset(&g, 0, sizeof(g));
  g.next_id = 1;
  g.storage_ok = true;
  g.begun = true;

  loadContactsAndChannels();
  loadDrafts();

  if (!SPIFFS.exists(kHistoryPath)) return;
  File f = SPIFFS.open(kHistoryPath, FILE_READ);
  if (!f) { g.storage_ok = false; return; }

  HistoryHeader h{};
  if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h) || !validHistoryHeader(h)) {
    f.close();
    SPIFFS.remove(kHistoryBad);
    SPIFFS.rename(kHistoryPath, kHistoryBad);
    g.storage_ok = false;
    return;
  }

  size_t pos = sizeof(h), total = f.size();
  while (pos + sizeof(HistoryRecord) <= total) {
    if (!f.seek(pos)) { g.needs_compact = true; break; }
    HistoryRecord r{};
    if (f.read((uint8_t*)&r, sizeof(r)) != sizeof(r) || !validRecord(r)) { g.needs_compact = true; break; }
    pos += sizeof(r);
    if (r.id >= g.next_id) g.next_id = r.id + 1;

    int slot = -1;
    for (int i = 0; i < kCacheSlots; ++i) if (g.ids[i] == r.id) { slot = i; break; }
    if (r.flags & 0x04) {
      if (slot >= 0) {
        memset(&_messages[slot], 0, sizeof(_messages[slot]));
        g.ids[slot] = 0; g.content_hash[slot] = g.persist_hash[slot] = 0;
        g.conv_kind[slot] = 0; memset(g.conv_key[slot], 0, 32); g.reply_to[slot] = 0;
      }
      continue;
    }

    if (slot < 0) {
      _message_head = (_message_head + 1) % kCacheSlots;
      slot = _message_head;
      g.ids[slot] = r.id;
      if (_message_count < kCacheSlots) ++_message_count;
    }

    MessageEntry& m = _messages[slot];
    memset(&m, 0, sizeof(m));
    m.timestamp = r.timestamp;
    m.path_len = r.path_len;
    m.unread = (r.flags & 0x02) ? 1 : 0;
    m.send_state = r.send_state;
    m.outgoing = (r.flags & 0x01) != 0;
    StrHelper::strncpy(m.origin, r.origin, sizeof(m.origin));
    StrHelper::strncpy(m.text, r.text, sizeof(m.text));

    // Resolve stable keys back to the current radio name. A contact/channel can
    // be renamed without orphaning its persisted conversation.
    if (r.conv_kind == 1) {
      for (int i = 0; i < _contact_count; ++i) if (memcmp(_contacts[i].id.pub_key, r.conv_key, 32) == 0) {
        StrHelper::strncpy(m.origin, _contacts[i].name, sizeof(m.origin)); break;
      }
    } else if (r.conv_kind == 2) {
      for (int i = 0; i < _channel_count; ++i) if (memcmp(_channels[i].channel.secret, r.conv_key, 32) == 0) {
        StrHelper::strncpy(m.origin, _channels[i].name, sizeof(m.origin)); break;
      }
    }

    g.conv_kind[slot] = r.conv_kind;
    memcpy(g.conv_key[slot], r.conv_key, 32);
    g.reply_to[slot] = r.reply_to;
    g.content_hash[slot] = messageContentHash(m.timestamp, m.outgoing, m.origin, m.text);
    g.persist_hash[slot] = messagePersistHash(g.content_hash[slot], m.unread, m.send_state, m.path_len,
                                               g.conv_kind[slot], g.conv_key[slot], g.reply_to[slot]);
  }
  if (pos != total) g.needs_compact = true; // partial write/corrupt tail: keep valid prefix
  f.close();

  // Existing UI loops already tolerate empty holes in the ring. Keeping the
  // loaded span at the bounded limit ensures tombstones never hide older live
  // records behind a deleted slot.
  if (_message_count) _message_count = kCacheSlots;
  _dirty = DIRTY_ALL;
}

void CommunicatorAppScreen::persistenceCheckpoint(bool force) {
  if (!g.begun) return;
  unsigned long now = millis();
  if (!force && now - g.last_checkpoint < 1250) return;
  g.last_checkpoint = now;

  loadContactsAndChannels();

  auto resolveIdentity = [&](const MessageEntry& m, uint8_t& kind, uint8_t key[32]) {
    kind = 0; memset(key, 0, 32);
    if (m.outgoing && strcmp(m.origin, _active_name) == 0) {
      if (_active_kind == ROW_CONTACT) { kind = 1; memcpy(key, _active_contact.id.pub_key, 32); return; }
      if (_active_kind == ROW_CHANNEL) { kind = 2; memcpy(key, _active_channel.channel.secret, 32); return; }
    }
    int ci = findContactByName(m.origin);
    if (ci >= 0) { kind = 1; memcpy(key, _contacts[ci].id.pub_key, 32); return; }
    int gi = findChannelByName(m.origin);
    if (gi >= 0) { kind = 2; memcpy(key, _channels[gi].channel.secret, 32); return; }
    kind = 3; fallbackKey(m.origin, key);
  };

  // If a reply reference is pending, attach it to the newest newly-created
  // outgoing message in the active conversation. beginReplyToMessage() forced
  // all pre-existing messages to have IDs, so this cannot bind to an older one.
  int pending_reply_slot = -1;
  if (g.pending_reply_to && _route == ROUTE_CHAT) {
    for (int n = 0; n < _message_count; ++n) {
      int i = (_message_head + kCacheSlots - n) % kCacheSlots;
      MessageEntry& m = _messages[i];
      if (!m.outgoing || !m.origin[0] || strcmp(m.origin, _active_name) != 0) continue;
      uint32_t content = messageContentHash(m.timestamp, m.outgoing, m.origin, m.text);
      bool new_content = g.ids[i] == 0 || (g.content_hash[i] != 0 && content != g.content_hash[i]);
      if (new_content) { pending_reply_slot = i; break; }
    }
  }

  auto appendSlot = [&](int i, bool deleted) -> bool {
    HistoryRecord r{};
    r.id = g.ids[i];
    r.reply_to = g.reply_to[i];
    r.timestamp = _messages[i].timestamp;
    r.conv_kind = g.conv_kind[i];
    r.flags = deleted ? 0x04 : ((_messages[i].outgoing ? 0x01 : 0) | (_messages[i].unread ? 0x02 : 0));
    r.send_state = _messages[i].send_state;
    r.path_len = _messages[i].path_len;
    memcpy(r.conv_key, g.conv_key[i], 32);
    StrHelper::strncpy(r.origin, _messages[i].origin, sizeof(r.origin));
    StrHelper::strncpy(r.text, _messages[i].text, sizeof(r.text));
    bool ok = appendHistoryRecord(r);
    if (!ok) g.storage_ok = false;
    return ok;
  };

  for (int i = 0; i < kCacheSlots; ++i) {
    MessageEntry& m = _messages[i];
    bool empty = !m.origin[0] && !m.text[0];
    if (empty) {
      if (g.ids[i] != 0) {
        if (appendSlot(i, true)) {
          g.ids[i] = 0; g.content_hash[i] = g.persist_hash[i] = 0; g.conv_kind[i] = 0;
          memset(g.conv_key[i], 0, 32); g.reply_to[i] = 0;
        }
      }
      continue;
    }

    uint32_t content = messageContentHash(m.timestamp, m.outgoing, m.origin, m.text);
    if (g.ids[i] != 0 && g.content_hash[i] != 0 && content != g.content_hash[i]) {
      // Ring slot was reused by the bounded RAM cache. Tombstone the previous
      // message before assigning a new stable ID to this content.
      if (!appendSlot(i, true)) continue;
      g.ids[i] = 0; g.persist_hash[i] = 0; g.conv_kind[i] = 0; g.reply_to[i] = 0;
      memset(g.conv_key[i], 0, 32);
    }

    if (g.ids[i] == 0) {
      g.ids[i] = g.next_id++;
      resolveIdentity(m, g.conv_kind[i], g.conv_key[i]);
      g.reply_to[i] = (i == pending_reply_slot) ? g.pending_reply_to : 0;
      if (i == pending_reply_slot) {
        g.pending_reply_to = 0;
        g.pending_reply_preview[0] = 0;
        pending_reply_slot = -1;
      }
    } else if (g.conv_kind[i] == 0) {
      resolveIdentity(m, g.conv_kind[i], g.conv_key[i]);
    }

    uint32_t persisted = messagePersistHash(content, m.unread, m.send_state, m.path_len,
                                             g.conv_kind[i], g.conv_key[i], g.reply_to[i]);
    if (persisted != g.persist_hash[i]) {
      if (appendSlot(i, false)) {
        g.content_hash[i] = content;
        g.persist_hash[i] = persisted;
      }
    }
  }

  // Drafts are keyed by the same stable contact/channel identity. Restore on
  // first entry to a conversation, then checkpoint edits at a throttled rate
  // and immediately before navigation/screen-off events (force=true).
  if (_route == ROUTE_CHAT && (_active_kind == ROW_CONTACT || _active_kind == ROW_CHANNEL)) {
    uint8_t kind = _active_kind == ROW_CONTACT ? 1 : 2;
    uint8_t key[32];
    if (kind == 1) memcpy(key, _active_contact.id.pub_key, 32); else memcpy(key, _active_channel.channel.secret, 32);
    bool sameActive = g.active_draft_valid && sameKey(g.active_draft_kind, g.active_draft_key, kind, key);
    if (!sameActive) {
      g.active_draft_valid = true; g.active_draft_kind = kind; memcpy(g.active_draft_key, key, 32);
      int di = findDraft(kind, key);
      if (di >= 0 && _compose_len == 0 && g.drafts[di].text[0]) {
        StrHelper::strncpy(_compose, g.drafts[di].text, sizeof(_compose));
        _compose_len = strlen(_compose);
        _dirty = DIRTY_COMPOSER;
      }
    }

    int di = findDraft(kind, key);
    bool changed = false;
    if (_compose_len == 0) {
      if (di >= 0) {
        for (int j = di; j + 1 < g.draft_count; ++j) g.drafts[j] = g.drafts[j + 1];
        --g.draft_count; changed = true;
      }
    } else {
      if (di < 0 && g.draft_count < kMaxDrafts) {
        di = g.draft_count++; memset(&g.drafts[di], 0, sizeof(DraftRecord));
        g.drafts[di].conv_kind = kind; memcpy(g.drafts[di].conv_key, key, 32); changed = true;
      }
      if (di >= 0 && strcmp(g.drafts[di].text, _compose) != 0) {
        StrHelper::strncpy(g.drafts[di].text, _compose, sizeof(g.drafts[di].text)); changed = true;
      }
    }
    if (changed && (force || now - g.last_draft_write >= 1250)) {
      if (!writeDraftsAtomic()) g.storage_ok = false;
      else g.last_draft_write = now;
    }
  } else {
    g.active_draft_valid = false;
  }

  bool compact = g.needs_compact;
  if (!compact && SPIFFS.exists(kHistoryPath)) {
    File f = SPIFFS.open(kHistoryPath, FILE_READ);
    if (f) { compact = f.size() >= kCompactAtBytes; f.close(); }
  }
  if (!compact) return;

  File out = SPIFFS.open(kHistoryTmp, FILE_WRITE);
  if (!out) { g.storage_ok = false; return; }
  HistoryHeader h = makeHistoryHeader();
  bool ok = out.write((const uint8_t*)&h, sizeof(h)) == sizeof(h);
  // Oldest to newest relative to the current ring head.
  for (int n = 1; ok && n <= kCacheSlots; ++n) {
    int i = (_message_head + n) % kCacheSlots;
    if (g.ids[i] == 0 || (!_messages[i].origin[0] && !_messages[i].text[0])) continue;
    HistoryRecord r{};
    r.magic = kRecordMagic; r.version = kSchemaVersion; r.size = sizeof(r); r.id = g.ids[i];
    r.reply_to = g.reply_to[i]; r.timestamp = _messages[i].timestamp; r.conv_kind = g.conv_kind[i];
    r.flags = (_messages[i].outgoing ? 0x01 : 0) | (_messages[i].unread ? 0x02 : 0);
    r.send_state = _messages[i].send_state; r.path_len = _messages[i].path_len;
    memcpy(r.conv_key, g.conv_key[i], 32);
    StrHelper::strncpy(r.origin, _messages[i].origin, sizeof(r.origin));
    StrHelper::strncpy(r.text, _messages[i].text, sizeof(r.text));
    r.crc = crc32((const uint8_t*)&r, offsetof(HistoryRecord, crc));
    ok = out.write((const uint8_t*)&r, sizeof(r)) == sizeof(r);
  }
  out.flush(); out.close();
  if (ok && replaceAtomically(kHistoryTmp, kHistoryPath, kHistoryBak)) g.needs_compact = false;
  else { SPIFFS.remove(kHistoryTmp); g.storage_ok = false; }
}