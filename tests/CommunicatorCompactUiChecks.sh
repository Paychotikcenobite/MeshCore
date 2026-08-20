#!/usr/bin/env bash
set -euo pipefail

UI="examples/companion_radio/ui-compact/CommunicatorAppScreen.cpp"
UI_H="examples/companion_radio/ui-compact/CommunicatorAppScreen.h"
TASK="examples/companion_radio/ui-compact/UITask.cpp"
TOUCH="examples/companion_radio/ui-compact/TouchDrvGT911Recovery.hpp"
PERSIST="examples/companion_radio/ui-compact/CommunicatorAppPersistence.cpp"
CONTACT="examples/companion_radio/ui-compact/CommunicatorContactAdd.cpp"
VISUAL="examples/companion_radio/ui-compact/CommunicatorVisualPolish.cpp"
DISPLAY="src/helpers/ui/ST7789LCDDisplay.cpp"
ROADMAP="docs/communicator-compact-roadmap.md"

require() {
  local file="$1"; local needle="$2"; local description="$3"
  if ! grep -Fq "$needle" "$file"; then
    echo "Communicator Compact retention check failed: $description"
    echo "Expected: $needle"
    echo "In: $file"
    exit 1
  fi
}

forbid() {
  local file="$1"; local needle="$2"; local description="$3"
  if grep -Fq "$needle" "$file"; then
    echo "Communicator Compact retention check failed: $description"
    echo "Unexpected: $needle"
    echo "In: $file"
    exit 1
  fi
}

# Piece 1: physically validated T-Deck input/display foundation.
require "$TOUCH" 'if (!isPressed()) return 0;' 'GT911 getPoint must remain gated by isPressed so idle polling cannot clear pending touches'
require "$TOUCH" 'wire.begin(sda, scl, 100000);' 'standard T-Deck shared I2C bus must remain explicitly initialized'
require "$TOUCH" 'COMPACT_TDECK_I2C_SDA 18' 'standard T-Deck SDA pin must remain 18'
require "$TOUCH" 'COMPACT_TDECK_I2C_SCL 8' 'standard T-Deck SCL pin must remain 8'
require "$TOUCH" 'COMPACT_TDECK_TOUCH_INT 16' 'standard T-Deck GT911 interrupt pin must remain 16'
require "$TOUCH" 'TouchDrvGT911::setMirrorXY(mirrorX, true);' 'validated standard-T-Deck Y transform must remain enabled'
require "$TASK" 'home = new CommunicatorAppScreen(this, &rtc_clock);' 'Compact target must launch the beta219-derived application shell'
require "$TASK" 'gesture = COMPACT_TOUCH_LONG_PRESS;' 'long press must be additive to the validated tap/swipe path'
require "$DISPLAY" '#if !(defined(LILYGO_TDECK) && defined(MESHCORE_COMPACT_UI))' 'Compact renderer must retain the no-full-screen-clear startFrame guard'

# Piece 1: product shell and corrected navigation semantics.
require "$UI_H" 'ROUTE_MAIN' 'main route must exist'
require "$UI_H" 'ROUTE_SETTINGS' 'Settings daughter route must exist'
require "$UI_H" 'ROUTE_RADIO' 'Radio Status daughter route must exist'
require "$UI" 'drawAppHeader(d)' 'persistent Communicator header must remain part of top-level rendering'
require "$UI" 'drawTabs(d)' 'Chats / Repeaters tabs must remain persistent top-level destinations'
require "$UI" '"MeshCore"' 'persistent app branding must remain MeshCore Communicator'
require "$UI" '"Communicator"' 'persistent app branding must remain MeshCore Communicator'
require "$TASK" 'app->openSettingsSingleTop();' 'Settings header action must use single-top navigation so repeated taps do not stack duplicate Settings routes'
require "$TASK" 'app->openRadioSingleTop();' 'Radio header action must use single-top navigation'
require "$TASK" 'app->navigateBack();' 'daughter-screen back must be universal so New conversation can return to main'
require "$PERSIST" 'void CommunicatorAppScreen::openSettingsSingleTop()' 'single-top Settings navigation implementation must exist'
require "$PERSIST" 'void CommunicatorAppScreen::navigateBack()' 'universal back implementation must exist'

# Visual polish: recognisable Windows/Fluent header glyphs with real coverage AA.
require "$TASK" 'redrawHeaderActionIconsFluent' 'runtime must use the Fluent icon layer rather than the v10 line-art fallback'
require "$VISUAL" 'SETTINGS24_A4' 'Settings must use a rasterized Fluent-style 24px mask'
require "$VISUAL" 'WIFI24_A4' 'wireless action must use a 24px Windows/Fluent fan mask'
require "$VISUAL" 'blend565' 'anti-aliased icon edges must blend coverage into their button background'
require "$VISUAL" '4-bit alpha masks' 'visual layer must document its sub-pixel coverage approach'

