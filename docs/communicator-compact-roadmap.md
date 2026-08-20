# MeshCore Communicator Compact — implementation roadmap

Target hardware: standard LilyGO T-Deck, 320x240 ST7789, GT911 touch, physical keyboard, local SX1262 radio.

Product reference: the accepted MeshCore Communicator Android beta219 behavior and information architecture. The T-Deck version should preserve that product model wherever the hardware allows it. Differences must be deliberate, documented, and user-visible rather than silently omitted.

## Non-negotiable baseline

The physically validated v8 T-Deck hardware path is frozen as the input/display baseline:

- GT911 touch remains on the proven recovery path and polling semantics.
- Physical keyboard remains the primary text-entry device.
- Trackball remains optional fallback navigation.
- Compact ST7789 rendering remains dirty-region driven; no periodic whole-screen clear/flicker.
- Launcher remains supported with the standalone application binary.
- Radio defaults and normal MeshCore protocol/radio behavior remain isolated from the upstream reference target.

A later phase may optimize boot time, but no phase may trade away touch reliability, keyboard reliability, or Launcher recovery convenience.

---

## Piece 1 — UI platform and navigation shell

**Goal:** establish the permanent 320x240 application framework before adding feature depth.

Implement:

- Communicator dark/light design tokens derived from the Android app.
- Persistent MeshCore Communicator header.
- Persistent Chats / Repeaters top-level destinations.
- Radio Status and Settings entry points.
- Back-stack semantics for daughter screens.
- Tap, swipe and long-press dispatch without changing the validated GT911 path.
- Physical-keyboard focus rules and Esc/Enter navigation.
- Dirty rendering and screen wake behavior.
- Static CI retention checks for all of the above.

**Acceptance:** touch and keyboard remain physically reliable; top-level tabs never gain a Back button; daughter screens do; header/tabs/navigation are recognizably Communicator; no full-screen periodic flicker.

**Status:** implementation started. The new `CommunicatorAppScreen` shell and v8 input path are on `communicator-compact`; this roadmap adds explicit regression gates.

---

## Piece 2 — Chats home, conversation organization, and New Conversation shell

**Goal:** make the first screen behave like Communicator before deep message storage/transport work.

Implement:

- Search conversations.
- All / Favorites / Unread / Needs attention filters.
- Conversation rows with avatar/initials, title, preview, activity age and unread count.
- Separate Favorite and Pin semantics.
- Local Mute, Mark read/unread, Rename, Archive and Delete-local-history actions.
- Direct-contact nicknames and group aliases stored by stable identity.
- Public / World visibility setting.
- `+ New conversation` daughter screen with direct contacts and configured groups.
- Explicit Add contact / Group chat / Expedition entry points, even while later pieces are still incomplete.
- Long-press menu matching the Android action order.
- CI retention checks for the shell and metadata behavior.

**Acceptance:** filters and metadata actions work without RF side effects; nickname/favorite/pin/mute/archive metadata survives reboot; contact and group identity are not conflated; Public/World does not masquerade as a private group.

**Status:** implementation started. Search, filters, rows, metadata persistence, long-press menu, aliases and New Conversation structure are already in the new Compact screen.

---

## Piece 3 — Persistent local data engine

**Goal:** replace the temporary RAM message cache with a durable, versioned data layer suitable for the remaining features.

Implement:

- Append-safe persistent message records on SPIFFS/LittleFS or SD, with a small index in flash/NVS.
- Stable per-message IDs.
- Conversation index keyed by full contact public key or channel identity, not display name.
- Persistent drafts.
- Unread state and first-unread boundary.
- Reply metadata.
- Per-message routing/send metadata.
- Storage schema version and migration path.
- Bounded retention/compaction so a corrupt or full filesystem cannot brick boot.
- Recovery behavior if the SD card is absent.

**Acceptance:** reboot does not lose history/drafts; names can change without losing conversations; storage corruption is recoverable; the UI never depends on a display name as the database key.

---

## Piece 4 — Text messaging correctness and delivery state

**Goal:** bring direct/group text behavior to the same truthfulness as Communicator.

Implement:

