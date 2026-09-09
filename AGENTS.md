# MeshCore Communicator Compact repository rules

This branch contains the standalone LilyGO T-Deck port of MeshCore Communicator.

## Shared private project reference

The Android MeshCore Communicator repository `Paychotikcenobite/meshcore-communicator` is the canonical private cross-project knowledge store. It contains the complete development continuity material, accepted Android behavior, project handoff documents, test history, and archived chat/development logs.

Before making a LilyGO change that depends on Android behavior, parity, prior design decisions, or historical context, read the relevant material from the private repository through the authenticated GitHub connection. At minimum, consult these files when applicable:

- `AGENTS.md`
- `CURRENT_STATE.md`
- `PROJECT_HANDOFF.md`
- `ARCHITECTURE.md`
- `BUILD_AUTOMATION.md`
- `CHANGELOG.md`
- `docs/USER_MANUAL.md`
- `CHAT_ARCHIVE/`
- `SHARED_PROJECT_CONTEXT.md` when present

Do not copy private chat archives or private project-history documents into this public repository. The public LilyGO repository should contain only the minimum references needed to locate the private source material. If the private repository cannot be read with the current authenticated GitHub connection, stop and resolve access rather than reconstructing history from memory.

## Before making any change

1. Read this file and `CURRENT_STATE.md`.
2. Identify the current `communicator-compact` head and the last hardware-accepted behavior before editing.
3. Inspect the existing implementation related to the request; do not replace whole screens/modules because it seems easier.
4. Preserve the validated GT911 touch path, keyboard behavior, MeshCore protocol/radio behavior, persistence, messaging/ACK behavior, and Launcher recovery/install path unless the requested change specifically requires touching them.
5. Make the smallest change that satisfies the request, then run the Compact contract checks and a real firmware build.

## UI redesign standard

The current redesign is intentionally allowed to replace the old visual language while preserving functionality.

- Target a polished modern Android/One UI-inspired visual quality adapted to 320x240.
- Use larger, proportional, anti-aliased sans-serif typography with clear Regular/Medium/Bold hierarchy.
- Do not use terminal-like, monospaced, blocky, or retro-looking text for normal UI.
- Reflow layouts around the larger typography instead of merely enlarging existing text in place.
- Prefer light neutral backgrounds, white/light cards, dark primary text, muted secondary text, restrained blue accent, rounded controls, and clean spacing.
- Keep touch targets comfortably large and preserve keyboard/touch usability on real T-Deck hardware.
- Preserve all existing features and information architecture unless the user explicitly requests a functional change.

## Validation and delivery

Routine CI on the public repository may use GitHub-hosted runners but must not retain Actions artifacts. Downloadable hardware-test firmware is produced by the manual self-hosted workflow and published as GitHub prerelease assets.

A successful compile is not the same as hardware acceptance. Physical T-Deck behavior remains the acceptance gate for UI, touch, keyboard, radio, and persistence changes.
