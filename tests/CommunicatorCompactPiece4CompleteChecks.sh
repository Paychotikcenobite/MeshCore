#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNREAD="$ROOT/examples/companion_radio/ui-compact/CommunicatorUnreadNavigation.cpp"
COMPLETE="$ROOT/docs/communicator-compact-piece4-complete.md"
WORKFLOW="$ROOT/.github/workflows/communicator-compact-ci.yml"

require() {
  local needle="$1" file="$2" label="$3"
  if ! grep -Fq -- "$needle" "$file"; then
    echo "Piece 4 completion check failed: $label" >&2
    exit 1
  fi
}

require 'c == KEY_ENTER && _route == ROUTE_MAIN && _tab == TAB_CHATS && !_search_active' "$UNREAD" "keyboard/trackball Enter must capture unread state before legacy openRow clears it"
require 'return false;' "$UNREAD" "pre-open unread capture must hand Enter back to the legacy row-open path"
require 'trackball-right jumps to newest' "$COMPLETE" "completion record must document physical newest navigation"
require 'no automatic direct-to-flood timeout fallback' "$COMPLETE" "completion record must not invent timeout fallback"
require 'no per-peer ACK' "$COMPLETE" "completion record must preserve truthful group delivery semantics"
require 'compact-v14-ui-ack-fix' "$WORKFLOW" "CI artifact identity must identify the completed Piece 4 release candidate"

echo "Communicator Compact Piece 4 completion contract: OK"