- Direct and group text sends through explicit UI-safe MyMesh wrappers.
- Register direct expected ACKs in MyMesh's expected-ACK table.
- Route/send attempt metadata.
- `Queued`, `Sending...`, `Confirmed`, `Sent`, and `Failed · Retry` semantics matching what the protocol can actually prove.
- Directed-to-flood fallback bookkeeping where applicable.
- Retry and cancel rules.
- Conversation `Needs attention` derived from persistent outbound states.
- Message long-press: Reply, reception details, delivery/send details, retry, delete locally.
- Conversation search against persistent history.
- New messages divider and newest navigation adapted to 320x240.

**Acceptance:** no fake Delivered/Confirmed states; direct ACK completion is real; group messages never claim per-peer delivery; failed sends survive reboot and can be retried.

---

## Piece 5 — Contacts and group administration

**Goal:** complete the standalone T-Deck equivalents of Communicator's contact/group workflows.

Implement:

- Add contact by full public key and `meshcore://contact/add` URI typed/pasted/imported.
- Save/remove/update contacts using verified MeshCore writes.
- Show own contact QR on the screen.
- Import contact/group links from SD and BLE/serial where practical.
- Create/join/edit/leave private groups with write/read-back verification.
- Group invite QR display and text/link export.
- Group details: alias, favorite, pin, mute, archive, conversation color, location sharing.
- Public / World special-case behavior.

**Hardware difference:** the standard T-Deck has no camera, so Android QR scanning cannot be implemented internally.

**Equivalent output:** accept the exact same MeshCore contact/group URI or key through keyboard, SD, or BLE/serial import; still display outbound QR codes for another device to scan.

**Acceptance:** contact/group writes are read-back verified; private group keys are not confused with Public/World; import/export round-trips with Communicator-compatible URIs.

---

## Piece 6 — Radio Status, Device Details, and Settings

**Goal:** preserve Communicator's information hierarchy while adapting from a phone+Wio architecture to one standalone radio.

Implement:

- Local radio identity/status card.
- Battery, firmware/build, frequency, BW, SF, CR, TX power and GPS fields.
- Radio statistics and packet counters when exposed by the firmware.
- Refresh local status without generating unnecessary LoRa traffic.
- Self advert action explicitly labeled as RF traffic.
- Settings sections matching Communicator: Connection & radio, Messaging, Location & maps, Appearance, Data & backup, Advanced.
- Routing preference UI wired to real send behavior.
- Location/privacy controls wired to NodePrefs and MCC behavior.

**Architecture difference:** Android's BLE scan/switch/reconnect screen does not make sense on the standalone T-Deck because its SX1262 is the radio.

**Equivalent output:** Radio Status shows the same categories of identity, configuration and health information for the local device rather than pretending a separate Wio exists.

**Acceptance:** every control either changes a real local setting or is absent/explicitly unavailable; status refresh is local and does not silently transmit on LoRa.

---

## Piece 7 — Repeaters, telemetry/weather, and Network Health

**Goal:** port the non-map network-awareness features.

Implement:

- Known/recent repeater list.
- Recent and Distance sort when a local position exists.
- Recency/freshness colors matching Communicator.
- Saved/not-saved state and route/hop information.
- Repeater details and Save-to-radio action.
- Route Trace with an explicit RF-traffic warning.
- Telemetry request/response plumbing.
- Weather capability detection and environmental report when supported.
- Network Health: activity, route distribution, coverage logging and recently observed nodes.
- Passive analytics store separate from message history.

**Acceptance:** passive analytics never masquerade as global network truth; Route Trace is visibly active RF; weather capability is confirmed rather than guessed.

---

## Piece 8 — Maps and location visualization

**Goal:** reproduce Communicator's map outputs without trying to run Android MapLibre on an ESP32-S3.

Implement in stages:

1. Native 320x240 point/route map with self/contact/repeater markers.
2. Fit-to-points, pan/zoom and north-up reset.
3. SD-backed basemap cache. Prefer a compact preprocessed tile/vector format that can be rendered incrementally with bounded RAM.
4. Network / My coverage / Traffic layers.
5. Heard/not-heard filtering and recency colors.
6. Contact-location and repeater-detail map actions.
7. Expedition marker/trail support used later by Piece 10.

