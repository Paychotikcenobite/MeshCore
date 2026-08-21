#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ADMIN="$ROOT/examples/companion_radio/ui-compact/CommunicatorPiece5Admin.cpp"
CODEC="$ROOT/examples/companion_radio/ui-compact/CommunicatorAdminCodec.cpp"
CONTACT="$ROOT/examples/companion_radio/ui-compact/CommunicatorContactAdd.cpp"
MESH="$ROOT/examples/companion_radio/ui-compact/MyMeshCompactAdmin.cpp"
TASK="$ROOT/examples/companion_radio/ui-compact/UITask.cpp"
PIO="$ROOT/variants/lilygo_tdeck/platformio.ini"
WORKFLOW="$ROOT/.github/workflows/communicator-compact-ci.yml"

need() { grep -Fq "$2" "$1" || { echo "Piece 5 contract missing: $2" >&2; exit 1; }; }
forbid() { ! grep -Fq "$2" "$1" || { echo "Piece 5 contract forbids: $2" >&2; exit 1; }; }

# Communicator-compatible URI input/export.
need "$CODEC" 'meshcore://contact/add?'
need "$CODEC" 'meshcore://channel/add?'
need "$CODEC" 'strlen(input) != 64'
need "$CODEC" 'parseHex(secret_hex, 32, out.channel.secret, 16)'
need "$CODEC" 'makeContactUri'
need "$CODEC" 'makeChannelUri'

# Contact writes are real MeshCore writes followed by persistent-file readback.
need "$CONTACT" 'compactUpsertContactVerified'
need "$MESH" 'saveContacts();'
need "$MESH" 'openRead("/contacts3")'
need "$MESH" 'compactRemoveContactVerified'
forbid "$CONTACT" '#include <Preferences.h>'

# Private groups persist and are re-read from /channels2. Channel zero is never
# writable through the Compact private-group admin path.
need "$MESH" 'compactSetPrivateChannelVerified'
need "$MESH" 'saveChannels();'
need "$MESH" 'openRead("/channels2")'
need "$MESH" 'channel_idx == 0'
need "$MESH" 'compactClearPrivateChannelVerified'
need "$ADMIN" 'Public / World key cannot be edited or exported'
need "$ADMIN" 'existing == 0'

# Standalone administration surface: create/join/edit/leave, local organization,
# conversation color/location permission, own/group QR and USB text export.
need "$ADMIN" 'ADMIN_GROUP_CREATE'
need "$ADMIN" 'ADMIN_GROUP_JOIN'
need "$ADMIN" 'ADMIN_GROUP_RENAME'
need "$ADMIN" 'ADMIN_GROUP_LEAVE'
need "$ADMIN" 'ADMIN_CONTACT_RENAME'
need "$ADMIN" 'ADMIN_CONTACT_REMOVE'
need "$ADMIN" 'location_share'
need "$ADMIN" 'colorName'
need "$ADMIN" 'qrcode_initText'
need "$ADMIN" 'qrcode_getModule'
need "$ADMIN" 'MESHCORE_SHARE'
need "$ADMIN" 'COMPACT_TOUCH_SWIPE_UP'
need "$ADMIN" 'COMPACT_TOUCH_SWIPE_DOWN'
need "$PIO" 'ricmoo/QRCode @ 0.0.1'
need "$WORKFLOW" 'FIRMWARE_VERSION: compact-v16-piece5-advert-aa'

# Piece 5 is layered into UITask dispatch; the validated GT911 classifier stays
# exactly in the existing polling path and is not replaced by the admin code.
need "$TASK" 'piece5AdminActive()'
need "$TASK" 'tryBeginPiece5Admin'
need "$TASK" 'drawPiece5AdminOverlay'
need "$TASK" 'tdeck_touch.begin(Wire, GT911_SLAVE_ADDRESS_L, 18, 8)'
need "$TASK" 'if (abs(dx) < 28 && abs(dy) < 28) gesture = COMPACT_TOUCH_TAP;'

echo "Communicator Compact Piece 5 contract checks passed"