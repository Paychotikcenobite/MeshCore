#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCREEN_H="$ROOT/examples/companion_radio/ui-compact/CommunicatorAppScreen.h"
DELIVERY_CPP="$ROOT/examples/companion_radio/ui-compact/CommunicatorDelivery.cpp"
MESH_H="$ROOT/examples/companion_radio/MyMesh.h"
MESH_DELIVERY_CPP="$ROOT/examples/companion_radio/ui-compact/MyMeshCompactDelivery.cpp"
UI_TASK_CPP="$ROOT/examples/companion_radio/ui-compact/UITask.cpp"

require() {
  local needle="$1" file="$2" label="$3"
  if ! grep -Fq -- "$needle" "$file"; then
    echo "Piece 4 contract failed: $label" >&2
    exit 1
  fi
}

require "SEND_SENDING = 3" "$SCREEN_H" "sending state must preserve schema-v1 sent/failed values"
require "delivery_ack" "$SCREEN_H" "message cache must track ACK token"
require "delivery_deadline_ms" "$SCREEN_H" "message cache must track ACK deadline"
require "MESHCORE_COMPACT_UI" "$MESH_H" "compact send overload must be target-gated"
require "char (&text)[N]" "$MESH_H" "compact composer must use array-only overload"
require "BaseChatMesh::sendMessage(*stored" "$MESH_DELIVERY_CPP" "compact send must delegate to existing transport"
require "expected_ack_table[slot].ack = expected_ack" "$MESH_DELIVERY_CPP" "compact send must register expected ACK"
require "lookupContactByPubKey" "$MESH_DELIVERY_CPP" "ACK table must point at MeshCore-owned contact"
require "CompactAckTrack" "$MESH_DELIVERY_CPP" "compact ACKs must retain their shared-table slot generation"
require "entry.msg_sent != track.msg_sent" "$MESH_DELIVERY_CPP" "overwritten ACK slots must not become false confirmations"
require "Unknown must never mean delivered" "$MESH_DELIVERY_CPP" "unknown ACK state must fail conservatively"
require "entry.msg_sent == track.msg_sent && entry.ack == ack" "$MESH_DELIVERY_CPP" "timeout cleanup must not clear another sender's ACK"
require "isCompactAckPending" "$DELIVERY_CPP" "UI must use ACK table as delivery authority"
require "Failed - no ACK" "$DELIVERY_CPP" "ACK timeout must produce failure"
require "d.print(\"Sending\")" "$DELIVERY_CPP" "pending direct message must render as Sending"
require "reconcileDirectSendState" "$UI_TASK_CPP" "UI loop must reconcile send lifecycle"
require "drawDirectSendOverlay" "$UI_TASK_CPP" "UI frame must render pending-send overlay"

if grep -Fq "MESHCORE_COMPACT_UI" "$ROOT/variants/lilygo_tdeck/platformio.ini"; then
  :
else
  echo "Piece 4 contract failed: compact target feature gate is missing" >&2
  exit 1
fi

echo "Communicator Compact Piece 4 delivery contract: OK"