# Piece 2: Chats home and organization semantics derived from Android Communicator.
require "$UI_H" 'FILTER_ALL' 'All filter must exist'
require "$UI_H" 'FILTER_FAVORITES' 'Favorites filter must exist'
require "$UI_H" 'FILTER_UNREAD' 'Unread filter must exist'
require "$UI_H" 'FILTER_ATTENTION' 'Needs attention filter must exist'
require "$UI" '"Search conversations"' 'Chats search must remain visible'
require "$UI" '"+  New conversation"' 'New conversation entry point must remain visible'
require "$UI" '"No favorite conversations"' 'Favorites empty state must remain explicit'
require "$UI" '"No unread conversations"' 'Unread empty state must remain explicit'
require "$UI" '"Nothing needs attention"' 'Needs-attention empty state must remain explicit'
require "$UI" 'p.putBytes("meta", _meta, sizeof(_meta));' 'favorite/pin/mute/archive/alias metadata must persist across reboot'
require "$UI" 'metaFlag(meta,0x01)?"Unfavorite":"Favorite"' 'Favorite action must remain separate from Pin'
require "$UI" 'metaFlag(meta,0x02)?"Unpin":"Pin"' 'Pin action must remain separate from Favorite'
require "$UI" 'metaFlag(meta,0x04)?"Unmute":"Mute"' 'Mute action must remain available'
require "$UI" '_active_kind==ROW_CHANNEL?"Rename group":"Rename contact"' 'Rename action must preserve contact/group terminology'
require "$UI" 'metaFlag(meta,0x08)?"Unarchive":"Archive"' 'Archive action must remain available'
require "$UI" '"Delete local history"' 'Delete action must remain explicitly local'
require "$UI" '_show_public' 'Public / World visibility must remain a distinct setting'
require "$UI" 'ROUTE_NEW_CONVERSATION' 'New Conversation must remain a daughter screen'

# Standalone contact creation: without this, a fresh T-Deck cannot initiate DMs.
require "$TASK" 'app->manualContactsBegin();' 'manual contacts must rehydrate after normal MeshCore startup'
require "$TASK" 'app->tryBeginContactAdd' 'New Conversation Add contact must enter the real standalone editor'
require "$CONTACT" 'the_mesh.addContact(ci)' 'manual public-key contacts must become real BaseChatMesh contacts'
require "$CONTACT" 'the_mesh.lookupContactByPubKey' 'contact editor must update/deduplicate by full identity key'
require "$CONTACT" 'Public key - 32 bytes / 64 hex digits' 'editor must make the full-key requirement explicit'
require "$CONTACT" 'mcccontacts' 'manual contacts must survive reboot independently of volatile UI state'
require "$CONTACT" 'c.out_path_len = OUT_PATH_UNKNOWN' 'manually added contacts must start flood-capable until a direct route is learned'

# Piece 3: durable, identity-keyed local data engine.
require "$UI_H" 'static const int MESSAGE_CACHE = 96;' 'persistent working-set limit must remain explicit and bounded'
require "$TASK" 'persistenceBegin();' 'durable history must load after MeshCore/SPIFFS initialization'
require "$TASK" 'persistenceCheckpoint(true);' 'navigation/messages/screen-off must checkpoint durable state'
require "$PERSIST" 'kHistoryPath = "/mcc_history_v1.bin"' 'versioned persistent message journal must exist'
require "$PERSIST" 'kDraftPath = "/mcc_drafts_v1.bin"' 'versioned persistent drafts store must exist'
require "$PERSIST" 'constexpr uint16_t kSchemaVersion = 1;' 'storage schema must be versioned'
require "$PERSIST" 'uint64_t id;' 'messages must have stable persistent IDs'
require "$PERSIST" 'uint64_t reply_to;' 'message schema must reserve stable reply metadata'
require "$PERSIST" 'uint8_t conv_key[32];' 'conversation storage must use a 32-byte stable identity key rather than display name'
require "$PERSIST" 'last-known display name only; never the primary key' 'display names must not become database identity'
require "$PERSIST" 'partial write/corrupt tail: keep valid prefix' 'append journal must recover from an incomplete/corrupt tail'
require "$PERSIST" 'kCompactAtBytes' 'journal growth must be bounded by compaction'
require "$PERSIST" 'replaceAtomically' 'compaction/draft snapshots must use replacement recovery semantics'
require "$PERSIST" 'Drafts are keyed by the same stable contact/channel identity' 'drafts must survive reboot without being keyed by mutable names'
require "$PERSIST" 'r.send_state = _messages[i].send_state' 'send metadata must persist'
require "$PERSIST" 'constexpr uint8_t kFlagUnread = 0x02;' 'schema-v1 unread flag value must remain stable'
require "$PERSIST" 'm.unread = (r.flags & kFlagUnread) ? 1 : 0;' 'unread state must restore from persistent history'

# Roadmap itself is part of the development contract.
require "$ROADMAP" '## Piece 1 — UI platform and navigation shell' 'Piece 1 roadmap section must exist'
require "$ROADMAP" '## Piece 3 — Persistent local data engine' 'Piece 3 roadmap section must exist'
require "$ROADMAP" '## Piece 12 — Integration, performance, power, and release validation' 'roadmap must cover full-project integration/release work'

# Do not let future refactors silently reintroduce the obsolete single-screen class.
forbid "$TASK" 'new CommunicatorScreen(' 'legacy one-off CommunicatorScreen must not return'

echo "Communicator Compact Piece 1/2/3 + contact/visual retention checks passed"
