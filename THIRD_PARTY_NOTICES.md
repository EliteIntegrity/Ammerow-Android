# Ammerow for Android: third-party notices

This file lists the third-party components built into the Android APK. It is
an inventory, not a substitute for the complete licence texts in `licenses/`,
which every APK also carries in `assets/legal/`. Update it whenever a pinned
dependency or bundled font changes.

## SDL libraries

The APK contains `libSDL3.so`, `libSDL3_image.so` and `libSDL3_ttf.so`, built
from these sources (pinned in `tools/fetch-deps.ps1` and `tools/fetch-deps.sh`),
together with SDL's Java glue (`android-project/app/src/main/java`):

| library | release | source revision |
|---|---:|---|
| SDL | 3.4.16 | `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` |
| SDL_image | 3.4.6 | `f661fa1ad24ab1b81e43662532f9a6a9fcf67ea6` |
| SDL_ttf | 3.2.2 | `a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b` |
| SDL_mixer | 3.2.4 | `72a81869b45e249e8e67102db4e98dd2441f05a1` |

Only SDL_mixer's SDL-adapted copy of stb_vorbis is used (compiled into
`libmain.so` to decode Ogg Vorbis sound); the rest of SDL_mixer is not built.
SDL, SDL_image, SDL_ttf and SDL_mixer use the zlib licence:
`licenses/SDL-family-zlib.txt`.

- <https://github.com/libsdl-org/SDL/tree/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c>
- <https://github.com/libsdl-org/SDL_image/tree/f661fa1ad24ab1b81e43662532f9a6a9fcf67ea6>
- <https://github.com/libsdl-org/SDL_ttf/tree/a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b>
- <https://github.com/libsdl-org/SDL_mixer/tree/72a81869b45e249e8e67102db4e98dd2441f05a1>

### Code inside those libraries

- HIDAPI, part of SDL (game controller support). Copyright 2009, Alan Ott,
  Signal 11 Software. Used under the original HIDAPI licence:
  `licenses/HIDAPI.txt`.
- FreeType 2.13.2, revision `9973564cfa63763a3e4ac67c09147899539b1e07`, built
  into SDL_ttf, used under the FreeType Project Licence. Portions of this
  software are copyright © 2023 The FreeType Project (www.freetype.org). All
  rights reserved. `licenses/FreeType-FTL.txt`.
- HarfBuzz 8.5.0, revision `564bf9818a18709776856533829c0c04950773d6`, built
  into SDL_ttf, under its old MIT licence, including the separately noticed
  Microsoft Universal Shaping Engine data: `licenses/HarfBuzz.txt` and
  `licenses/HarfBuzz-Microsoft-USE.txt`.
- stb_rect_pack 1.01 (SDL_ttf), stb_image 2.30 (SDL_image's PNG decoder) and
  stb_vorbis 1.22 (from SDL_mixer), by Sean Barrett and contributors, under
  their offered MIT/public-domain terms: `licenses/STB-dual-license.txt`.

PlutoSVG and PlutoVG, which SDL_ttf can also include, are not built.

## Kotlin standard library

The app's Kotlin code uses the Kotlin standard library 2.2.10 and
`org.jetbrains:annotations` 13.0, compiled into `classes.dex`. Copyright
JetBrains s.r.o. and Kotlin Programming Language contributors. Apache License
2.0: `licenses/Apache-2.0.txt`.

## Bundled fonts

| packaged file | embedded version | pinned source | SHA-256 | licence |
|---|---:|---|---|---|
| `CozetteVector.ttf` | 1.0; release v1.30.0 | Cozette tag `v.1.30.0`, revision `d2282e728aa64949b0a1e6489920ceb0772944fa` | `2d54a586f824f32d9d2dd01ec3b990c2a4d6445efa77e20177efe382ebe51627` | MIT |
| `IBMPlexMono-Regular.ttf` | 2.3 | Google Fonts path revision `633f3200539c52ee0aba2dfd7f46921417a81877` | `6a3412f058c7d8dfd9170c41e85ade48e5156ecb89356110ca57a0a27734af46` | OFL 1.1; reserved name `Plex` |
| `RobotoMono-SemiBold.ttf` | 3.001 | Roboto Mono tag `v3.001`, revision `111eb14e367888c9374da4da0b018e72cf8ac46d` | `dd877afb1ac59aa54fc142bf7e080ead574189ddac899dbffb0698554d1473b2` | OFL 1.1 |
| `ShareTechMono-Regular.ttf` | 1.003 | Google Fonts path revision `1b86e1e716445daccc346253911955073b0d254a` | `9ceab1f87414829af259c0f537573ae03ef7dd3147c0b27a36a1a0beb6732677` | OFL 1.1; reserved name `Share` |
| `VT323-Regular.ttf` | 2.000 | Google Fonts path revision `eb8781e516576b414603df8cd267c0f21c9b4ee2` | `cf4de751ada78ceac033dbe16a687742939995b77bc2a052ae17a4957958594d` | OFL 1.1 |

Sources:

- <https://github.com/the-moonwitch/Cozette/tree/v.1.30.0>
- <https://github.com/googlefonts/RobotoMono/tree/v3.001>
- <https://github.com/google/fonts/tree/633f3200539c52ee0aba2dfd7f46921417a81877/ofl/ibmplexmono>
- <https://github.com/google/fonts/tree/1b86e1e716445daccc346253911955073b0d254a/ofl/sharetechmono>
- <https://github.com/google/fonts/tree/eb8781e516576b414603df8cd267c0f21c9b4ee2/ofl/vt323>

The font copyright notices and full OFL 1.1 terms are in
`licenses/Fonts-OFL-1.1.txt`; Cozette's terms are in `licenses/Cozette-MIT.txt`.

## Embedded random-number generator

`game/src/z-rand.c` embeds the state transition from xoshiro128** 1.1, written
in 2018 by David Blackman and Sebastiano Vigna, and uses SplitMix64, written in
2015 by Sebastiano Vigna, to expand Ammerow's seeds. Both sources include a
public-domain dedication to the extent possible and an unrestricted permission
fallback; their complete notices are in
`licenses/Xoshiro-SplitMix-public-domain.txt`.

- <https://prng.di.unimi.it/xoshiro128starstar.c>
- <https://prng.di.unimi.it/splitmix64.c>

## Ammerow artwork and audio

Official APKs also contain Ammerow's compiled ASCII-art plates, its 32x32 and
64x64 Hybrid sprite sheets, its sound effects and Home theme (converted to Ogg
Vorbis for Android) and the official launcher icon. These are Ammerow's own
assets, separately licensed under `ASSET-LICENSE.md`, not third-party
components and not under the GPL; they are not in this repository. Their
attribution and production notices are in `game/lib/art/NOTICE.md`,
`game/lib/tiles/NOTICE.md` and `game/lib/sounds/ammerow/NOTICE.md`.
