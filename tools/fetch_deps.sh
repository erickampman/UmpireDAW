#!/usr/bin/env bash
#
# Initialise every dependency submodule Umpire needs, at the pinned commits.
#
# Run this once after a fresh `git clone`, and again after a `git pull` that
# moves, adds or re-points a submodule.
#
# Note on vst3sdk (the plugin's VST3 build, milestone P1): it declares seven
# nested submodules. Only base, cmake, pluginterfaces and public.sdk are
# needed; doc, tutorials and vstgui4 are left uninitialised, as Retune does,
# so a checkout stays small, which a bare `--init --recursive` wouldn't do.
#
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> Submodule URLs (after a pull that changed one)"
git submodule sync --recursive

echo "==> Submodules: uw-core, dpf (with its pugl fork), DPF-Widgets, clap-wrapper"
git submodule update --init --recursive external/uw-core external/dpf external/DPF-Widgets external/clap-wrapper

echo "==> vst3sdk: nested base, cmake, pluginterfaces, public.sdk"
git submodule update --init external/vst3sdk
git -C external/vst3sdk submodule update --init --recursive base cmake pluginterfaces public.sdk

echo "==> Done. Configure with: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release"
