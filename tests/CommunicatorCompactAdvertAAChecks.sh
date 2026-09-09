#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROMPT="$ROOT/examples/companion_radio/ui-compact/CommunicatorAdvertPrompt.cpp"
AA="$ROOT/examples/companion_radio/ui-compact/CompactAARender.cpp"
REG="$ROOT/examples/companion_radio/ui-compact/CompactAAReg9.inc"
MED="$ROOT/examples/companion_radio/ui-compact/CompactAAMed11.inc"
AAPASS="$ROOT/examples/companion_radio/ui-compact/CommunicatorVisualAAText.cpp"
TASK="$ROOT/examples/companion_radio/ui-compact/UITask.cpp"
DISPLAY="$ROOT/src/helpers/ui/LGFXDisplay.cpp"
WORKFLOW="$ROOT/.github/workflows/communicator-compact-ci.yml"

need(){ grep -Fq "$2" "$1" || { echo "Advert/AA contract missing: $2" >&2; exit 1; }; }

need "$PROMPT" '"Heard:"'
need "$PROMPT" '"Add to contacts"'
need "$PROMPT" '"Dismiss"'
need "$PROMPT" 'compactUpsertContactVerified'
need "$PROMPT" 'compactRemoveContactVerified'
need "$PROMPT" 'getRecentlyHeard'
need "$PROMPT" 'age<=30'
need "$TASK" 'advertPromptActive()'
need "$TASK" 'handleAdvertPromptTouch'
need "$TASK" 'handleAdvertPromptInput'
need "$TASK" 'drawAdvertPromptOverlay'

need "$REG" 'generated offline at 4x then Lanczos downsampled'
need "$REG" 'REG9_A4_B64'
need "$MED" 'MED11_A4_B64'
need "$AA" 'blend565'
need "$AAPASS" 'redrawAATextPass'
need "$AAPASS" 'redrawAAComposerText'
need "$AAPASS" 'CompactAA::searchIcon'
need "$AAPASS" 'CompactAA::circle'

need "$DISPLAY" '#ifdef MESHCORE_COMPACT_UI'
need "$DISPLAY" 'display->setColorDepth(16);'
need "$DISPLAY" 'buffer.setColorDepth(16);'
need "$DISPLAY" 'display->setColorDepth(8);'
need "$DISPLAY" 'buffer.setColorDepth(8);'

need "$TASK" 'tdeck_touch.begin(Wire, GT911_SLAVE_ADDRESS_L, 18, 8)'
need "$TASK" 'if (abs(dx) < 28 && abs(dy) < 28) gesture = COMPACT_TOUCH_TAP;'
need "$WORKFLOW" 'FIRMWARE_VERSION: compact-v16-piece5-advert-aa'

echo "Communicator Compact advert/AA contract checks passed"