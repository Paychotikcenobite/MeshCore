# Communicator Compact Piece 3 — persistent local data engine

This document describes the first hardware-test candidate for roadmap Piece 3 on the standard LilyGO T-Deck.

## Storage model

The Compact UI uses the ESP32 SPIFFS instance that MeshCore already mounts before `UITask::begin()`. It does not depend on the microSD card being present.

Message history is stored in `/mcc_history_v1.bin` as a versioned append journal. Each fixed-size record carries a CRC32 and contains:

- stable 64-bit local message ID
- reserved 64-bit reply-to message ID
- timestamp
- stable conversation kind and 32-byte identity key
- outgoing/unread flags
- send state
- route/path length metadata
- last-known display name for presentation only
- message text

Known direct conversations are keyed by the full 32-byte contact public key. Known groups are keyed by the full channel secret. Display names are never the primary persistent identity. If an incoming callback cannot yet be resolved to a known contact/channel, the record is marked as unresolved and receives a deterministic fallback key; it can later be resolved when the radio contact/channel data is available.

## Durability and recovery

The journal is append-only during normal use. A power loss during the last write cannot invalidate earlier complete records: startup accepts the valid prefix and schedules compaction when it sees a partial/corrupt tail.

When the journal reaches the bounded compaction threshold, the newest live working set is written to a temporary file, then swapped into place with a backup/restore rename sequence. The on-screen working set is deliberately bounded at 96 messages for this first T-Deck candidate.

An incompatible/corrupt file header is preserved as a `.bad` recovery file and a new version-1 journal can be started rather than preventing the UI from booting.

## Drafts

Conversation drafts are stored separately in `/mcc_drafts_v1.bin`. Drafts are keyed by the same stable contact/channel identity as history and are written with a temporary-file/rename transaction. Entering a conversation restores its draft; sending or clearing the composer removes the saved draft.

## What Piece 3 does and does not provide

Piece 3 provides the durable substrate needed by later roadmap pieces: stable IDs, durable text history, unread state, draft state, stable conversation identity, reply-ID storage space, and routing/send metadata.

It does **not** claim Piece 4 messaging semantics yet. In particular, the current UI's direct send path still does not register the expected ACK in MyMesh's private ACK table, so a persisted `Sent` state must not be interpreted as Communicator-style `Confirmed`. Piece 4 will wire the stable IDs and persistent metadata into truthful queued/sending/confirmed/failed/retry states.

It also does not yet implement SD export/import or encrypted backup packages. Those remain Piece 11; this phase only ensures that ordinary reboot/power cycles no longer throw away the local working history and drafts.