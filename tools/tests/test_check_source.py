# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only
"""Tests for tools/check_source.py on small source trees and archives.

Run: python -m unittest discover -s tools/tests
"""

from __future__ import annotations

import shutil
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_source as cs  # noqa: E402

FILES = {
    "README.md": b"# Example\n\nSee [the notice](NOTICE.md) and `game/src/`.\n",
    "NOTICE.md": b"Notice.\n",
    "app/build.gradle.kts": b'android { defaultConfig { versionName = "1.0-android.2" } }\n',
    "game/lib/screens/legal.txt": b"notice:Source tagged v1.0-android.2.\n",
    "game/sources.cmake": b"set(AMMEROW_CORE_SOURCES\n    src/a.c)\n",
    "game/src/a.c": b"int a;\n",
    "game/lib/help/index.txt": b"Index\n",
    "game/packaging/runtime.txt": b"data|lib/help/index.txt|help/index.txt\n",
    "game/packaging/runtime-art.txt": b"data|lib/art/wolf.art|art/wolf.art\n",
}


class SourceTree(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root)
        self.listed = sorted(FILES) + [cs.FILE_LIST]
        for name, data in FILES.items():
            self.write(name, data)
        self.write_list()

    def write(self, name: str, data: bytes) -> None:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def write_list(self) -> None:
        self.write(cs.FILE_LIST, ("\n".join(self.listed) + "\n").encode())

    def errors(self) -> list[str]:
        tree, errors = cs.disk_tree(self.root)
        return errors + cs.check(tree, cs.local_patterns(self.root))

    def assertError(self, fragment: str) -> None:
        errors = self.errors()
        self.assertTrue(any(fragment in error for error in errors), f"no {fragment!r} in {errors}")

    def test_clean_tree_passes(self):
        self.assertEqual(self.errors(), [])

    def test_build_output_is_ignored(self):
        self.write("third_party/SDL/CMakeLists.txt", b"x\n")
        self.write("app/build/outputs/apk/debug/app-debug.apk", b"apk")
        self.write("local.properties", b"sdk.dir=x\n")
        self.assertEqual(self.errors(), [])

    def test_unlisted_file(self):
        self.write("docs/notes.md", b"draft\n")
        self.assertError("unlisted file: docs/notes.md")

    def test_missing_file(self):
        (self.root / "NOTICE.md").unlink()
        self.assertError("missing listed file: NOTICE.md")

    def test_media(self):
        self.listed.append("app/icon.png")
        self.write_list()
        self.write("app/icon.png", b"\x89PNG")
        self.assertError("media, binary or signing file: app/icon.png")

    def test_secret(self):
        self.listed.append("keystore.properties")
        self.write_list()
        self.write("keystore.properties", b"storePassword=x\n")
        self.assertError("local or secret file: keystore.properties")

    def test_private_path(self):
        self.write("README.md", b"Built in C:\\Users\\someone\\project.\n")
        self.assertError("private reference in README.md")

    def test_local_private_pattern(self):
        self.write(cs.LOCAL_PATTERNS, b"# my folders\nSecret-Folder\n")
        self.write("app/build.gradle.kts", FILES["app/build.gradle.kts"] + b"// ../Secret-Folder\n")
        self.assertError("private reference in app/build.gradle.kts: Secret-Folder")

    def test_email(self):
        self.write("NOTICE.md", b"Write to someone@example.com.\n")
        self.assertError("e-mail address in NOTICE.md")

    def test_broken_link(self):
        self.write("README.md", b"See [notes](docs/notes.md).\n")
        self.assertError("broken link in README.md: docs/notes.md")

    def test_missing_named_path(self):
        self.write("README.md", b"Run `tools/fetch-deps.sh`.\n")
        self.assertError("missing path named in README.md: tools/fetch-deps.sh")

    def test_legal_screen_names_another_release(self):
        self.write("game/lib/screens/legal.txt", b"notice:Source tagged v1.0-android.1.\n")
        self.assertError("does not name this release (v1.0-android.2)")

    def test_source_list_names_a_missing_file(self):
        self.write("game/sources.cmake", b"set(AMMEROW_CORE_SOURCES\n    src/a.c\n    src/b.c)\n")
        self.assertError("game/sources.cmake lists a missing file: src/b.c")

    def test_manifest_names_a_missing_file(self):
        self.write("game/packaging/runtime.txt", b"data|lib/help/touch.txt|help/touch.txt\n")
        self.assertError("names a missing file: lib/help/touch.txt")

    def test_licensed_media_in_the_source(self):
        self.listed.append("game/lib/art/wolf.art")
        self.write_list()
        self.write("game/lib/art/wolf.art", b"plate\n")
        self.assertError("separately licensed file in the source: game/lib/art/wolf.art")

    def archive(self, extra: dict[str, bytes] | None = None, revision: bytes | None = None) -> Path:
        path = self.root.parent / f"{self.root.name}.zip"
        self.addCleanup(lambda: path.unlink(missing_ok=True))
        with zipfile.ZipFile(path, "w") as archive:
            for name in self.listed:
                archive.writestr("Ammerow-Android-1.0/" + name, (self.root / name).read_bytes())
            archive.writestr("Ammerow-Android-1.0/" + cs.REVISION,
                             revision if revision is not None else b"commit " + b"a" * 40 + b"\nversion 1.0\n")
            for name, data in (extra or {}).items():
                archive.writestr("Ammerow-Android-1.0/" + name, data)
        return path

    def archive_errors(self, path: Path) -> list[str]:
        tree, errors = cs.archive_tree(path)
        return errors + cs.check(tree)

    def test_archive_passes(self):
        self.assertEqual(self.archive_errors(self.archive()), [])

    def test_archive_extra_file(self):
        self.assertIn("unlisted file in the archive: notes.md",
                      self.archive_errors(self.archive({"notes.md": b"x\n"})))

    def test_archive_without_revision(self):
        self.assertIn(f"{cs.REVISION} must name the commit and version",
                      self.archive_errors(self.archive(revision=b"")))


if __name__ == "__main__":
    unittest.main()