**Platform difference:** Android MapLibre/Mapsforge cannot be transplanted directly with the same runtime and memory model.

**Equivalent output:** preserve the same map information, marker semantics, filters and fit behavior using a native renderer and SD-backed map data.

**Acceptance:** maps remain useful with no internet connection; lack of a basemap never hides positions; RAM use stays bounded while panning/zooming.

---

## Piece 9 — Voice messaging

**Goal:** port the actual MeshCore Communicator voice protocol, not invent an incompatible T-Deck format.

Implement:

- ES7210 microphone capture.
- T-Deck audio output path.
- Opus encode/decode at settings compatible with Communicator.
- MCC voice carrier/control/recovery framing.
- Direct and group voice transport.
- Receiver completion receipts/recovery.
- 30-second recording limit.
- Voice bubble playback/progress.
- Persistent voice file/message metadata.
- Cancel/retry behavior.

**Acceptance:** T-Deck voice messages interoperate with Android Communicator; radio acceptance alone is never presented as completed receiver playback.

---

## Piece 10 — Expedition

**Goal:** implement the full app-level Expedition feature over existing private MeshCore channels.

Implement:

- Expedition group creation/join recognition.
- Full-key roster persistence and convergence.
- Hidden MCC member and location control datagrams.
- Chat / Map / Members workspace adapted to 320x240.
- Manual, 5-minute, 15-minute and 30-minute location update modes.
- Wio/T-Deck-only outward location source and one-touch stop sharing.
- Member heard-age vs location-age distinction.
- Breadcrumb recording/retention.
- Stable per-member pin/trail colors, with self in Communicator blue.
- Reconnect roster-repair schedule and bounded RF gossip.

**Acceptance:** interoperates with Android Communicator Expeditions; no oversized roster text packets; location sharing is opt-in and cadence-controlled; roster self-heals after missed packets.

---

## Piece 11 — Backup, security, and portability

**Goal:** make the standalone device durable without making false claims about Android Keystore equivalence.

Implement:

- Export/import of local history, metadata, drafts, groups, Expedition data and settings to SD.
- Versioned backup manifest and integrity authentication.
- Application-level AES-GCM encryption for sensitive local records/backups.
- User-managed backup passphrase/key option.
- Privacy controls for outward location and on-screen sensitive data.

**Platform difference:** Android Keystore is not available on the T-Deck.

**Equivalent output:** encrypted local records/backups with explicit key-management semantics. Optional ESP32 secure boot/flash encryption can be evaluated separately, but must never be enabled casually because eFuse/security changes can be irreversible and complicate Launcher recovery.

**Acceptance:** restore reproduces the same local state; corrupt/wrong-key backups fail safely; no irreversible hardware security provisioning is performed as part of normal development.

---

## Piece 12 — Integration, performance, power, and release validation

**Goal:** turn the collection of pieces into a reliable standalone product.

Implement/test:

- Boot-stage timing and optimization without hiding storage errors.
- RAM/PSRAM budget and fragmentation checks.
- SD absent/full/corrupt behavior.
- Long conversations and large contact/repeater lists.
- Touch, keyboard, trackball fallback and screen-wake regression tests.
- Radio send/receive under UI load.
- Battery/power behavior and display auto-off.
- No periodic flicker.
- Upgrade/install through Launcher using standalone binaries.
- Reboot persistence and schema migration.
- Cross-device text, group, voice, location and Expedition interoperability with Android Communicator.
- Hardware validation checklist before the draft PR can be considered mergeable for release.

**Acceptance:** all prior piece acceptance criteria pass together on physical hardware; no known feature is represented as working when it is only a placeholder.

---

## Development rule for unavailable features

A control may be present before its backend is complete only when it is useful for preserving the final layout. Selecting it must clearly state:

1. what is unavailable,
2. why it is unavailable,
3. whether the limitation is hardware or unfinished software,
4. the exact planned standalone equivalent.

No placeholder may silently pretend to perform the Android behavior.
