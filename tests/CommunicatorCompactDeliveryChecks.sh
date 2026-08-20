#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCREEN_H="$ROOT/examples/companion_radio/ui-compact/CommunicatorAppScreen.h"
DELIVERY_CPP="$ROOT/examples/companion_radio/ui-compact/CommunicatorDelivery.cpp"
ACTIONS_CPP="$ROOT/examples/companion_radio/ui-compact/CommunicatorMessageActions.cpp"
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
require "SEND_CONFIRMED = 4" "$SCREEN_H" "confirmed state must be distinct from legacy optimistic Sent"
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
require "m.send_state = SEND_CONFIRMED" "$DELIVERY_CPP" "only ACK reconciliation may promote a direct message to Confirmed"
require "Confirmed" "$DELIVERY_CPP" "confirmed direct sends must render distinctly from Sent"
require "Failed - no ACK" "$DELIVERY_CPP" "ACK timeout must produce failure"
require "reconcileDirectSendState" "$UI_TASK_CPP" "UI loop must reconcile send lifecycle"
require "drawDirectSendOverlay" "$UI_TASK_CPP" "UI frame must render pending/confirmed direct-send overlay"

require "tryOpenMessageActions" "$ACTIONS_CPP" "message long-press must open a real action modal"
require "Retry failed send" "$ACTIONS_CPP" "failed sends must expose Retry"
require "attempt=4" "$ACTIONS_CPP" "direct retry must use MeshCore retry packet form"
require "takeCompactSendStart" "$ACTIONS_CPP" "retry must bind the newly registered ACK to the same message"
require "persistenceCheckpoint(true)" "$ACTIONS_CPP" "retry/delete state changes must persist immediately"
require "Message deleted locally" "$ACTIONS_CPP" "per-message delete must be local and explicit"
require "Confirmed: MeshCore ACK received" "$ACTIONS_CPP" "delivery details must expose real ACK evidence"
require "Sent: no retained peer-ACK proof" "$ACTIONS_CPP" "legacy Sent must not be relabeled Confirmed"
require "no per-peer group ACK exists" "$ACTIONS_CPP" "group details must not claim peer delivery"
require "messageActionActive" "$UI_TASK_CPP" "message action modal must intercept touch/keyboard before chat"
require "drawMessageActionOverlay" "$UI_TASK_CPP" "message action modal must render after chat"

if grep -Fq "MESHCORE_COMPACT_UI" "$ROOT/variants/lilygo_tdeck/platformio.ini"; then
  :
else
  echo "Piece 4 contract failed: compact target feature gate is missing" >&2
  exit 1
fi

echo "Communicator Compact Piece 4 delivery contract: OK"
