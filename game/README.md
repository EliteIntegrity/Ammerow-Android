# The game

This folder holds the source of Ammerow: Lands Beyond itself: the engine, the
SDL3 frontend, the game data with its authoring inputs and generators, and
the game's tests. It is the desktop edition's published source with the
Android changes listed in [ANDROID-CHANGES.md](../ANDROID-CHANGES.md).

- The Android app compiles the sources listed in `sources.cmake`.
- `CMakeLists.txt` is the desktop build. Here it is used to run the game's
  tests (see the repository [README](../README.md)).
- Generated data files are written by the scripts in `utils/` from their
  inputs; see [docs/data-authoring.md](docs/data-authoring.md).
- Paths in these files are relative to this folder, except the licence texts
  (`third_party/licenses/` on desktop) and the top-level notices (`COPYING`,
  `NOTICE.md`, `AUTHORS.md`, `ASSET-LICENSE.md`, `THIRD_PARTY_NOTICES.md`,
  `docs/copying.rst`, `docs/thanks.rst`): those are at the repository root,
  with the licence texts in `licenses/`.
