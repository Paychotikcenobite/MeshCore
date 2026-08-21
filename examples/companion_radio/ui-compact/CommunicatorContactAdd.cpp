#include "CommunicatorAppScreen.h"
#include "CommunicatorAdminCodec.h"
#include "UITask.h"
#include "../MyMesh.h"
#include <string.h>

namespace {

struct ContactAddState {
  bool active = false;
  uint8_t field = 0; // 0 optional name, 1 key/URI
  char name[32] = {0};
  uint8_t name_len = 0;
  char entry[224] = {0};
  uint16_t entry_len = 0;
};

ContactAddState g_contact;

ColorVal rgb565Local(uint8_t r, uint8_t g, uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void beginWizard() {
  memset(&g_contact, 0, sizeof(g_contact));
  g_contact.active = true;
}

} // namespace

void CommunicatorAppScreen::manualContactsBegin() {
  // Piece 5 removed the parallel Preferences-backed contact database. Contacts
  // created on the T-Deck now live in MeshCore's normal /contacts3 store and
  // therefore load through the same DataStore path as BLE/serial-created ones.
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
  if (x >= 238 && y >= 44 && y <= 78) {
    g_contact.active = false;
    openOwnContactQr();
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
      g_contact.field = 1;
      _dirty = DIRTY_ALL;
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
      g_contact.field = 1;
      _dirty = DIRTY_ALL;
      return true;
    }

    ContactInfo requested{};
    ContactInfo readback{};
    char error[72] = {0};
    if (!CompactAdminCodec::parseContactInput(g_contact.entry, g_contact.name,
                                               requested, error, sizeof(error))) {
      _task->showAlert(error, 1400);
      return true;
    }
    if (!the_mesh.compactUpsertContactVerified(requested, readback)) {
      _task->showAlert("Contact write/read-back failed", 1500);
      return true;
    }

    // This contact was explicitly added by the user, so it must become part of
    // the advert baseline rather than being mistaken for a newly heard RF node.
    syncAdvertContactBaseline();
    _task->showAlert("Contact saved + verified", 1000);
    g_contact.active = false;
    _selected = _list_offset = 0;
    _dirty = DIRTY_ALL;
    return true;
  }

  if (c == 8 || (uint8_t)c == 127) {
    if (g_contact.field == 0) {
      if (g_contact.name_len) g_contact.name[--g_contact.name_len] = 0;
    } else {
      if (g_contact.entry_len) g_contact.entry[--g_contact.entry_len] = 0;
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
  } else if (g_contact.entry_len < sizeof(g_contact.entry) - 1) {
    g_contact.entry[g_contact.entry_len++] = c;
    g_contact.entry[g_contact.entry_len] = 0;
    _dirty = DIRTY_ALL;
  }
  return true;
}

void CommunicatorAppScreen::drawContactAddOverlay(DisplayDriver& d) {
  if (!g_contact.active) return;
  fillScreen(d);
  drawDetailTitle(d, "Add contact", "verified");

  const ColorVal input = _light_mode ? rgb565Local(237,243,249) : rgb565Local(27,57,91);
  const ColorVal text = _light_mode ? rgb565Local(20,28,40) : rgb565Local(255,255,255);
  const ColorVal sub = _light_mode ? rgb565Local(92,108,128) : rgb565Local(166,185,207);
  const ColorVal accent = _light_mode ? rgb565Local(11,58,117) : rgb565Local(70,145,255);
  const ColorVal stroke = _light_mode ? rgb565Local(176,198,221) : rgb565Local(55,88,124);

  drawButton(d, 240, 45, 72, 29, "My QR", false, true);

  d.setColor(input); d.fillRoundRect(8, 82, 304, 39, 7);
  d.setColor(g_contact.field == 0 ? accent : stroke); d.drawRoundRect(8, 82, 304, 39, 7);
  d.setTextSize(1); d.setColor(sub); d.setCursor(17, 87); d.print("Name - optional when URI includes one");
  d.setColor(g_contact.name_len ? text : sub);
  d.drawTextEllipsized(17, 103, 280, g_contact.name_len ? g_contact.name : "Name for a raw public key");

  d.setColor(input); d.fillRoundRect(8, 126, 304, 69, 7);
  d.setColor(g_contact.field == 1 ? accent : stroke); d.drawRoundRect(8, 126, 304, 69, 7);
  d.setColor(sub); d.setCursor(17, 131); d.print("Public key or meshcore://contact/add URI");
  if (!g_contact.entry_len) {
    d.setColor(sub); d.setCursor(17, 151); d.print("64 hex digits, or paste/type contact link");
  } else {
    d.setColor(text);
    char a[49] = {0}, b[49] = {0}, c[49] = {0};
    strncpy(a, g_contact.entry, 48);
    if (g_contact.entry_len > 48) strncpy(b, g_contact.entry + 48, 48);
    if (g_contact.entry_len > 96) strncpy(c, g_contact.entry + 96, 48);
    d.drawTextEllipsized(17, 148, 285, a);
    if (b[0]) d.drawTextEllipsized(17, 162, 285, b);
    if (c[0]) d.drawTextEllipsized(17, 176, 285, c);
  }

  d.setColor(sub);
  d.drawTextCentered(160, 198, g_contact.field == 0 ? "Enter continues" : "Enter writes and verifies MeshCore storage");
  drawButton(d, 8, 211, 145, 25, "Cancel", false, true);
  drawButton(d, 167, 211, 145, 25, g_contact.field == 0 ? "Next" : "Save contact", true, true);
}