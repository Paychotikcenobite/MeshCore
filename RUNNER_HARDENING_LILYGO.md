# LilyGO self-hosted runner hardening

This project mirrors the proven MeshCore Communicator delivery model without disturbing the existing Android runner.

## Architecture

- Android project runner root: `C:\actions-runner` — leave unchanged.
- LilyGO project runner root: `C:\actions-runner-lilygo`.
- LilyGO runner name: `lilygo-builder`.
- LilyGO runner label: `lilygo`.
- LilyGO watchdog task: `LilyGo-Runner-Watchdog`.
- LilyGO workflow: `.github/workflows/tdeck-self-hosted-build.yml`.
- Hardware-test outputs are GitHub prerelease assets, not Actions artifacts.

The repository is public, so routine `ubuntu-latest` CI compute is free. Routine CI therefore stays hosted for fast validation but retains no artifacts. The self-hosted runner is used for durable hardware-test candidate delivery and to match the Android project's zero-artifact workflow pattern.

## One-time LEVIATHAN setup

The only step that requires local machine access is registering the second repository-level GitHub runner. A repository runner cannot be shared between two personal repositories, so the existing Android registration must not be reused or removed.

1. In `Paychotikcenobite/MeshCore`, open **Settings → Actions → Runners → New self-hosted runner** and copy the temporary registration token shown for Windows x64.
2. Open **Administrator PowerShell** on LEVIATHAN.
3. Download the bootstrap script from the `communicator-compact` branch or run it from a local checkout.
4. Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\bootstrap-lilygo-runner.ps1 -RegistrationToken '<temporary token>'
```

The registration token is one-time/short-lived and must never be committed to GitHub.

The bootstrap script:

- installs the runner into the separate `C:\actions-runner-lilygo` root;
- registers `lilygo-builder` only to `Paychotikcenobite/MeshCore`;
- configures Automatic service startup and service recovery at 60s, 120s, then 300s;
- installs persistent Python 3.13 + PlatformIO under the LilyGO runner tool cache;
- reuses the already-proven PortableGit installation from the Android runner when available;
- installs a SYSTEM watchdog at startup and every five minutes;
- keeps the machine awake on AC power;
- does not reconfigure the Android runner.

## Verification

After bootstrap completes, verify the output reports:

- service status `Running`;
- runner name `lilygo-builder`;
- label `lilygo`;
- PlatformIO version successfully printed;
- watchdog installed;
- AC standby disabled while plugged in.

Then manually dispatch **T-Deck Communicator candidate (self-hosted)** from the Actions tab. A successful run should create a prerelease containing the T-Deck firmware ZIP plus SHA-256 file and should create **no Actions artifact**.

## Private Android project reference

The private `Paychotikcenobite/meshcore-communicator` repository remains the shared cross-project history/knowledge store. Access it through authenticated GitHub tooling when needed. Do not sync or copy its private `CHAT_ARCHIVE/` contents into this public repository.
