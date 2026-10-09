# Ammerow for Android

The Android edition of *Ammerow: Lands Beyond*, a roguelike that began from
Angband: the game's C and SDL3 source with touch controls of its own.
Landscape, touch first (physical keyboards and game controllers also work).
The run is saved whenever Android sends the game to the background.

## Licence

- **Program and game data: GNU GPL version 2** ([COPYING](COPYING)). See
  [NOTICE.md](NOTICE.md), [AUTHORS.md](AUTHORS.md) and
  [docs/copying.rst](docs/copying.rst) for the inherited Angband, Moria and
  Umoria notices.
- **Artwork, audio and the official icon are not in this repository.**
  Ammerow's ASCII-art plates, Hybrid sprite sheets, sound effects, music and
  launcher icon are separately licensed ([ASSET-LICENSE.md](ASSET-LICENSE.md)).
  Official releases include them. A build from this repository uses plain
  glyphs and synthesised sound effects, has no music, and shows a generic icon.
- **Third-party libraries and fonts** keep their own licences:
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/`.
- **The Ammerow name** is a trade mark: a public fork must use its own name,
  application ID and icon (see [NOTICE.md](NOTICE.md)).

## Layout

| Path | Contents |
| --- | --- |
| `game/` | The game: engine, SDL3 frontend, data with its generators, and tests ([game/README.md](game/README.md)). Its Android changes are listed in [ANDROID-CHANGES.md](ANDROID-CHANGES.md). |
| `app/` | The Android app: Kotlin touch interface, native glue in `app/src/main/cpp/android/`, and the Gradle and CMake build. |
| `licenses/`, `docs/` | Licence texts and the inherited notices. |
| `tools/` | Dependency fetchers, and the source check with its tests. |

## Requirements

- Android SDK platform 36, NDK `30.0.16248370` and CMake `3.31.6` (from
  Android Studio's SDK Manager), and JDK 17 or later (Android Studio's
  bundled one works).
- Git, and Python 3.12 or later for the source check and data generators.

## Build and install

1. `tools/fetch-deps.sh` (or `tools/fetch-deps.ps1` on Windows) clones SDL
   3.4.16, SDL_image 3.4.6, SDL_ttf 3.2.2 (with FreeType and HarfBuzz) and
   SDL_mixer 3.2.4 into `third_party/`, checking each pinned commit.
2. `./gradlew assembleDebug` (`gradlew.bat` on Windows), with `JAVA_HOME`
   set and the SDK named by `ANDROID_HOME` or by `sdk.dir` in
   `local.properties`. `-Pammerow.abis=x86_64` builds only what an emulator
   needs. Or open the folder in Android Studio after step 1.
3. With a device (USB debugging on) or an emulator connected:
   `./gradlew installDebug`, or
   `adb install -r app/build/outputs/apk/debug/app-debug.apk`. The app needs
   Android 8.0 or later on arm64-v8a or x86_64.

The build lays out the game's runtime files from the manifests in
`game/packaging/` and puts the licence texts in the APK's `assets/legal/`.
`./gradlew assembleRelease` leaves the APK unsigned unless a
`keystore.properties` file (not tracked) names a key with `storeFile`,
`storePassword`, `keyAlias` and `keyPassword`. An APK signed with another key
cannot update an installed official release.

Official releases add the separately licensed files from a private asset pack
(`ammerow.assetPack` in `local.properties`, or `-Pammerow.assetPack=...`) and
carry their own source as `assets/source/Ammerow-Android-source.zip`
(`-Pammerow.sourceArchive=...`, from `python tools/check_source.py --export`).

## Tests

- `python tools/check_source.py` checks the published file list, media,
  secrets, links and release details, and that the data generators in
  `game/utils/` reproduce the game data from their inputs (see
  [game/docs/data-authoring.md](game/docs/data-authoring.md));
  `python -m unittest discover -s tools/tests` tests that check.
- The game's own tests run on a desktop build of `game/`, with CMake, a C
  compiler and the SDL3, SDL3_ttf and SDL3_image development packages:

  ```
  cmake -S game -B build/game-tests -DSUPPORT_SDL3_FRONTEND=ON -DSUPPORT_WINDOWS_FRONTEND=OFF -DSUPPORT_HEADLESS_FRONTEND=ON -DSUPPORT_TEST_FRONTEND=ON -DSUPPORT_SPOIL_FRONTEND=OFF -DCMAKE_PREFIX_PATH="<SDL3 packages>"
  cmake --build build/game-tests --config Release --target allunittests
  ```


## How it fits together

- `app/src/main/cpp/CMakeLists.txt` builds SDL, then the game from the
  lists in `game/sources.cmake`.
- `android-main.c` installs the runtime files from the APK into internal
  storage on first run or after an update, then runs the game's `main()`.
- `context.c` tells the touch controls what the game is waiting for, through
  link-time wraps of the game's input functions; each wrapper's signature is
  checked against the game's declaration (`TOUCH_CHECK_WRAP` in `touch.h`).
- `pins.c` keeps the quick bar; `touch-text.c` rewords keyboard hints for
  touch as they are drawn.
- `AmmerowActivity` (an `SDLActivity`), `ControlLayouts` (the buttons for
  each mode) and `TouchControlsView` (a Canvas overlay) are the Kotlin side.
  Every control presses the game's own keys; the in-game field guide
  explains them to players.

A new file is published only once it is listed in `tools/source-files.txt`.
When an Android change touches a file in `game/`, list it in
[ANDROID-CHANGES.md](ANDROID-CHANGES.md).
