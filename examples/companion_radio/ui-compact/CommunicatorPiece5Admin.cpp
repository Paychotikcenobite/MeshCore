#include "CommunicatorAppScreen.h"
#include "CommunicatorAdminCodec.h"
#include "UITask.h"
#include "../MyMesh.h"

#include <Preferences.h>
#include <qrcode.h>
#include <string.h>

namespace {

enum AdminMode : uint8_t {
  ADMIN_NONE = 0,
  ADMIN_GROUP_HOME,
  ADMIN_GROUP_CREATE,
  ADMIN_GROUP_JOIN,
  ADMIN_GROUP_MANAGE,
  ADMIN_GROUP_RENAME,
  ADMIN_GROUP_LEAVE,
  ADMIN_CONTACT_MANAGE,
  ADMIN_CONTACT_RENAME,
  ADMIN_CONTACT_REMOVE,
  ADMIN_QR,
};

struct AdminState {
  bool active = false;
  AdminMode mode = ADMIN_NONE;
  uint8_t selected = 0;
  uint8_t offset = 0;
  char input[224] = {0};
  uint16_t input_len = 0;
  char uri[320] = {0};
  char qr_title[32] = {0};
  bool qr_exportable = false;
};

struct GroupExtra {
  uint8_t color = 0;
  uint8_t location_share = 0;
};

AdminState g_admin;

ColorVal rgb565p5(uint8_t r, uint8_t g, uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void resetAdmin(AdminMode mode) {
  memset(&g_admin, 0, sizeof(g_admin));
  g_admin.active = true;
  g_admin.mode = mode;
}

void groupExtraKey(const ChannelDetails& ch, uint8_t idx, char out[16]) {
  snprintf(out, 16, "g%02u%02x%02x%02x%02x", idx,
           ch.channel.secret[0], ch.channel.secret[1], ch.channel.secret[2], ch.channel.secret[3]);
}

GroupExtra loadGroupExtra(const ChannelDetails& ch, uint8_t idx) {
  GroupExtra extra{};
  if (idx == 0) return extra;
  char key[16]; groupExtraKey(ch, idx, key);
  Preferences p;
  if (!p.begin("mccgrp5", true)) return extra;
  if (p.getBytesLength(key) == sizeof(extra)) p.getBytes(key, &extra, sizeof(extra));
  p.end();
  if (extra.color > 4) extra.color = 0;
  extra.location_share = extra.location_share ? 1 : 0;
  return extra;
}

void saveGroupExtra(const ChannelDetails& ch, uint8_t idx, const GroupExtra& extra) {
  if (idx == 0) return;
  char key[16]; groupExtraKey(ch, idx, key);
  Preferences p;
  if (!p.begin("mccgrp5", false)) return;
  p.putBytes(key, &extra, sizeof(extra));
  p.end();
}

const char* colorName(uint8_t color) {
  static const char* names[] = {"Default", "Blue", "Green", "Purple", "Orange"};
  return names[color <= 4 ? color : 0];
}

ColorVal colorValue(uint8_t color, bool light) {
  switch (color) {
    case 1: return rgb565p5(70, 145, 255);
    case 2: return rgb565p5(52, 199, 89);
    case 3: return rgb565p5(175, 110, 255);
    case 4: return rgb565p5(255, 149, 0);
    default: return light ? rgb565p5(11, 58, 117) : rgb565p5(70, 145, 255);
  }
}

void makePrivateGroup(const char* name, ChannelDetails& ch) {
  memset(&ch, 0, sizeof(ch));
  StrHelper::strncpy(ch.name, name, sizeof(ch.name));
  the_mesh.getRNG()->random(ch.channel.secret, 16);
  memset(&ch.channel.secret[16], 0, 16);
}

} // namespace

bool CommunicatorAppScreen::piece5AdminActive() const { return g_admin.active; }

void CommunicatorAppScreen::openOwnContactQr() {
  resetAdmin(ADMIN_QR);
  StrHelper::strncpy(g_admin.qr_title, "My contact QR", sizeof(g_admin.qr_title));
  const char* name = the_mesh.getNodeName();
  if (!name || !name[0]) name = "T-Deck";
  if (!CompactAdminCodec::makeContactUri(name, the_mesh.self_id.pub_key, ADV_TYPE_CHAT,
                                          g_admin.uri, sizeof(g_admin.uri))) {
    g_admin.uri[0] = 0;
  }
  g_admin.qr_exportable = true;
  _dirty = DIRTY_ALL;
}

bool CommunicatorAppScreen::tryBeginPiece5Admin(int16_t x, int16_t y, uint8_t gesture) {
  if (gesture != COMPACT_TOUCH_TAP) return false;

  if (_route == ROUTE_NEW_CONVERSATION && y >= 79 && y <= 109 && x >= 108 && x < 213) {
    resetAdmin(ADMIN_GROUP_HOME);
    _dirty = DIRTY_ALL;
    return true;
  }

  if (_route == ROUTE_CONVERSATION_DETAILS && y >= 178 && y <= 226 && x >= 198) {
    if (_active_kind == ROW_CHANNEL) resetAdmin(ADMIN_GROUP_MANAGE);
    else if (_active_kind == ROW_CONTACT) resetAdmin(ADMIN_CONTACT_MANAGE);
    else return false;
    _dirty = DIRTY_ALL;
    return true;
  }
  return false;
}

void CommunicatorAppScreen::drawPiece5Affordances(DisplayDriver& d) {
  if (g_admin.active || _route != ROUTE_CONVERSATION_DETAILS) return;
  if (_active_kind != ROW_CONTACT && _active_kind != ROW_CHANNEL) return;
  drawButton(d, 206, 187, 101, 28, _active_kind == ROW_CHANNEL ? "Manage group" : "Manage", true, true);
  if (_active_kind == ROW_CHANNEL && _active_channel_index > 0) {
    GroupExtra extra = loadGroupExtra(_active_channel, _active_channel_index);
    d.setColor(colorValue(extra.color, _light_mode));
    d.fillCircle(197, 201, 5);
  }
}

void CommunicatorAppScreen::drawPiece5AdminOverlay(DisplayDriver& d) {
  if (!g_admin.active) return;
  fillScreen(d);

  if (g_admin.mode == ADMIN_QR) {
    drawDetailTitle(d, g_admin.qr_title, "MeshCore URI");
    if (!g_admin.uri[0]) {
      d.setColor(_light_mode ? rgb565p5(70,70,70) : rgb565p5(200,210,220));
      d.drawTextCentered(160, 130, "Unable to create compatible QR");
    } else {
      QRCode qr;
      uint8_t data[512];
      int rc = qrcode_initText(&qr, data, 9, ECC_LOW, g_admin.uri);
      if (rc < 0) {
        d.setColor(_light_mode ? rgb565p5(70,70,70) : rgb565p5(200,210,220));
        d.drawTextCentered(160, 130, "QR payload is too large");
      } else {
        const int scale = 2, quiet = 4;
        const int side = (qr.size + quiet * 2) * scale;
        const int left = (320 - side) / 2;
        const int top = 82;
        d.setColor(0xFFFF); d.fillRect(left, top, side, side);
        d.setColor(0x0000);
        for (uint8_t yy = 0; yy < qr.size; ++yy) {
          for (uint8_t xx = 0; xx < qr.size; ++xx) {
            if (qrcode_getModule(&qr, xx, yy))
              d.fillRect(left + (xx + quiet) * scale, top + (yy + quiet) * scale, scale, scale);
          }
        }
      }
    }
    drawButton(d, 8, 214, 145, 22, "Back", false, true);
    drawButton(d, 167, 214, 145, 22, "Export USB", false, g_admin.qr_exportable && g_admin.uri[0]);
    return;
  }

  const char* title = "Contacts & groups";
  if (g_admin.mode == ADMIN_GROUP_HOME) title = "Private groups";
  else if (g_admin.mode == ADMIN_GROUP_CREATE) title = "Create private group";
  else if (g_admin.mode == ADMIN_GROUP_JOIN) title = "Join private group";
  else if (g_admin.mode == ADMIN_GROUP_MANAGE) title = _active_channel_index == 0 ? "Public / World" : "Manage group";
  else if (g_admin.mode == ADMIN_GROUP_RENAME) title = "Radio group name";
  else if (g_admin.mode == ADMIN_GROUP_LEAVE) title = "Leave private group";
  else if (g_admin.mode == ADMIN_CONTACT_MANAGE) title = "Manage contact";
  else if (g_admin.mode == ADMIN_CONTACT_RENAME) title = "Contact radio name";
  else if (g_admin.mode == ADMIN_CONTACT_REMOVE) title = "Remove contact";
  drawDetailTitle(d, title, "Piece 5");

  const ColorVal text = _light_mode ? rgb565p5(20,28,40) : 0xFFFF;
  const ColorVal sub = _light_mode ? rgb565p5(92,108,128) : rgb565p5(166,185,207);
  const ColorVal card = _light_mode ? 0xFFFF : rgb565p5(12,29,50);
  const ColorVal accent = colorValue(0, _light_mode);

  if (g_admin.mode == ADMIN_GROUP_CREATE || g_admin.mode == ADMIN_GROUP_JOIN ||
      g_admin.mode == ADMIN_GROUP_RENAME || g_admin.mode == ADMIN_CONTACT_RENAME) {
    d.setColor(card); d.fillRoundRect(8, 84, 304, 105, 7);
    d.setColor(accent); d.drawRoundRect(8, 84, 304, 105, 7);
    d.setColor(sub); d.setTextSize(1); d.setCursor(17, 91);
    if (g_admin.mode == ADMIN_GROUP_JOIN) d.print("meshcore://channel/add URI");
    else d.print("Type with physical keyboard");
    d.setColor(g_admin.input_len ? text : sub);
    if (!g_admin.input_len) {
      const char* hint = g_admin.mode == ADMIN_GROUP_JOIN ? "Paste/type the full group invitation link" : "Enter a name";
      d.drawTextEllipsized(17, 113, 280, hint);
    } else {
      for (int row = 0; row < 5; ++row) {
        int start = row * 46;
        if (start >= g_admin.input_len) break;
        char line[47] = {0};
        strncpy(line, g_admin.input + start, 46);
        d.drawTextEllipsized(17, 110 + row * 14, 280, line);
      }
    }
    drawButton(d, 8, 207, 145, 29, "Cancel", false, true);
    drawButton(d, 167, 207, 145, 29, "Save", true, true);
    return;
  }

  if (g_admin.mode == ADMIN_GROUP_LEAVE || g_admin.mode == ADMIN_CONTACT_REMOVE) {
    d.setColor(card); d.fillRoundRect(8, 88, 304, 93, 7);
    d.setColor(text); d.setTextSize(1);
    d.drawTextCentered(160, 106, g_admin.mode == ADMIN_GROUP_LEAVE ? "Remove this private group from the radio?" : "Remove this contact from the radio?");
    d.setColor(sub);
    d.drawTextCentered(160, 132, "Local message history is not deleted.");
    d.drawTextCentered(160, 149, "The persisted MeshCore record is verified after write.");
    drawButton(d, 8, 201, 145, 32, "Cancel", false, true);
    drawButton(d, 167, 201, 145, 32, g_admin.mode == ADMIN_GROUP_LEAVE ? "Leave group" : "Remove", true, true);
    return;
  }

  const char* items[12];
  char dynamic[5][48];
  int count = 0;
  if (g_admin.mode == ADMIN_GROUP_HOME) {
    items[count++] = "Create private group";
    items[count++] = "Join from MeshCore link";
    items[count++] = "My contact QR";
    items[count++] = "Refresh contacts / groups";
  } else if (g_admin.mode == ADMIN_CONTACT_MANAGE) {
    int meta = findMetaForContact(_active_contact, true);
    items[count++] = "Update radio contact name";
    items[count++] = "Edit local nickname";
    snprintf(dynamic[0], sizeof(dynamic[0]), "%s", metaFlag(meta,0x01)?"Unfavorite":"Favorite"); items[count++] = dynamic[0];
    snprintf(dynamic[1], sizeof(dynamic[1]), "%s", metaFlag(meta,0x02)?"Unpin":"Pin"); items[count++] = dynamic[1];
    snprintf(dynamic[2], sizeof(dynamic[2]), "%s", metaFlag(meta,0x04)?"Unmute":"Mute"); items[count++] = dynamic[2];
    snprintf(dynamic[3], sizeof(dynamic[3]), "%s", metaFlag(meta,0x08)?"Unarchive":"Archive"); items[count++] = dynamic[3];
    items[count++] = "Remove contact";
  } else if (g_admin.mode == ADMIN_GROUP_MANAGE) {
    int meta = findMetaForChannel(_active_channel, _active_channel_index, true);
    if (_active_channel_index == 0) {
      items[count++] = "Edit local alias";
      snprintf(dynamic[0], sizeof(dynamic[0]), "%s", metaFlag(meta,0x01)?"Unfavorite":"Favorite"); items[count++] = dynamic[0];
      snprintf(dynamic[1], sizeof(dynamic[1]), "%s", metaFlag(meta,0x02)?"Unpin":"Pin"); items[count++] = dynamic[1];
      snprintf(dynamic[2], sizeof(dynamic[2]), "%s", metaFlag(meta,0x04)?"Unmute":"Mute"); items[count++] = dynamic[2];
      snprintf(dynamic[3], sizeof(dynamic[3]), "%s", metaFlag(meta,0x08)?"Unarchive":"Archive"); items[count++] = dynamic[3];
    } else {
      GroupExtra extra = loadGroupExtra(_active_channel, _active_channel_index);
      items[count++] = "Invitation QR";
      items[count++] = "Export invitation over USB";
      items[count++] = "Edit radio group name";
      items[count++] = "Edit local alias";
      snprintf(dynamic[0], sizeof(dynamic[0]), "%s", metaFlag(meta,0x01)?"Unfavorite":"Favorite"); items[count++] = dynamic[0];
      snprintf(dynamic[1], sizeof(dynamic[1]), "%s", metaFlag(meta,0x02)?"Unpin":"Pin"); items[count++] = dynamic[1];
      snprintf(dynamic[2], sizeof(dynamic[2]), "%s", metaFlag(meta,0x04)?"Unmute":"Mute"); items[count++] = dynamic[2];
      snprintf(dynamic[3], sizeof(dynamic[3]), "%s", metaFlag(meta,0x08)?"Unarchive":"Archive"); items[count++] = dynamic[3];
      snprintf(dynamic[4], sizeof(dynamic[4]), "Color: %s", colorName(extra.color)); items[count++] = dynamic[4];
      static char loc[48]; snprintf(loc, sizeof(loc), "Location sharing: %s", extra.location_share?"Allowed":"Blocked"); items[count++] = loc;
      items[count++] = "Leave private group";
    }
  }

  const int visible = 7;
  if (g_admin.selected < g_admin.offset) g_admin.offset = g_admin.selected;
  if (g_admin.selected >= g_admin.offset + visible) g_admin.offset = g_admin.selected - visible + 1;
  for (int row = 0; row < visible; ++row) {
    int idx = g_admin.offset + row;
    if (idx >= count) break;
    int y = 81 + row * 21;
    if (idx == g_admin.selected) { d.setColor(card); d.fillRoundRect(8, y, 304, 19, 4); }
    d.setColor((strstr(items[idx], "Remove") || strstr(items[idx], "Leave")) ? rgb565p5(235,75,75) : text);
    d.setTextSize(1); d.drawTextEllipsized(17, y + 6, 278, items[idx]);
  }
  if (g_admin.mode == ADMIN_GROUP_MANAGE && _active_channel_index == 0) {
    d.setColor(sub); d.drawTextCentered(160, 229, "Public / World key cannot be edited or exported");
  } else if (g_admin.mode == ADMIN_GROUP_MANAGE && _active_channel_index > 0) {
    d.setColor(sub); d.drawTextCentered(160, 229, "Location setting is permission only; no auto-RF location");
  }
}

static int menuCountFor(const CommunicatorAppScreen* /*self*/) {
  if (g_admin.mode == ADMIN_GROUP_HOME) return 4;
  if (g_admin.mode == ADMIN_CONTACT_MANAGE) return 7;
  if (g_admin.mode == ADMIN_GROUP_MANAGE) return 11;
  return 0;
}

bool CommunicatorAppScreen::handlePiece5AdminTouch(int16_t x, int16_t y, uint8_t gesture) {
  if (!g_admin.active) return false;
  int count = menuCountFor(this);
  if (g_admin.mode == ADMIN_GROUP_MANAGE && _active_channel_index == 0) count = 5;
  if (gesture == COMPACT_TOUCH_SWIPE_UP || gesture == COMPACT_TOUCH_SWIPE_DOWN) {
    if (count > 0) {
      int next = (int)g_admin.selected + (gesture == COMPACT_TOUCH_SWIPE_UP ? 3 : -3);
      if (next < 0) next = 0;
      if (next >= count) next = count - 1;
      g_admin.selected = (uint8_t)next;
      _dirty = DIRTY_ALL;
    }
    return true;
  }
  if (gesture != COMPACT_TOUCH_TAP) return true;
  if (x < 42 && y >= 44 && y <= 78) {
    g_admin.active = false; _dirty = DIRTY_ALL; return true;
  }
  if (g_admin.mode == ADMIN_QR) {
    if (y >= 210) {
      if (x < 160) { g_admin.active = false; _dirty = DIRTY_ALL; }
      else if (g_admin.qr_exportable && g_admin.uri[0]) {
        Serial.print("MESHCORE_SHARE "); Serial.println(g_admin.uri);
        _task->showAlert("Link exported over USB serial", 1100);
      }
    }
    return true;
  }
  if (g_admin.mode == ADMIN_GROUP_CREATE || g_admin.mode == ADMIN_GROUP_JOIN ||
      g_admin.mode == ADMIN_GROUP_RENAME || g_admin.mode == ADMIN_CONTACT_RENAME) {
    if (y >= 201) return handlePiece5AdminInput(x < 160 ? KEY_CANCEL : KEY_ENTER);
    return true;
  }
  if (g_admin.mode == ADMIN_GROUP_LEAVE || g_admin.mode == ADMIN_CONTACT_REMOVE) {
    if (y >= 196) return handlePiece5AdminInput(x < 160 ? KEY_CANCEL : KEY_ENTER);
    return true;
  }
  if (y >= 79 && y < 228) {
    int row = (y - 81) / 21;
    if (row < 0) row = 0;
    int chosen = g_admin.offset + row;
    if (count > 0 && chosen >= count) chosen = count - 1;
    g_admin.selected = (uint8_t)chosen;
    return handlePiece5AdminInput(KEY_ENTER);
  }
  return true;
}

bool CommunicatorAppScreen::handlePiece5AdminInput(char c) {
  if (!g_admin.active) return false;
  if (c == KEY_CANCEL) { g_admin.active = false; _dirty = DIRTY_ALL; return true; }

  bool inputMode = g_admin.mode == ADMIN_GROUP_CREATE || g_admin.mode == ADMIN_GROUP_JOIN ||
                   g_admin.mode == ADMIN_GROUP_RENAME || g_admin.mode == ADMIN_CONTACT_RENAME;
  if (inputMode) {
    if (c == KEY_ENTER) {
      if (!g_admin.input_len) { _task->showAlert("Enter a value", 800); return true; }
      if (g_admin.mode == ADMIN_GROUP_CREATE) {
        int idx = the_mesh.compactFindFreePrivateChannel();
        if (idx < 1) { _task->showAlert("No free private group slots", 1200); return true; }
        ChannelDetails requested{}, readback{};
        makePrivateGroup(g_admin.input, requested);
        if (!the_mesh.compactSetPrivateChannelVerified((uint8_t)idx, requested, readback)) {
          _task->showAlert("Group write/read-back failed", 1400); return true;
        }
        _task->showAlert("Private group created + verified", 1100);
        g_admin.active = false; _dirty = DIRTY_ALL; return true;
      }
      if (g_admin.mode == ADMIN_GROUP_JOIN) {
        ChannelDetails requested{}, readback{}; char err[72] = {0};
        if (!CompactAdminCodec::parseChannelUri(g_admin.input, requested, err, sizeof(err))) {
          _task->showAlert(err, 1300); return true;
        }
        int existing = the_mesh.findChannelIdx(requested.channel);
        if (existing == 0) { _task->showAlert("Public / World is already built in", 1200); return true; }
        if (existing > 0) { _task->showAlert("Private group already configured", 1100); return true; }
        int idx = the_mesh.compactFindFreePrivateChannel();
        if (idx < 1 || !the_mesh.compactSetPrivateChannelVerified((uint8_t)idx, requested, readback)) {
          _task->showAlert("Group import write/read-back failed", 1400); return true;
        }
        _task->showAlert("Private group joined + verified", 1100);
        g_admin.active = false; _dirty = DIRTY_ALL; return true;
      }
      if (g_admin.mode == ADMIN_GROUP_RENAME) {
        if (_active_channel_index == 0) { _task->showAlert("Public / World is protected", 1000); return true; }
        ChannelDetails requested = _active_channel, readback{};
        StrHelper::strncpy(requested.name, g_admin.input, sizeof(requested.name));
        if (!the_mesh.compactSetPrivateChannelVerified(_active_channel_index, requested, readback)) {
          _task->showAlert("Group rename verification failed", 1300); return true;
        }
        _active_channel = readback; StrHelper::strncpy(_active_name, readback.name, sizeof(_active_name));
        _task->showAlert("Radio group name verified", 950);
        resetAdmin(ADMIN_GROUP_MANAGE); _dirty = DIRTY_ALL; return true;
      }
      if (g_admin.mode == ADMIN_CONTACT_RENAME) {
        ContactInfo requested = _active_contact, readback{};
        StrHelper::strncpy(requested.name, g_admin.input, sizeof(requested.name));
        if (!the_mesh.compactUpsertContactVerified(requested, readback)) {
          _task->showAlert("Contact update verification failed", 1300); return true;
        }
        _active_contact = readback; StrHelper::strncpy(_active_name, readback.name, sizeof(_active_name));
        _task->showAlert("Contact update verified", 950);
        resetAdmin(ADMIN_CONTACT_MANAGE); _dirty = DIRTY_ALL; return true;
      }
    }
    if (c == 8 || (uint8_t)c == 127) {
      if (g_admin.input_len) g_admin.input[--g_admin.input_len] = 0;
      _dirty = DIRTY_ALL; return true;
    }
    if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && g_admin.input_len < sizeof(g_admin.input) - 1) {
      g_admin.input[g_admin.input_len++] = c; g_admin.input[g_admin.input_len] = 0;
      _dirty = DIRTY_ALL;
    }
    return true;
  }

