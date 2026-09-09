# Communicator Compact v2 hardware-test build

Build source: `communicator-compact` draft PR #1.

## Product/UI reference

The v2 UI is grounded in the actual `Paychotikcenobite/meshcore-communicator` Android project rather than the earlier conceptual card UI. The native 320×240 T-Deck port uses the Communicator interaction structure: persistent MeshCore / Communicator header, radio and settings actions, Chats / Repeaters main tabs, searchable conversation rows, New conversation, conversation header/details, message bubbles, composer, radio status, settings, and repeater details.

## T-Deck hardware behavior

- GT911 touchscreen is the primary navigation input.
- Physical keyboard is primary text input.
- Trackball remains an optional fallback.
- Compact display frames no longer clear the whole ST7789 before every redraw; this removes the old periodic full-screen flicker.
- The custom build is isolated from the upstream/reference T-Deck environment.

## Radio preset

The custom target pre-includes `variants/lilygo_tdeck/compact_radio_config.h` so the inherited EU base defaults are replaced before radio headers are parsed:

- 910.525 MHz
- SF7
- BW62.5
- CR5

The standard reference target is unchanged.

## Verified CI build

Head commit: `956a4515bac86792264eb40d535f8cb447aa0367`

GitHub Actions run `32225505833` completed successfully, including both the unchanged standard LilyGO T-Deck BLE reference build and the Communicator Compact target. Unit tests and PR build checks also passed for the same head.

Launcher application binary from the successful workflow artifact:

- size: 1,251,504 bytes
- SHA-256: `270e0f352e583d3fc83f1616aee58c4fc95e626d473f71d84298016ca98b77ea`
- use the non-merged application binary with Launcher; do not use the merged image.

## Current intentional boundaries

This is the next hardware-test build, not a claim of full Android beta200 feature parity. Core native text messaging/contact/channel/repeater/settings/radio UI is present. Full Android voice transport, long-term encrypted Android-style history, Expedition, and the richer map/network-health screens remain later porting work. Direct-send delivery ACK registration is also not yet bridged into the private `MyMesh` expected-ACK table.
