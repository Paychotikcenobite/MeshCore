#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCREEN_H="$ROOT/examples/companion_radio/ui-compact/CommunicatorAppScreen.h"
DELIVERY_CPP="$ROOT/examples/companion_radio/ui-compact/CommunicatorDelivery.cpp"
ACTIONS_CPP="$ROOT/examples/companion_radio/ui-compact/CommunicatorMessageActions.cpp"
PERSIST_CPP="$ROOT/examples/companion_radio/ui-compact/CommunicatorAppPersistence.cpp"
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
require "SEND_STOPPED = 5" "$SCREEN_H" "local stop-waiting must be distinct from failed/cancelled delivery"
require "delivery_ack" "$SCREEN_H" "message cache must track ACK token"
require "delivery_deadline_ms" "$SCREEN_H" "message cache must track ACK deadline"
require "MESHCORE_COMPACT_UI" "$MESH_H" "compact send overload must be target-gated"
require "char (&text)[N]" "$MESH_H" "compact composer must use array-only direct-send overload"
require "using BaseChatMesh::sendGroupMessage" "$MESH_H" "compact group wrapper must preserve pointer-based phone/BLE sends"
require "sendCompactGroupMessage" "$MESH_H" "compact group sends must cross an explicit UI-safe wrapper"
require "BaseChatMesh::sendGroupMessage" "$MESH_DELIVERY_CPP" "compact group wrapper must delegate to existing MeshCore flood transport"
require "has no receiver ACK" "$MESH_DELIVERY_CPP" "group wrapper must document lack of peer confirmation"
require "BaseChatMesh::sendMessage(*stored" "$MESH_DELIVERY_CPP" "compact direct send must delegate to existing transport"
require "expected_ack_table[slot].ack = expected_ack" "$MESH_DELIVERY_CPP" "compact send must register expected ACK"
require "lookupContactByPubKey" "$MESH_DELIVERY_CPP" "ACK table must point at MeshCore-owned contact"
require "CompactAckTrack" "$MESH_DELIVERY_CPP" "compact ACKs must retain their shared-table slot generation"
require "entry.msg_sent != track.msg_sent" "$MESH_DELIVERY_CPP" "overwritten ACK slots must not become false confirmations"
require "Unknown must never mean delivered" "$MESH_DELIVERY_CPP" "unknown ACK state must fail conservatively"
require "entry.msg_sent == track.msg_sent && entry.ack == ack" "$MESH_DELIVERY_CPP" "timeout cleanup must not clear another sender's ACK"
require "m.send_state = SEND_CONFIRMED" "$DELIVERY_CPP" "only ACK reconciliation may promote a direct message to Confirmed"
require "SEND_STOPPED" "$DELIVERY_CPP" "stopped local ACK tracking must render distinctly"
require "d.print(\"Stopped\")" "$DELIVERY_CPP" "stopped state must not render as Sent or Failed"
require "Failed - no ACK" "$DELIVERY_CPP" "ACK timeout must produce failure"
require "reconcileDirectSendState" "$UI_TASK_CPP" "UI loop must reconcile send lifecycle"
require "drawDirectSendOverlay" "$UI_TASK_CPP" "UI frame must render pending/confirmed direct-send overlay"

require "tryOpenMessageActions" "$ACTIONS_CPP" "message long-press must open a real action modal"
require "Reply (local reference)" "$ACTIONS_CPP" "message actions must expose truthfully labeled local Reply"
require "beginReplyToMessage" "$ACTIONS_CPP" "Reply action must bind to persistent history metadata"
require "Stop waiting (local)" "$ACTIONS_CPP" "in-flight direct sends must expose local stop-waiting semantics"
require "packet already sent" "$ACTIONS_CPP" "stop-waiting must never imply RF cancellation"
require "m.send_state = SEND_STOPPED" "$ACTIONS_CPP" "stop-waiting must persist a distinct truthful state"
require "Retry failed send" "$ACTIONS_CPP" "failed sends must expose Retry"
require "attempt=4" "$ACTIONS_CPP" "direct retry must use MeshCore retry packet form"
require "takeCompactSendStart" "$ACTIONS_CPP" "retry must bind the newly registered ACK to the same message"
require "persistenceCheckpoint(true)" "$ACTIONS_CPP" "retry/delete/stop state changes must persist immediately"
require "Message deleted locally" "$ACTIONS_CPP" "per-message delete must be local and explicit"
require "Confirmed: MeshCore ACK received" "$ACTIONS_CPP" "delivery details must expose real ACK evidence"
require "Stopped locally; RF packet was already sent" "$ACTIONS_CPP" "delivery details must describe stop-waiting truthfully"
require "Sent: no retained peer-ACK proof" "$ACTIONS_CPP" "legacy Sent must not be relabeled Confirmed"
require "no per-peer group ACK exists" "$ACTIONS_CPP" "group details must not claim peer delivery"
require "Local reply to:" "$ACTIONS_CPP" "message details must surface persisted local reply relationship"
require "messageActionActive" "$UI_TASK_CPP" "message action modal must intercept touch/keyboard before chat"
require "drawMessageActionOverlay" "$UI_TASK_CPP" "message action modal must render after chat"

require "uint64_t reply_to" "$PERSIST_CPP" "history schema must retain stable reply target IDs"
require "pending_reply_to" "$PERSIST_CPP" "composer must stage a local reply target"
require "beginReplyToMessage" "$PERSIST_CPP" "reply target must be created only after target has a stable ID"
require "persistenceCheckpoint(true)" "$PERSIST_CPP" "reply target ID must be durable before it is referenced"
require "g.reply_to[i] = (i == pending_reply_slot) ? g.pending_reply_to : 0" "$PERSIST_CPP" "new outgoing message must persist the stable reply_to relationship"
require "messagePersistHash" "$PERSIST_CPP" "reply_to changes must participate in persistence hashing"
require "no bytes are added to MeshCore's plain/group text RF payload" "$PERSIST_CPP" "local Reply must not invent an incompatible RF envelope"
require "Reply (local):" "$PERSIST_CPP" "composer must label reply scope accurately"
require "drawReplyComposerOverlay" "$UI_TASK_CPP" "pending Reply must be visible in the composer"
require "handleReplyTouch" "$UI_TASK_CPP" "touch must allow cancelling a pending Reply"
require "replyPending() && c == KEY_CANCEL" "$UI_TASK_CPP" "keyboard Esc must cancel Reply before leaving chat"

if grep -Fq "MESHCORE_COMPACT_UI" "$ROOT/variants/lilygo_tdeck/platformio.ini"; then
  :
else
  echo "Piece 4 contract failed: compact target feature gate is missing" >&2
  exit 1
fi

echo "Communicator Compact Piece 4 delivery contract: OK"
