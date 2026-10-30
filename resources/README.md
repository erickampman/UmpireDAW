# resources

What Umpire is built with besides code, and where each came from.

- `Umpire.icon`: the app icon, an Icon Composer document (three tracks and a
  wave over a dark green gradient). The Mac build compiles it with Xcode's
  `actool` (CMakeLists.txt; BUILDING.md, "macOS").
- `linux/icons/umpire-daw-*.png`: the Linux icons, made from the `.icon`'s
  artwork by `tools/render-linux-icons.sh` (rounded corners and a margin, at
  48 to 512 px), as StageHand's. The 128 and 48 px ones are embedded as the
  windows' icon (`src/Ui/WindowIcon.cpp`); `tools/install-desktop-entry.sh`
  installs them all with `linux/umpire-daw.desktop.in`, the launcher
  (BUILDING.md, "The dock icon").
