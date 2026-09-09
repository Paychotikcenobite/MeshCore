# MeshCore Communicator Compact — current state

## Repository / branch

- Repository: `Paychotikcenobite/MeshCore`
- Development branch: `communicator-compact`
- Draft PR: #1
- Current branch head before runner/quota hardening: `babf672bb88aeb5b752dde84db7afb18095aacc9`
- Current CI firmware identity in the branch: `compact-v16-piece5-advert-aa`

The PR description still mentions an older v10 hardware-test candidate and is therefore not sufficient by itself to determine the latest branch state.

## Preserved implementation state

The current branch contains the standalone LilyGO T-Deck Communicator work through the existing Compact implementation and its retained contract checks. The validated touch/keyboard path, Compact messaging/persistence work, contact/group work, advert handling, and anti-aliased text infrastructure must be preserved unless a requested change explicitly touches them.

## Build / quota policy

Routine public-repository CI is retained for regression checking and compile validation, but routine jobs must not upload GitHub Actions artifacts.

Hardware-test firmware is delivered through a manual self-hosted LEVIATHAN workflow using a dedicated repository-level runner named `lilygo-builder` with label `lilygo`. Candidate binaries are published as GitHub prerelease assets, matching the proven delivery model used by the private Android MeshCore Communicator project.

The Android runner at `C:\actions-runner` is a separate repository-level runner and must not be reconfigured or repurposed for this repository. The LilyGO runner uses `C:\actions-runner-lilygo`.

## Shared project knowledge

The private repository `Paychotikcenobite/meshcore-communicator` is the canonical cross-project knowledge store. It contains the complete Android development history, continuity documents, and `CHAT_ARCHIVE/` material. LilyGO development should consult it whenever Android parity or prior design decisions matter. Private history must not be copied into this public repository.

## Next requested work

Redesign the LilyGO Communicator UI while preserving functionality:

- move away from the washed-out light-blue-on-dark-blue theme;
- use a modern Samsung/Android One UI-inspired light visual language;
- substantially increase readable font sizes;
- use polished proportional anti-aliased sans-serif fonts rather than terminal-like text;
- redesign spacing, row heights, cards, buttons, inputs, and screen density around the larger typography;
- retain all current navigation, touch, keyboard, messaging, persistence, radio, and device-management functionality.

Physical T-Deck testing remains the final acceptance gate.
