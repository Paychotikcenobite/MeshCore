#!/usr/bin/env bash
set -euo pipefail

UI="examples/companion_radio/ui-compact/CommunicatorAppScreen.cpp"
UI_H="examples/companion_radio/ui-compact/CommunicatorAppScreen.h"
TASK="examples/companion_radio/ui-compact/UITask.cpp"
TOUCH="examples/companion_radio/ui-compact/TouchDrvGT911Recovery.hpp"
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

# Piece 1: product shell and navigation model.
require "$UI_H" 'ROUTE_MAIN' 'main route must exist'
require "$UI_H" 'ROUTE_SETTINGS' 'Settings daughter route must exist'
require "$UI_H" 'ROUTE_RADIO' 'Radio Status daughter route must exist'
require "$UI" 'drawAppHeader(d)' 'persistent Communicator header must remain part of top-level rendering'
require "$UI" 'drawTabs(d)' 'Chats / Repeaters tabs must remain persistent top-level destinations'
require "$UI" '"MeshCore"' 'persistent app branding must remain MeshCore Communicator'
require "$UI" '"Communicator"' 'persistent app branding must remain MeshCore Communicator'

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

# Roadmap itself is now part of the development contract.
require "$ROADMAP" '## Piece 1 — UI platform and navigation shell' 'Piece 1 roadmap section must exist'
require "$ROADMAP" '## Piece 12 — Integration, performance, power, and release validation' 'roadmap must cover full-project integration/release work'

# Do not let future refactors silently reintroduce the obsolete single-screen class.
forbid "$TASK" 'new CommunicatorScreen(' 'legacy one-off CommunicatorScreen must not return'

echo "Communicator Compact Piece 1/2 retention checks passed"
