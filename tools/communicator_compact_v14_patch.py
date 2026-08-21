from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))


# 1) Header icons: render the Fluent icons in the header itself.  The previous
# implementation painted the old line-art glyphs first and only overlaid the
# Fluent masks after the whole screen finished rendering, which made the old
# icons visible momentarily on a direct-draw ST7789.
ui = "examples/companion_radio/ui-compact/CommunicatorAppScreen.cpp"
replace_once(
    ui,
    'void CommunicatorAppScreen::drawAppHeader(DisplayDriver& d){CompactPalette p=paletteFor(_light_mode);d.setColor(p.bg);d.fillRect(0,0,320,41);drawLogo(d);d.setTextSize(1);d.setColor(p.text);d.setCursor(41,7);d.print("MeshCore");d.setCursor(41,21);d.print("Communicator");d.setColor(p.card);d.fillRoundRect(238,4,36,33,6);d.setColor(p.divider);d.drawRoundRect(238,4,36,33,6);drawRadioGlyph(d,256,21);d.setColor(p.card);d.fillRoundRect(279,4,36,33,6);d.setColor(p.divider);d.drawRoundRect(279,4,36,33,6);drawGear(d,297,21);}',
    'void CommunicatorAppScreen::drawAppHeader(DisplayDriver& d){CompactPalette p=paletteFor(_light_mode);d.setColor(p.bg);d.fillRect(0,0,320,41);drawLogo(d);d.setTextSize(1);d.setColor(p.text);d.setCursor(41,7);d.print("MeshCore");d.setCursor(41,21);d.print("Communicator");redrawHeaderActionIconsFluent(d);}'
)

# 2) Keyboard flicker: a composer-only dirty frame now erases/redraws only
# the text interior.  Full chat renders still draw the complete composer and
# both buttons, but a keypress no longer repaints the entire 320x41 strip.
replace_once(
    ui,
    'void CommunicatorAppScreen::drawComposer(DisplayDriver& d){CompactPalette p=paletteFor(_light_mode);d.setColor(p.bg);d.fillRect(0,199,320,41);d.setColor(p.input);d.fillRoundRect(6,203,193,33,6);d.setColor(p.stroke);d.drawRoundRect(6,203,193,33,6);d.setTextSize(1);d.setColor(_compose_len?p.text:p.sub);d.drawTextEllipsized(16,215,173,_compose_len?_compose:"Message");drawButton(d,204,203,49,33,"Voice",false,true);drawButton(d,258,203,56,33,"Send",true,true);}',
    'void CommunicatorAppScreen::drawComposer(DisplayDriver& d){CompactPalette p=paletteFor(_light_mode);if(_dirty==DIRTY_COMPOSER){d.setColor(p.input);d.fillRect(12,211,181,15);d.setTextSize(1);d.setColor(_compose_len?p.text:p.sub);d.drawTextEllipsized(16,215,173,_compose_len?_compose:"Message");return;}d.setColor(p.bg);d.fillRect(0,199,320,41);d.setColor(p.input);d.fillRoundRect(6,203,193,33,6);d.setColor(p.stroke);d.drawRoundRect(6,203,193,33,6);d.setTextSize(1);d.setColor(_compose_len?p.text:p.sub);d.drawTextEllipsized(16,215,173,_compose_len?_compose:"Message");drawButton(d,204,203,49,33,"Voice",false,true);drawButton(d,258,203,56,33,"Send",true,true);}'
)

