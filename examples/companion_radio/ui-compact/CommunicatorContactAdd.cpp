#include "CommunicatorAppScreen.h"
#include "UITask.h"
#include "../MyMesh.h"
#include <Preferences.h>
#include <helpers/TxtDataHelpers.h>
#include <string.h>
#include <stddef.h>

namespace {

constexpr uint32_t kManualMagic = 0x314E434D; // MCN1
constexpr uint16_t kManualVersion = 1;
constexpr int kMaxManualContacts = 20;

#pragma pack(push, 1)
struct ManualContactRecord {
  uint8_t pub_key[32];
  char name[32];
  uint32_t crc;
};
struct ManualContactBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  ManualContactRecord records[kMaxManualContacts];
  uint32_t crc;
};
#pragma pack(pop)

struct ContactAddState {
  bool active = false;
  uint8_t field = 0; // 0 name, 1 public key
  char name[32] = {0};
  uint8_t name_len = 0;
  char key_hex[65] = {0};
  uint8_t key_len = 0;
  ManualContactBlob store{};
  bool store_loaded = false;
};

ContactAddState g_contact;

uint32_t crc32Local(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
  }
  return ~crc;
}

uint32_t recordCrc(const ManualContactRecord& r) {
  return crc32Local((const uint8_t*)&r, offsetof(ManualContactRecord, crc));
}

uint32_t blobCrc(const ManualContactBlob& b) {
  return crc32Local((const uint8_t*)&b, offsetof(ManualContactBlob, crc));
}

void resetStore() {
  memset(&g_contact.store, 0, sizeof(g_contact.store));
  g_contact.store.magic = kManualMagic;
  g_contact.store.version = kManualVersion;
  g_contact.store.count = 0;
  g_contact.store.crc = blobCrc(g_contact.store);
}

bool validStore(const ManualContactBlob& b) {
  if (b.magic != kManualMagic || b.version != kManualVersion || b.count > kMaxManualContacts) return false;
  if (b.crc != blobCrc(b)) return false;
  for (int i = 0; i < b.count; ++i) {
    if (!b.records[i].name[0] || b.records[i].crc != recordCrc(b.records[i])) return false;
  }
  return true;
}

void loadStore() {
  if (g_contact.store_loaded) return;
  g_contact.store_loaded = true;
  resetStore();
  Preferences p;
  if (!p.begin("mcccontacts", true)) return;
  size_t n = p.getBytesLength("contacts");
  if (n == sizeof(ManualContactBlob)) {
    ManualContactBlob tmp{};
    if (p.getBytes("contacts", &tmp, sizeof(tmp)) == sizeof(tmp) && validStore(tmp)) g_contact.store = tmp;
  }
  p.end();
}

