#!/usr/bin/env python3
# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only
"""Checks this repository's published source and writes its source archive.

The source is exactly the files in tools/source-files.txt: a new file is not
published until it is listed there. The checks cover missing and unlisted
files, separately licensed media, secrets, private references in the Android
edition's own text, broken links, the release manifests, the build's source
list, the version named by the legal screen, and (in a checkout) that the
data generators in game/utils reproduce the committed game data from their
inputs. Passing them does not establish licence clearance.

  python tools/check_source.py                  # check this tree
  python tools/check_source.py --export OUT.zip  # write the archive from HEAD
  python tools/check_source.py --archive OUT.zip # check an archive

Official APKs carry the archive (gradlew assembleRelease
-Pammerow.sourceArchive=OUT.zip), so each release includes its own source.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
FILE_LIST = "tools/source-files.txt"
REVISION = "SOURCE-REVISION.txt"
GRADLE = "app/build.gradle.kts"
LEGAL = "game/lib/screens/legal.txt"
SOURCES = "game/sources.cmake"
MANIFESTS = ("game/packaging/runtime.txt", "game/packaging/runtime-art.txt", "game/packaging/runtime-audio.txt")
# Each generated data file is checked against its inputs (run from game/).
GENERATOR_CHECKS = (
    ("utils/generate-artifact-slots.py", "--check"),
    ("utils/generate-unique-monsters.py", "--check"),
    ("utils/generate-original-monsters.py", "--check"),
    ("utils/generate-sdl3-sound-map.py", "--check"),
    ("utils/generate-ammerow-vaults.py", "--check-vault", "lib/gamedata/vault.txt"),
)

# Separately licensed media, signing material and build output never belong
# in the source. The Gradle wrapper and the fonts are the binaries it needs.
FORBIDDEN_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".svg", ".ico", ".icns",
                      ".wav", ".ogg", ".mp3", ".flac", ".art", ".jks", ".keystore", ".p12",
                      ".apk", ".aab", ".so", ".dex", ".zip", ".pyc"}
ALLOWED_BINARIES = ("gradle/wrapper/gradle-wrapper.jar",)
BINARY_SUFFIXES = {".jar", ".ttf"}
SECRET_NAMES = {"keystore.properties", "local.properties"}
# Text written for the Android edition (the game's own files under game/ are
# the desktop edition's published source) must not point at a private
# checkout, machine or tool, or carry personal contact details.
ANDROID_TEXT = ("README.md", "NOTICE.md", "THIRD_PARTY_NOTICES.md", "CONTRIBUTING.md", "ANDROID-CHANGES.md",
                "docs/copying.rst", ".gitignore", ".gitattributes", "app/", "tools/", "game/README.md",
                "game/sources.cmake", LEGAL)
PRIVATE_PATTERNS = (
    r"[A-Za-z]:[/\\]Users[/\\]",
    r"/home/[^/\s]+/|/Users/[^/\s]+/",
    r"private (?:repository|repo|checkout|commit|branch)",
    r"(?m)^\s*(?:storePassword|keyPassword)=\S",  # a signing password in properties form
)
# A maintainer's own private names (folders, machines, tools), one regular
# expression per line, in an untracked file.
LOCAL_PATTERNS = "tools/private-patterns.txt"
EMAIL = r"[\w.+-]+@[\w-]+(?:\.[\w-]+)+"
# The checker and its tests name the patterns themselves.
PROSE_EXEMPT = ("tools/check_source.py", "tools/tests/")
# Folders a build or tool creates in a checkout.
BUILD_FOLDERS = ("build/", ".gradle/", ".kotlin/", ".idea/", "app/build/", "app/.cxx/", "third_party/",
                 "captures/", "tools/tests/__pycache__/", "tools/__pycache__/")
BUILD_FILES = {"local.properties", "keystore.properties"}


def safe_path(name: str) -> str:
    if (not name or "\\" in name or ":" in name or any(ord(c) < 32 for c in name)
            or any(part in ("", ".", "..") for part in name.split("/"))):
        raise ValueError(f"unsafe path: {name!r}")
    return name


def read_list(text: str) -> list[str]:
    names = [safe_path(line.strip()) for line in text.splitlines() if line.strip() and not line.startswith("#")]
    folded = [name.casefold() for name in names]
    if len(set(folded)) != len(folded):
        raise ValueError(f"duplicate or case-colliding entry in {FILE_LIST}")
    return names


def git(*args: str, text: bool = True):
    result = subprocess.run(["git", "--no-optional-locks", "-C", str(ROOT), *args], capture_output=True, text=text)
    if result.returncode != 0:
        raise ValueError(f"git {' '.join(args)}: {result.stderr}")
    return result.stdout


def version_name(gradle: str) -> str:
    match = re.search(r'versionName\s*=\s*"([^"]+)"', gradle)
    if not match:
        raise ValueError(f"no versionName in {GRADLE}")
    return match.group(1)


class Tree:
    """The listed files and a way to read them: from disk or an archive."""

    def __init__(self, names: list[str], read):
        self.names = names
        self.read = read

    def text(self, name: str) -> str:
        return self.read(name).decode("utf-8").replace("\r\n", "\n")


def disk_tree(root: Path) -> tuple[Tree, list[str]]:
    errors: list[str] = []
    listed = read_list((root / FILE_LIST).read_text(encoding="utf-8"))
    if (root / ".git").exists():
        present = git("ls-files", "-z").split("\0")[:-1]
        untracked = git("ls-files", "-z", "--others", "--exclude-standard").split("\0")[:-1]
        for name in untracked:
            errors.append(f"untracked file (list it in {FILE_LIST} or ignore it): {name}")
    else:
        present = [path.relative_to(root).as_posix() for path in root.rglob("*")
                   if path.is_file() and ".git" not in path.relative_to(root).parts]
    listed_set = set(listed)
    for name in present:
        if (name in listed_set or name in (REVISION, LOCAL_PATTERNS) or name in BUILD_FILES
                or name.startswith(BUILD_FOLDERS)):
            continue
        errors.append(f"unlisted file: {name}")
    files = []
    for name in listed:
        path = root / name
        if not path.is_file():
            errors.append(f"missing listed file: {name}")
        elif path.is_symlink():
            errors.append(f"symbolic link: {name}")
        else:
            files.append(name)
    for name in BUILD_FILES:
        if (root / ".git").exists() and name in present:
            errors.append(f"local or secret file committed: {name}")
    return Tree(files, lambda name: (root / name).read_bytes()), errors


def archive_tree(path: Path) -> tuple[Tree, list[str]]:
    archive = zipfile.ZipFile(path)
    entries = [info.filename for info in archive.infolist() if not info.is_dir()]
    prefixes = {entry.split("/", 1)[0] for entry in entries}
    if len(prefixes) != 1:
        raise ValueError("the archive must hold a single top-level folder")
    prefix = prefixes.pop() + "/"
    names = [safe_path(entry[len(prefix):]) for entry in entries]
    read = lambda name: archive.read(prefix + name)  # noqa: E731
    errors: list[str] = []
    listed = read_list(read(FILE_LIST).decode("utf-8"))
    for name in sorted(set(names) - set(listed) - {REVISION}):
        errors.append(f"unlisted file in the archive: {name}")
    for name in sorted(set(listed) - set(names)):
        errors.append(f"missing from the archive: {name}")
    revision = read(REVISION).decode("utf-8") if REVISION in names else ""
    if not re.fullmatch(r"commit [0-9a-f]{40}\nversion \S+\n", revision):
        errors.append(f"{REVISION} must name the commit and version")
    return Tree([n for n in names if n != REVISION and n in set(listed)], read), errors


def local_patterns(root: Path) -> list[str]:
    path = root / LOCAL_PATTERNS
    if not path.is_file():
        return []
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.startswith("#")]


def check(tree: Tree, extra_patterns: list[str] = ()) -> list[str]:
    errors: list[str] = []
    names = set(tree.names)
    patterns = list(PRIVATE_PATTERNS) + list(extra_patterns)
    for name in tree.names:
        suffix = PurePosixPath(name).suffix.lower()
        base = PurePosixPath(name).name
        if suffix in FORBIDDEN_SUFFIXES or (suffix == ".jar" and name not in ALLOWED_BINARIES):
            errors.append(f"media, binary or signing file: {name}")
        if base in SECRET_NAMES or ".git" in PurePosixPath(name).parts:
            errors.append(f"local or secret file: {name}")
        if (suffix in BINARY_SUFFIXES | FORBIDDEN_SUFFIXES or not name.startswith(ANDROID_TEXT)
                or name.startswith(PROSE_EXEMPT)):
            continue
        try:
            content = tree.text(name)
        except UnicodeDecodeError:
            errors.append(f"not UTF-8 text: {name}")
            continue
        for pattern in patterns:
            if re.search(pattern, content, re.IGNORECASE):
                errors.append(f"private reference in {name}: {pattern}")
        for address in re.findall(EMAIL, content):
            errors.append(f"e-mail address in {name}: {address}")
        if name.endswith(".md"):
            errors.extend(check_links(name, content, names))
    errors.extend(check_build(tree, names))
    return errors


def check_links(name: str, content: str, names: set[str]) -> list[str]:
    errors = []
    folder = PurePosixPath(name).parent
    for target in re.findall(r"\[[^\]\n]*\]\(([^)\s]+)\)", content):
        target = target.strip("<>").split("#", 1)[0]
        if not target or re.match(r"[a-z]+:", target):
            continue
        parts: list[str] = []
        for part in (folder / target).parts:
            if part == "..":
                if parts:
                    parts.pop()
                else:
                    parts.append("..")
            elif part != ".":
                parts.append(part)
        resolved = "/".join(parts)
        if resolved not in names and not any(n.startswith(resolved + "/") for n in names):
            errors.append(f"broken link in {name}: {target}")
    # Backquoted paths into the repository must exist (as a file or a folder).
    for target in re.findall(r"`((?:app|game|docs|licenses|tools)/[\w./-]*)`", content):
        target = target.rstrip("/")
        if (target + "/").startswith(BUILD_FOLDERS):
            continue
        if target not in names and not any(n.startswith(target + "/") for n in names):
            errors.append(f"missing path named in {name}: {target}")
    return errors


def check_build(tree: Tree, names: set[str]) -> list[str]:
    errors = []
    if GRADLE in names and LEGAL in names:
        version = version_name(tree.text(GRADLE))
        if f"v{version}" not in tree.text(LEGAL):
            errors.append(f"{LEGAL} does not name this release (v{version})")
    if SOURCES in names:
        for source in re.findall(r"src/[\w/.-]+\.c", tree.text(SOURCES)):
            if f"game/{source}" not in names:
                errors.append(f"{SOURCES} lists a missing file: {source}")
    # Every runtime file is either public game data or separately licensed media.
    for manifest in MANIFESTS:
        if manifest not in names:
            continue
        for line in tree.text(manifest).splitlines():
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            source = line.split("|")[1]
            media = PurePosixPath(source).suffix.lower() in {".wav", ".png", ".art"}
            present = f"game/{source}" in names
            if media and present:
                errors.append(f"separately licensed file in the source: game/{source}")
            if not media and not present:
                errors.append(f"{manifest} names a missing file: {source}")
    return errors


def check_generators(root: Path) -> list[str]:
    errors = []
    for command in GENERATOR_CHECKS:
        result = subprocess.run([sys.executable, *command], cwd=root / "game", capture_output=True, text=True)
        if result.returncode:
            errors.append(f"game/{' '.join(command)}: {(result.stdout + result.stderr).strip()[:400]}")
    return errors


def export(output: Path) -> None:
    """Writes the archive from the committed files at HEAD only."""
    if git("status", "--porcelain", "--untracked-files=all").strip():
        raise ValueError("export needs a clean checkout (commit or remove the changes first)")
    if output.suffix != ".zip":
        raise ValueError("the archive name must end in .zip")
    commit = git("rev-parse", "HEAD").strip()
    listed = read_list(git("show", f"{commit}:{FILE_LIST}"))
    version = version_name(git("show", f"{commit}:{GRADLE}"))
    stamp = datetime.fromtimestamp(int(git("show", "-s", "--format=%ct", commit)), timezone.utc).timetuple()[:6]
    prefix = f"Ammerow-Android-{version}/"
    output.parent.mkdir(parents=True, exist_ok=True)
    blobs = subprocess.Popen(["git", "--no-optional-locks", "-C", str(ROOT), "cat-file", "--batch"],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    executable = set(git("ls-files", "-s").splitlines())
    executable = {line.split("\t", 1)[1] for line in executable if line.startswith("100755")}
    # Exclusive creation: an earlier archive is never overwritten.
    with output.open("xb") as stream, zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(listed):
            blobs.stdin.write(f"{commit}:{name}\n".encode("utf-8"))
            blobs.stdin.flush()
            header = blobs.stdout.readline().split()
            if len(header) != 3 or header[1] != b"blob":
                raise ValueError(f"not committed: {name}")
            data = blobs.stdout.read(int(header[2]))
            blobs.stdout.read(1)
            entry = zipfile.ZipInfo(prefix + name, stamp)
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = (0o755 if name in executable else 0o644) << 16
            archive.writestr(entry, data)
        entry = zipfile.ZipInfo(prefix + REVISION, stamp)
        entry.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(entry, f"commit {commit}\nversion {version}\n")
    blobs.stdin.close()
    blobs.wait()
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_name(output.name + ".sha256").write_text(f"{digest}  {output.name}\n", encoding="utf-8", newline="\n")
    print(f"Wrote {output}\ncommit {commit}\nversion {version}\nSHA-256 {digest}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--export", type=Path, help="write the source archive from HEAD")
    mode.add_argument("--archive", type=Path, help="check a source archive")
    args = parser.parse_args()
    try:
        if args.export:
            tree, errors = disk_tree(ROOT)
            errors += check(tree, local_patterns(ROOT))
            if errors:
                raise ValueError("fix the tree first:\n  " + "\n  ".join(errors))
            export(args.export.resolve())
            args.archive = args.export
        tree, errors = archive_tree(args.archive.resolve()) if args.archive else disk_tree(ROOT)
        errors += check(tree, local_patterns(ROOT))
        if not args.archive:
            errors += check_generators(ROOT)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f"source check error: {error}", file=sys.stderr)
        return 1
    for error in errors:
        print(f"source check error: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"Source OK: {len(tree.names)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