# 3) Direct ACK observation: keep generation safety, but latch the positive
# event in the actual MeshCore processAck() path rather than relying only on a
# later observation of a cleared shared table slot.
mesh_delivery = "examples/companion_radio/ui-compact/MyMeshCompactDelivery.cpp"
replace_once(
    mesh_delivery,
    'struct CompactAckTrack {\n  uint32_t ack;\n  uint8_t slot;\n  unsigned long msg_sent;\n};',
    'struct CompactAckTrack {\n  uint32_t ack;\n  uint8_t slot;\n  unsigned long msg_sent;\n  bool confirmed;\n};'
)
replace_once(
    mesh_delivery,
    '  const unsigned long now = _ms->getMillis();\n  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; ++i) {\n    if (expected_ack_table[i].ack && (unsigned long)(now - expected_ack_table[i].msg_sent) > kAckEntryStaleMs) {',
    '  memset(&g_compact_send_start, 0, sizeof(g_compact_send_start));\n\n  const unsigned long cleanup_now = _ms->getMillis();\n  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; ++i) {\n    if (expected_ack_table[i].ack && (unsigned long)(cleanup_now - expected_ack_table[i].msg_sent) > kAckEntryStaleMs) {'
)
replace_once(
    mesh_delivery,
    '  if (expected_ack) {\n    expected_ack_table[slot].msg_sent = now;\n    expected_ack_table[slot].ack = expected_ack;\n    expected_ack_table[slot].contact = stored;\n    next_ack_idx = (slot + 1) % EXPECTED_ACK_TABLE_SIZE;\n\n    g_compact_ack_tracks[track_slot].ack = expected_ack;\n    g_compact_ack_tracks[track_slot].slot = (uint8_t)slot;\n    g_compact_ack_tracks[track_slot].msg_sent = now;\n\n    memset(&g_compact_send_start, 0, sizeof(g_compact_send_start));',
    '  if (expected_ack) {\n    // Match the established phone/BLE path: register the ACK generation after\n    // sendMessage() has returned from packet handoff.\n    const unsigned long msg_sent = _ms->getMillis();\n    expected_ack_table[slot].msg_sent = msg_sent;\n    expected_ack_table[slot].ack = expected_ack;\n    expected_ack_table[slot].contact = stored;\n    next_ack_idx = (slot + 1) % EXPECTED_ACK_TABLE_SIZE;\n\n    g_compact_ack_tracks[track_slot].ack = expected_ack;\n    g_compact_ack_tracks[track_slot].slot = (uint8_t)slot;\n    g_compact_ack_tracks[track_slot].msg_sent = msg_sent;\n    g_compact_ack_tracks[track_slot].confirmed = false;\n\n    memset(&g_compact_send_start, 0, sizeof(g_compact_send_start));'
)
replace_once(
    mesh_delivery,
    'bool MyMesh::isCompactAckPending(uint32_t ack) const {\n  int track_idx = findCompactTrack(ack);',
    'void MyMesh::noteCompactAckReceived(uint32_t ack, unsigned long msg_sent) {\n  int track_idx = findCompactTrack(ack);\n  if (track_idx < 0) return;\n  CompactAckTrack& track = g_compact_ack_tracks[track_idx];\n  if (track.msg_sent == msg_sent) track.confirmed = true;\n}\n\nbool MyMesh::isCompactAckPending(uint32_t ack) const {\n  int track_idx = findCompactTrack(ack);'
)
replace_once(
    mesh_delivery,
    '  CompactAckTrack& track = g_compact_ack_tracks[track_idx];\n  if (track.slot >= EXPECTED_ACK_TABLE_SIZE) return true;',
    '  CompactAckTrack& track = g_compact_ack_tracks[track_idx];\n  if (track.confirmed) {\n    memset(&track, 0, sizeof(track));\n    return false;\n  }\n  if (track.slot >= EXPECTED_ACK_TABLE_SIZE) return true;'
)

mesh_h = "examples/companion_radio/MyMesh.h"
replace_once(
    mesh_h,
    '  bool isCompactAckPending(uint32_t ack) const;\n  void releaseCompactAck(uint32_t ack);',
    '  bool isCompactAckPending(uint32_t ack) const;\n  void noteCompactAckReceived(uint32_t ack, unsigned long msg_sent);\n  void releaseCompactAck(uint32_t ack);'
)

