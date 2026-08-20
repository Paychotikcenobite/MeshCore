# MeshCore Communicator Compact — Piece 4 completion record

Piece 4 (Text messaging correctness and delivery state) is complete on the `communicator-compact` branch for the current MeshCore text protocol and standard LilyGO T-Deck target.

## Implemented behavior

- Direct and group text sends cross explicit Compact-only `MyMesh` wrappers while the existing phone/BLE pointer-based paths remain unchanged.
- Direct sends register their expected ACK in `MyMesh`'s ACK table. `Confirmed` is written only after positive ACK reconciliation; legacy optimistic `Sent` records are never upgraded without proof.
- Shared ACK-table slot reuse is generation-checked so another sender cannot create a false confirmation.
- Direct send state is durable: `Sending`, `Confirmed`, `Failed`, and local `Stopped` survive the UI/persistence lifecycle. Failed messages drive Needs Attention and can be retried.
- `Stop waiting (local)` only abandons local ACK tracking after RF handoff. It never claims that an already-emitted packet was cancelled.
- Group `Sent` means local flood-packet handoff only. Group text has no per-peer ACK and never claims peer delivery.
- Direct attempt number and planned route are persisted. Existing schema-v1 journal size/version are preserved by using previously reserved flag bits; old records therefore load with attempt unknown rather than fabricated metadata.
- Retry starts with MeshCore's extended attempt form (attempt 4) and advances the stored attempt on subsequent retries. Attempt 15 is a hard local ceiling so the 4-bit persistent representation never wraps silently.
- Delivery details report the actual planned route for the recorded attempt: directed with hop count, or flood/no stored path.
- This firmware has no automatic direct-to-flood timeout fallback (`MyMesh::onSendTimeout()` is empty). A timeout therefore becomes Failed; the UI does not invent fallback bookkeeping that did not happen.
- Message long-press provides delivery/reception details, retry or local stop-waiting when applicable, local delete, and Reply.
- Reply uses the persistent history `reply_to` stable message ID and is explicitly local. The current MeshCore plain/group text payload has no protocol reply-to field, so no incompatible RF extension is transmitted.
- Conversation search uses the persistent history working set.
- New-message boundary and newest navigation are adapted to 320x240. The unread boundary is captured before the existing read-clear path for both touch-open and keyboard/trackball Enter; trackball-right jumps to newest while scrolled.

## Truthful state model

There is no fabricated durable pre-handoff `Queued` stage in this implementation because the current local send API synchronously returns packet-handoff success/failure. A successful direct handoff enters `Sending` while awaiting the real ACK; local handoff failure enters `Failed`. A successful group handoff enters `Sent` without implying receiver confirmation.

## Compatibility and safety

- No MeshCore packet format was changed.
- No normal radio defaults were changed.
- The standard T-Deck GT911 v8 recovery/polling baseline was not modified.
- The standard non-Compact T-Deck reference target remains separately built in CI.
- No physical device flashing is part of Piece 4 completion.

The release candidate for this completed Piece 4 scope is identified in CI as `compact-v13-piece4-complete`.