  if (g_admin.mode == ADMIN_GROUP_LEAVE) {
    if (c == KEY_ENTER) {
      if (_active_channel_index == 0 || !the_mesh.compactClearPrivateChannelVerified(_active_channel_index)) {
        _task->showAlert("Leave verification failed", 1200); return true;
      }
      _task->showAlert("Private group removed + verified", 1100);
      g_admin.active = false; goBack(); _dirty = DIRTY_ALL;
    }
    return true;
  }
  if (g_admin.mode == ADMIN_CONTACT_REMOVE) {
    if (c == KEY_ENTER) {
      uint8_t key[32]; memcpy(key, _active_contact.id.pub_key, sizeof(key));
      if (!the_mesh.compactRemoveContactVerified(key)) {
        _task->showAlert("Contact removal verification failed", 1300); return true;
      }
      _task->showAlert("Contact removed + verified", 1000);
      g_admin.active = false; goBack(); _dirty = DIRTY_ALL;
    }
    return true;
  }

  int count = menuCountFor(this);
  if (g_admin.mode == ADMIN_GROUP_MANAGE && _active_channel_index == 0) count = 5;
  if (c == KEY_UP) { if (g_admin.selected) --g_admin.selected; _dirty = DIRTY_ALL; return true; }
  if (c == KEY_DOWN) { if (g_admin.selected + 1 < count) ++g_admin.selected; _dirty = DIRTY_ALL; return true; }
  if (c != KEY_ENTER) return true;