mesh_cpp = "examples/companion_radio/MyMesh.cpp"
replace_once(
    mesh_cpp,
    '    if (memcmp(data, &expected_ack_table[i].ack, 4) == 0) { // got an ACK from recipient\n      out_frame[0] = PUSH_CODE_SEND_CONFIRMED;',
    '    if (memcmp(data, &expected_ack_table[i].ack, 4) == 0) { // got an ACK from recipient\n#ifdef MESHCORE_COMPACT_UI\n      // Latch the real receive event before the shared table token is cleared.\n      // Compact still requires this positive protocol evidence for Confirmed.\n      noteCompactAckReceived(expected_ack_table[i].ack, expected_ack_table[i].msg_sent);\n#endif\n      out_frame[0] = PUSH_CODE_SEND_CONFIRMED;'
)

# Retention contracts for the two hardware-visible regressions and direct ACK
# latching.  These checks deliberately avoid touching the validated GT911 path.
ui_test = "tests/CommunicatorCompactUiChecks.sh"
replace_once(
    ui_test,
    'require "$TASK" \'redrawHeaderActionIconsFluent\' \'runtime must use the Fluent icon layer rather than the v10 line-art fallback\'\n',
    'require "$TASK" \'redrawHeaderActionIconsFluent\' \'runtime must retain the Fluent icon layer\'\nrequire "$UI" \'redrawHeaderActionIconsFluent(d);\' \'header must paint Fluent icons in-place before body rendering can expose legacy glyphs\'\nforbid "$UI" \'drawRadioGlyph(d,256,21)\' \'header must not paint the obsolete radio line-art glyph first\'\nforbid "$UI" \'drawGear(d,297,21)\' \'header must not paint the obsolete gear line-art glyph first\'\nrequire "$UI" \'if(_dirty==DIRTY_COMPOSER)\' \'keypress redraw must use the composer-only fast path\'\nrequire "$UI" \'d.fillRect(12,211,181,15)\' \'keypress redraw must be bounded to the input text interior instead of the full composer strip\'\n'
)

delivery_test = "tests/CommunicatorCompactDeliveryChecks.sh"
replace_once(
    delivery_test,
    'MESH_H="$ROOT/examples/companion_radio/MyMesh.h"\nMESH_DELIVERY_CPP="$ROOT/examples/companion_radio/ui-compact/MyMeshCompactDelivery.cpp"',
    'MESH_H="$ROOT/examples/companion_radio/MyMesh.h"\nMESH_CPP="$ROOT/examples/companion_radio/MyMesh.cpp"\nMESH_DELIVERY_CPP="$ROOT/examples/companion_radio/ui-compact/MyMeshCompactDelivery.cpp"'
)
replace_once(
    delivery_test,
    'require "entry.msg_sent != track.msg_sent" "$MESH_DELIVERY_CPP" "overwritten ACK slots must not become false confirmations"\n',
    'require "entry.msg_sent != track.msg_sent" "$MESH_DELIVERY_CPP" "overwritten ACK slots must not become false confirmations"\nrequire "noteCompactAckReceived" "$MESH_CPP" "real MeshCore ACK receive path must latch compact confirmation before clearing the shared token"\nrequire "track.confirmed" "$MESH_DELIVERY_CPP" "compact ACK tracker must retain explicit positive receive evidence"\nrequire "const unsigned long msg_sent = _ms->getMillis();" "$MESH_DELIVERY_CPP" "compact ACK registration timing must match the established post-handoff phone/BLE pattern"\n'
)

# Stamp a distinct hardware-test build so the reported v13 binary can never be
# confused with this regression-fix candidate.
workflow = ".github/workflows/communicator-compact-ci.yml"
replace_once(workflow, 'FIRMWARE_VERSION: compact-v13-piece4-complete', 'FIRMWARE_VERSION: compact-v14-ui-ack-fix')
completion = "tests/CommunicatorCompactPiece4CompleteChecks.sh"
replace_once(completion, "require 'compact-v13-piece4-complete' \"$WORKFLOW\"", "require 'compact-v14-ui-ack-fix' \"$WORKFLOW\"")

doc = "docs/communicator-compact-piece4-complete.md"
replace_once(doc, '`compact-v13-piece4-complete`.', '`compact-v14-ui-ack-fix` (hardware regression-fix candidate).')

print("Communicator Compact v14 source patch applied successfully")
