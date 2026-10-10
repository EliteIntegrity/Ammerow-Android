# Android changes to the game

`game/` is the game's own source, as published for the desktop edition
(version 0.1.1), with the changes below made for Android in 2026 by John
Horton. Everything else in `game/` is unchanged.

| Change | Files in `game/` |
| --- | --- |
| The map camera keeps its edges clear of the touch controls and the message history, fits the text grid between strips the host covers, shows a scaled preview while pinching, starts a side-view cave's status strip clear of the d-pad, and lets Hybrid's magnified cells follow the zoom continuously. | `src/sdl3/render.c`, `render.h`, `render-internal.h`, `zoom.c`, `zoom.h` |
| Authored sounds and music ship as Ogg Vorbis: effects are decoded once, background loops and music stream from the file (new `src/sdl3/audio-ogg.c`, `.h`). | `src/snd-sdl3.c` |
| Touch hooks: pinch zoom, taps on shop and item-list rows pick the row that is drawn, a first tap only highlights while a list is browsed, a tap on the message history opens the log, the host's buttons stand in for the fishing rig's key panel, and the fishing scene keeps clear of the controls (new `src/sdl3/host.h`). | `src/sdl3/fishing.c`, `frontend-events.c`, `frontend-internal.h`, `frontend-settings.c`, `presenter.c`, `presenter.h` |
| A tap, click, Enter or Right past the last value of a setting that stops at its ends (zoom, message rows, volumes; interface size cycles by itself) comes back round to the first, so a tap can always undo a change. | `src/sdl3/frontend-settings.c` |
| The sidebar keeps its rows above the d-pad, leaving out its least important when they do not all fit, as on a shorter screen (`sidebar_covered_rows_hook`, set by the app); an examine card shorter than the full card keeps its facts, with a smaller portrait or none, and facts a card does not have take no rows. | `src/ui-display.c`, `src/ui-display.h`, `src/sdl3/inspect-card.c`, `monster-card-layout.c`, `src/tests/sdl3/monster-card-layout.c`, `src/tests/ui/sidebar.c` |
| Taps open field guide sections and turn its pages. | `src/ui-help.c` |
| An item's context-menu actions can be listed (`context_menu_object_actions`), so the touch buttons offer the same ones. | `src/ui-context.c`, `src/ui-context.h` |
| The *Top right* message history starts a line down, clear of the game's own message line, and the camera keeps the player a row clear of it. | `src/sdl3/layout.c`, `src/tests/sdl3/layout.c` |
| The map's own window of squares covers the whole drawing, which side controls can make wider than the text grid. | `src/main-sdl3.c`, `src/sdl3/map-presenter.c`, `map-presenter.h` |
| Touch versions of field guide pages (new `lib/help/touch.txt`), listed in the release manifest. | `lib/help/index.txt`, `interface.txt`, `world.txt`, `fishing.txt`, `spelunking.txt`, `packaging/runtime.txt` |
| The legal screen describes the Android edition. | `lib/screens/legal.txt` |
| The field guide's test opens the keyboard commands from the Android index, where they are (g). | `src/tests/sdl3/help-screen.c` |
| The unit-test list check skips suites whose source is not in the tree (the desktop's private identity audit, which the published source leaves out), so this tree configures. | `src/cmake/scripts/check_test_manifest.cmake` |
| The Android build's source lists, and this folder's guide (both new). | `sources.cmake`, `README.md` |
