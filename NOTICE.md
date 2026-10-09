# Ammerow for Android: source and distribution notice

This notice is an index, not a replacement for any licence text or file-level
copyright notice.

## The program

Ammerow: Lands Beyond began from Angband upstream commit
`5c45eb9588b8227d4f1b1998e0a627ad7ee11a75`, 174 commits after the Angband
4.2.6 release. The combined Ammerow source, including this Android edition, is
distributed under GNU GPL version 2 only (GPLv2), subject to compatible
file-level notices. The complete GPLv2 text is in [`COPYING`](COPYING).

Inherited Angband material was offered under GPLv2 or the alternative Angband
licence. This distribution relies on GPLv2 and preserves the inherited
alternative notices and named copyright holders in
[`docs/copying.rst`](docs/copying.rst), and the contributor chronology in
[`docs/thanks.rst`](docs/thanks.rst). Choosing GPLv2 does not erase terms
applying to inherited material considered separately. Authorship is
summarised in [`AUTHORS.md`](AUTHORS.md).

This repository holds the complete corresponding source for the Android
releases: the game (`game/`), the Android program (`app/`) and the build
scripts. The game files changed for Android are listed in
[`ANDROID-CHANGES.md`](ANDROID-CHANGES.md). Each official APK carries its own
source, as `assets/source/Ammerow-Android-source.zip`, which needs no
Internet connection to obtain; the tag of the APK's version in this
repository holds the same source. A different version's source is not a
substitute. The third-party libraries are fetched at pinned revisions by
`tools/fetch-deps.ps1` (or `tools/fetch-deps.sh`); each GitHub release also
offers their sources as an archive.

## Separately licensed assets

The GPLv2 source licence does not license every font, sound, image, library or
other asset distributed beside the code. Each such component remains governed
by its own terms.

The Ammerow ASCII-art plates, Hybrid sprite sheets, sound effects, Home theme
and official launcher icon are covered by the separate
[`ASSET-LICENSE.md`](ASSET-LICENSE.md), not by GPLv2. Copyright © 2026 John
Horton; all rights are reserved to the extent copyright and related rights
subsist. They are not in this repository. Official APKs include them; builds
from this repository without them use the game's plain glyphs, its
code-synthesised sound effects and a generic launcher icon. A public fork must
omit or replace these assets unless separately authorised.

The third-party libraries and fonts, their versions and their licence texts
are listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and
`licenses/`. Every APK carries these notices and licence texts in its
`assets/legal/` folder.

## Ammerow name

AMMEROW™ and AMMEROW: LANDS BEYOND™ are trade marks of John Horton. No trade
mark licence is granted with the GPL-covered source or the assets. A public
fork must use distinct, non-confusing branding (including its own application
ID and launcher icon) unless separately authorised, while truthful attribution
such as “based on the Ammerow source” remains permitted.

Only releases and store pages published by John Horton, or through a channel
he expressly identifies, are official.