  if (g_admin.mode == ADMIN_GROUP_HOME) {
    if (g_admin.selected == 0) resetAdmin(ADMIN_GROUP_CREATE);
    else if (g_admin.selected == 1) resetAdmin(ADMIN_GROUP_JOIN);
    else if (g_admin.selected == 2) openOwnContactQr();
    else { loadContactsAndChannels(); _task->showAlert("Radio contacts/groups refreshed", 850); }
    _dirty = DIRTY_ALL; return true;
  }

  if (g_admin.mode == ADMIN_CONTACT_MANAGE) {
    int meta = findMetaForContact(_active_contact, true);
    switch (g_admin.selected) {
      case 0: resetAdmin(ADMIN_CONTACT_RENAME); StrHelper::strncpy(g_admin.input, _active_contact.name, sizeof(g_admin.input)); g_admin.input_len = strlen(g_admin.input); break;
      case 1: g_admin.active = false; beginAliasEdit(); break;
      case 2: toggleMetaFlag(meta,0x01); break;
      case 3: toggleMetaFlag(meta,0x02); break;
      case 4: toggleMetaFlag(meta,0x04); break;
      case 5: toggleMetaFlag(meta,0x08); break;
      case 6: resetAdmin(ADMIN_CONTACT_REMOVE); break;
    }
    _dirty = DIRTY_ALL; return true;
  }

