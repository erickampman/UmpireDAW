# Naming

The product is **Umpire**, a play on UMP. "MIDI" stays out of every name
because Umpire may grow audio support later, the way MOTU's Performer became
Digital Performer.

| Where | Name |
|---|---|
| Product name (window titles, About box, menus, docs) | Umpire |
| Logo / wordmark only | UMPire |
| Repo, folder, CMake `project()` | UmpireDAW |
| Linux binary and distro package | `umpire-daw` |
| Linux `.desktop` file | `Name=Umpire`, `Exec=umpire-daw` |
| Linux app ID (Flatpak, AppStream) | `com.unlikelyware.Umpire` |
| macOS app | `Umpire.app` |
| macOS bundle ID | `com.unlikelyware.umpire` |
| Settings and data, Linux | `~/.config/umpire-daw`, `~/.local/share/umpire-daw` |
| Settings and data, macOS | `~/Library/Application Support/Umpire` |
| C++ namespace | `umpire` |

## Why the `-daw` suffix

LLNL's [Umpire](https://github.com/llnl/Umpire), a memory-management library
for high-performance computing, already owns `umpire` and `libumpire` on
Linux (Arch packages `umpire` and `umpire-cuda`, plus HPC package managers).
So anything that shares a system-wide namespace on Linux (the binary, distro
packages, XDG folders) gets the `-daw` suffix.

Reverse-DNS identifiers (`com.unlikelyware.*`) are already unique, so they
use the plain product name, as does everything the user sees.

## C++ namespace

LLNL Umpire also uses `namespace umpire`. That only matters if both end up
in the same process, and a GPU memory allocator won't be loaded into a DAW.
If Umpire ever installs a public library for others to link against, switch
that library to `umpire_daw`.

## Conventions shared with StageHand

These follow StageHand's: project `StageHand`, binary `stagehand`, bundle ID
`com.unlikelyware.stagehand`, namespace `stagehand`.

## Names considered

- **TooTone**: clear of conflicts, but when spoken it is indistinguishable
  from the many TwoTone companies.
- **2Tone**: 2 Tone Records (ska), the TwoTone data-sonification tool, and
  Tone2 (soft synths).
- **Resolute**, **Umami** (Umami Analytics), **Twofold**: already taken by
  other software or companies.