bool saveStore() {
  g_contact.store.magic = kManualMagic;
  g_contact.store.version = kManualVersion;
  for (int i = 0; i < g_contact.store.count; ++i) g_contact.store.records[i].crc = recordCrc(g_contact.store.records[i]);
  g_contact.store.crc = blobCrc(g_contact.store);
  Preferences p;
  if (!p.begin("mcccontacts", false)) return false;
  bool ok = p.putBytes("contacts", &g_contact.store, sizeof(g_contact.store)) == sizeof(g_contact.store);
  p.end();
  return ok;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseKey(const char* hex, uint8_t out[32]) {
  if (!hex || strlen(hex) != 64) return false;
  for (int i = 0; i < 32; ++i) {
    int hi = hexNibble(hex[i * 2]);
    int lo = hexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

int findManual(const uint8_t key[32]) {
  loadStore();
  for (int i = 0; i < g_contact.store.count; ++i) {
    if (memcmp(g_contact.store.records[i].pub_key, key, 32) == 0) return i;
  }
  return -1;
}

bool persistManual(const uint8_t key[32], const char* name) {
  loadStore();
  int idx = findManual(key);
  if (idx < 0) {
    if (g_contact.store.count >= kMaxManualContacts) return false;
    idx = g_contact.store.count++;
    memset(&g_contact.store.records[idx], 0, sizeof(g_contact.store.records[idx]));
    memcpy(g_contact.store.records[idx].pub_key, key, 32);
  }
  StrHelper::strncpy(g_contact.store.records[idx].name, name, sizeof(g_contact.store.records[idx].name));
  return saveStore();
}

ColorVal rgb565Local(uint8_t r, uint8_t g, uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void beginWizard() {
  g_contact.active = true;
  g_contact.field = 0;
  g_contact.name[0] = 0;
  g_contact.name_len = 0;
  g_contact.key_hex[0] = 0;
  g_contact.key_len = 0;
}

} // namespace

void CommunicatorAppScreen::manualContactsBegin() {
  loadStore();
  for (int i = 0; i < g_contact.store.count; ++i) {
    const ManualContactRecord& r = g_contact.store.records[i];
    ContactInfo* existing = the_mesh.lookupContactByPubKey(r.pub_key, 32);
    if (existing) continue;
    ContactInfo c{};
    memcpy(c.id.pub_key, r.pub_key, 32);
    StrHelper::strncpy(c.name, r.name, sizeof(c.name));
    c.type = ADV_TYPE_CHAT;
    c.flags = 0;
    c.out_path_len = OUT_PATH_UNKNOWN;
    c.last_advert_timestamp = 0;
    c.lastmod = _rtc->getCurrentTime();
    c.gps_lat = 0;
    c.gps_lon = 0;
    c.sync_since = 0;
    if (!the_mesh.addContact(c)) {
      Serial.printf("[compact-contact] unable to rehydrate manual contact %s\n", r.name);
      break;
    }
  }
}

bool CommunicatorAppScreen::tryBeginContactAdd(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture != COMPACT_TOUCH_TAP || _route != ROUTE_NEW_CONVERSATION) return false;
  if (y >= 79 && y <= 109 && x < 108) {
    beginWizard();
    _dirty = DIRTY_ALL;
    return true;
  }
  return false;
}

bool CommunicatorAppScreen::contactAddActive() const { return g_contact.active; }

bool CommunicatorAppScreen::handleContactAddTouch(int16_t x, int16_t y, uint8_t gesture) {
  if (!g_contact.active || gesture != COMPACT_TOUCH_TAP) return false;
  if (x < 42 && y >= 44 && y <= 78) {
    g_contact.active = false;
    _dirty = DIRTY_ALL;
    return true;
  }
  if (y >= 82 && y <= 121) {
    g_contact.field = 0;
    _dirty = DIRTY_ALL;
    return true;
  }
  if (y >= 126 && y <= 194) {
    g_contact.field = 1;
    _dirty = DIRTY_ALL;
    return true;
  }
  if (y >= 207) {
    if (x < 158) {
      g_contact.active = false;
      _dirty = DIRTY_ALL;
      return true;
    }
    if (g_contact.field == 0) {
      if (!g_contact.name_len) { _task->showAlert("Enter a contact name", 900); return true; }
      g_contact.field = 1;
      _dirty = DIRTY_ALL;
      return true;
    }
    if (g_contact.key_len != 64) {
      _task->showAlert("Public key needs 64 hex digits", 1200);
      return true;
    }
    return handleContactAddInput(KEY_ENTER);
  }
  return true;
}

bool CommunicatorAppScreen::handleContactAddInput(char c) {
  if (!g_contact.active) return false;
  if (c == KEY_CANCEL) {
    g_contact.active = false;
    _dirty = DIRTY_ALL;
    return true;
  }
  if (c == KEY_ENTER) {
    if (g_contact.field == 0) {
      if (!g_contact.name_len) { _task->showAlert("Enter a contact name", 900); return true; }
      g_contact.field = 1;
      _dirty = DIRTY_ALL;
      return true;
    }
    if (!g_contact.name_len) { _task->showAlert("Enter a contact name", 900); g_contact.field = 0; return true; }
    if (g_contact.key_len != 64) { _task->showAlert("Public key needs 64 hex digits", 1200); return true; }

    uint8_t key[32];
    if (!parseKey(g_contact.key_hex, key)) { _task->showAlert("Invalid public key", 1000); return true; }

    ContactInfo* existing = the_mesh.lookupContactByPubKey(key, 32);
    bool added = false;
    if (existing) {
      StrHelper::strncpy(existing->name, g_contact.name, sizeof(existing->name));
      existing->type = ADV_TYPE_CHAT;
      existing->lastmod = _rtc->getCurrentTime();
      added = true;
    } else {
      ContactInfo ci{};
      memcpy(ci.id.pub_key, key, 32);
      StrHelper::strncpy(ci.name, g_contact.name, sizeof(ci.name));
      ci.type = ADV_TYPE_CHAT;
      ci.flags = 0;
      ci.out_path_len = OUT_PATH_UNKNOWN;
      ci.last_advert_timestamp = 0;
      ci.lastmod = _rtc->getCurrentTime();
      ci.gps_lat = 0;
      ci.gps_lon = 0;
      ci.sync_since = 0;
      added = the_mesh.addContact(ci);
    }
    if (!added) { _task->showAlert("Contact list is full", 1100); return true; }
    if (!persistManual(key, g_contact.name)) {
      _task->showAlert("Contact added; local save failed", 1400);
    } else {
      _task->showAlert(existing ? "Contact updated" : "Contact added", 850);
    }
    g_contact.active = false;
    _selected = _list_offset = 0;
    _dirty = DIRTY_ALL;
    return true;
  }

  if (c == 8 || (uint8_t)c == 127) {
    if (g_contact.field == 0) {
      if (g_contact.name_len) g_contact.name[--g_contact.name_len] = 0;
    } else {
      if (g_contact.key_len) g_contact.key_hex[--g_contact.key_len] = 0;
    }
    _dirty = DIRTY_ALL;
    return true;
  }

  if ((uint8_t)c < 32 || (uint8_t)c > 126) return true;
  if (g_contact.field == 0) {
    if (g_contact.name_len < sizeof(g_contact.name) - 1) {
      g_contact.name[g_contact.name_len++] = c;
      g_contact.name[g_contact.name_len] = 0;
      _dirty = DIRTY_ALL;
    }
    return true;
  }

  int n = hexNibble(c);
  if (n >= 0 && g_contact.key_len < 64) {
    g_contact.key_hex[g_contact.key_len++] = "0123456789abcdef"[n];
    g_contact.key_hex[g_contact.key_len] = 0;
    _dirty = DIRTY_ALL;
  }
  return true;
}

void CommunicatorAppScreen::drawContactAddOverlay(DisplayDriver& d) {
  if (!g_contact.active) return;
  fillScreen(d);
  drawDetailTitle(d, "Add contact", "standalone");

  const ColorVal card = _light_mode ? rgb565Local(255,255,255) : rgb565Local(12,29,50);
  const ColorVal input = _light_mode ? rgb565Local(237,243,249) : rgb565Local(27,57,91);
  const ColorVal text = _light_mode ? rgb565Local(20,28,40) : rgb565Local(255,255,255);
  const ColorVal sub = _light_mode ? rgb565Local(92,108,128) : rgb565Local(166,185,207);
  const ColorVal accent = _light_mode ? rgb565Local(11,58,117) : rgb565Local(70,145,255);
  const ColorVal stroke = _light_mode ? rgb565Local(176,198,221) : rgb565Local(55,88,124);

  d.setColor(input); d.fillRoundRect(8, 82, 304, 39, 7);
  d.setColor(g_contact.field == 0 ? accent : stroke); d.drawRoundRect(8, 82, 304, 39, 7);
  d.setTextSize(1); d.setColor(sub); d.setCursor(17, 87); d.print("Name");
  d.setColor(g_contact.name_len ? text : sub);
  d.drawTextEllipsized(17, 103, 280, g_contact.name_len ? g_contact.name : "Type a local display name");

  d.setColor(input); d.fillRoundRect(8, 126, 304, 69, 7);
  d.setColor(g_contact.field == 1 ? accent : stroke); d.drawRoundRect(8, 126, 304, 69, 7);
  d.setColor(sub); d.setCursor(17, 131); d.print("Public key - 32 bytes / 64 hex digits");
  char line[17];
  for (int row = 0; row < 4; ++row) {
    int start = row * 16;
    int n = 0;
    while (n < 16 && start + n < g_contact.key_len) { line[n] = g_contact.key_hex[start + n]; ++n; }
    while (n < 16) line[n++] = '_';
    line[16] = 0;
    d.setColor(g_contact.key_len ? text : sub);
    d.setCursor(18, 148 + row * 11);
    d.print(line);
    if (row < 3) {
      d.setColor(sub);
      char count[12]; snprintf(count, sizeof(count), "%u/64", g_contact.key_len);
      d.drawTextRightAlign(300, 148, count);
    }
  }

  d.setColor(sub);
  d.drawTextCentered(160, 198, g_contact.field == 0 ? "Type name, then press Enter" : "Type the full public key; Enter saves");
  drawButton(d, 8, 211, 145, 25, "Cancel", false, true);
  drawButton(d, 167, 211, 145, 25, g_contact.field == 0 ? "Next" : "Save contact", true, true);
}