  if (g_admin.mode == ADMIN_GROUP_MANAGE) {
    int meta = findMetaForChannel(_active_channel, _active_channel_index, true);
    if (_active_channel_index == 0) {
      if (g_admin.selected == 0) { g_admin.active = false; beginAliasEdit(); }
      else if (g_admin.selected == 1) toggleMetaFlag(meta,0x01);
      else if (g_admin.selected == 2) toggleMetaFlag(meta,0x02);
      else if (g_admin.selected == 3) toggleMetaFlag(meta,0x04);
      else if (g_admin.selected == 4) toggleMetaFlag(meta,0x08);
      _dirty = DIRTY_ALL; return true;
    }

    if (g_admin.selected == 0 || g_admin.selected == 1) {
      char uri[320] = {0};
      if (!CompactAdminCodec::makeChannelUri(_active_channel, uri, sizeof(uri))) {
        _task->showAlert("Legacy 32-byte group key is not QR-compatible", 1500); return true;
      }
      if (g_admin.selected == 0) {
        resetAdmin(ADMIN_QR); StrHelper::strncpy(g_admin.qr_title, "Group invitation", sizeof(g_admin.qr_title));
        StrHelper::strncpy(g_admin.uri, uri, sizeof(g_admin.uri)); g_admin.qr_exportable = true;
      } else {
        Serial.print("MESHCORE_SHARE "); Serial.println(uri);
        _task->showAlert("Invitation exported over USB serial", 1100);
      }
    } else if (g_admin.selected == 2) {
      resetAdmin(ADMIN_GROUP_RENAME); StrHelper::strncpy(g_admin.input, _active_channel.name, sizeof(g_admin.input)); g_admin.input_len = strlen(g_admin.input);
    } else if (g_admin.selected == 3) {
      g_admin.active = false; beginAliasEdit();
    } else if (g_admin.selected == 4) toggleMetaFlag(meta,0x01);
    else if (g_admin.selected == 5) toggleMetaFlag(meta,0x02);
    else if (g_admin.selected == 6) toggleMetaFlag(meta,0x04);
    else if (g_admin.selected == 7) toggleMetaFlag(meta,0x08);
    else if (g_admin.selected == 8) {
      GroupExtra extra = loadGroupExtra(_active_channel, _active_channel_index);
      extra.color = (extra.color + 1) % 5; saveGroupExtra(_active_channel, _active_channel_index, extra);
    } else if (g_admin.selected == 9) {
      GroupExtra extra = loadGroupExtra(_active_channel, _active_channel_index);
      extra.location_share = !extra.location_share; saveGroupExtra(_active_channel, _active_channel_index, extra);
    } else if (g_admin.selected == 10) resetAdmin(ADMIN_GROUP_LEAVE);
    _dirty = DIRTY_ALL; return true;
  }

  return true;
}
