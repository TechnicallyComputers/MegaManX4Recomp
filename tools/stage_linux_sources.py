"""Snapshot X4's current source for a fresh, private Linux build tree.

No build, game-ROM read, WSL process or publication runs here. Git-tracked working
files and recursive checked-out submodules are included, along with new source
files under src/tests/tools/docs and the required ignored SLUS generated C.
Use --require-clean after integration to require committed source and pins.

Windows preparation:
  python tools/stage_linux_sources.py --plan
  python tools/stage_linux_sources.py --require-clean --output dist/linux-source.tar

Linux extraction (choose a NEW source directory; do not overlay an older tree):
  mkdir -p /home/matthew/mmx4-release-next
  mkdir /home/matthew/mmx4-release-next/source
  tar -xf /mnt/f/Projects/psxrecomp/_wt-mmx4-coop/dist/linux-source.tar \\
      -C /home/matthew/mmx4-release-next/source

LINUX_SOURCE_MANIFEST.json records every copied file's hash/mode, source revision
and release configuration requirements. The .tar.json receipt also hashes the
archive. This private build input contains generated game code; it is not a
release asset. Regenerate OpenBIOS with the pinned Linux emitter before building.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import io
import json
import re
import subprocess
import tarfile


ROOT = Path(__file__).resolve().parents[1]
NEW_SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".py", ".sh", ".ps1", ".md", ".toml"}
PRIVATE_SUFFIXES = {".cue", ".iso", ".img", ".chd", ".ccd", ".sub", ".mcd", ".mcr",
                    ".exe", ".dll", ".o", ".obj", ".a", ".so", ".dylib", ".pdb", ".appimage"}
GENERATED = re.compile(r"SLUS_005\.61_(?:full(?:_\d+)?\.c|dispatch\.c|decls\.h|full\.ranges)$")
RELEASE_FLAGS = {
    "CMAKE_BUILD_TYPE": "Release", "PSX_EXECUTION_PROFILE": "ENHANCED",
    "PSX_SDL_BACKEND": "SDL2", "PSX_NETPLAY": "ON", "PSX_RECOMP_UI": "ON",
    "RNET_ENABLE_ICE": "ON", "RNET_ICE_BUNDLE_STATIC": "ON",
    "PSX_DEBUG_TOOLS": "OFF", "MMX4_COOP_NETPLAY_DEBUG": "OFF",
}


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def private_path(name):
    parts = PurePosixPath(name).parts
    if not parts or ".." in parts or PurePosixPath(name).is_absolute():
        raise ValueError(f"Unsafe source path: {name}")
    if any(part in {".git", "mmx4", "isos", "saves", "cache", ".cache", "__pycache__",
                    "dist", "out", "release-stage", "release-linux"} or
           part == "build" or part.startswith("build-") for part in parts):
        return True
    if name.lower().endswith(".bin"):
        return name != "psxrecomp-v4/bios/openbios.bin"
    return Path(name).suffix.lower() in PRIVATE_SUFFIXES


def inventory(root):
    files, repositories = {}, []

    def visit(repo, prefix="", expected=None):
        revision = git(repo, "rev-parse", "HEAD").decode().strip()
        status = git(repo, "status", "--porcelain=v1", "--untracked-files=no",
                     "--ignore-submodules=untracked").decode()
        record = {"path": prefix or ".", "revision": revision,
                  "branch": git(repo, "rev-parse", "--abbrev-ref", "HEAD").decode().strip(),
                  "indexed_revision": expected, "tracked_status": status, "new_sources": []}
        repositories.append(record)
        for entry in git(repo, "ls-files", "--stage", "-z").split(b"\0"):
            if not entry:
                continue
            metadata, raw_name = entry.split(b"\t", 1)
            mode, indexed, stage = metadata.decode().split()
            name = raw_name.decode("utf-8")
            target = f"{prefix}/{name}" if prefix else name
            if stage != "0":
                raise ValueError(f"Unresolved index entry: {target}")
            if private_path(target):
                continue
            source = repo / name
            if mode == "160000":
                if not (source / ".git").exists():
                    raise ValueError(f"Initialize recursive submodule before staging: {target}")
                visit(source, target, indexed)
            elif mode == "120000":
                raise ValueError(f"Source symlink requires an explicit staging rule: {target}")
            elif source.is_file():
                if source.is_symlink():
                    raise ValueError(f"Source symlink requires an explicit staging rule: {target}")
                files[target] = (source, 0o755 if mode == "100755" else 0o644)
        new = git(repo, "ls-files", "--others", "--exclude-standard", "-z", "--",
                  "src", "tests", "tools", "docs")
        for raw_name in new.split(b"\0"):
            if not raw_name:
                continue
            name = raw_name.decode("utf-8")
            target = f"{prefix}/{name}" if prefix else name
            source = repo / name
            if source.suffix in NEW_SOURCE_SUFFIXES and not private_path(target) and source.is_file():
                if source.is_symlink():
                    raise ValueError(f"New source symlink requires an explicit rule: {target}")
                files[target] = (source, 0o755 if source.suffix == ".sh" else 0o644)
                record["new_sources"].append(name)

    visit(root)
    generated = root / "generated"
    for source in sorted(generated.glob("SLUS_005.61_*")):
        if source.is_file() and GENERATED.fullmatch(source.name):
            if source.is_symlink():
                raise ValueError(f"Generated source symlink requires an explicit rule: {source}")
            files[f"generated/{source.name}"] = (source, 0o644)
    expected = int(re.search(r"^EXPECTED_STATIC_SHARDS=(\d+)$",
                            (root / "packaging/release/app.conf").read_text(), re.M)[1])
    shards = sum(bool(re.fullmatch(r"generated/SLUS_005\.61_full_\d+\.c", name)) for name in files)
    required = {"generated/SLUS_005.61_decls.h", "generated/SLUS_005.61_dispatch.c"}
    if shards != expected or not required.issubset(files):
        raise ValueError(f"Full trusted generated source required: {shards}/{expected} shards, declarations and dispatch")
    return files, repositories, shards


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=ROOT)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument("--plan", action="store_true")
    destination.add_argument("--output", type=Path)
    parser.add_argument("--require-clean", action="store_true")
    args = parser.parse_args()
    root = args.source.resolve()
    files, repositories, shards = inventory(root)
    if args.require_clean:
        for repo in repositories:
            if repo["tracked_status"] or repo["new_sources"] or (
                    repo["indexed_revision"] and repo["revision"] != repo["indexed_revision"]):
                raise ValueError(f"Commit source and recursive pins before release staging: {repo['path']}")
    manifest = {"schema": 1, "repositories": repositories,
                "release_version": (root / "packaging/release/VERSION").read_text().strip(),
                "required_cmake_flags": RELEASE_FLAGS, "generated_shards": shards}
    if args.plan:
        print(json.dumps({**manifest, "files": len(files),
                          "bytes": sum(source.stat().st_size for source, _ in files.values())}, indent=2))
        return
    output = args.output.resolve()
    receipt = output.with_name(output.name + ".json")
    if output.exists() or receipt.exists():
        raise ValueError("Use a fresh archive and receipt path")
    output.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    with output.open("xb") as archive, tarfile.open(fileobj=archive, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for name, (source, mode) in sorted(files.items()):
            data = source.read_bytes()
            entry = tarfile.TarInfo(name)
            entry.mode, entry.size = mode, len(data)
            tar.addfile(entry, io.BytesIO(data))
            rows.append({"path": name, "mode": mode, "size": len(data),
                         "sha256": hashlib.sha256(data).hexdigest()})
        # Refuse a mixed live-edit snapshot. No source file is modified here.
        after_files, after_repositories, _ = inventory(root)
        if after_files.keys() != files.keys() or after_repositories != repositories or any(
                hashlib.sha256(files[row["path"]][0].read_bytes()).hexdigest() != row["sha256"] for row in rows):
            raise ValueError("Source changed during staging; discard this private archive and retry after edits settle")
        manifest["files"] = rows
        manifest["source_sha256"] = hashlib.sha256(json.dumps(rows, sort_keys=True).encode()).hexdigest()
        data = (json.dumps(manifest, indent=2) + "\n").encode()
        entry = tarfile.TarInfo("LINUX_SOURCE_MANIFEST.json")
        entry.mode, entry.size = 0o644, len(data)
        tar.addfile(entry, io.BytesIO(data))
    with output.open("rb") as archive:
        manifest["archive_sha256"] = hashlib.file_digest(archive, "sha256").hexdigest()
    receipt.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"archive": str(output), "receipt": str(receipt),
                      "files": len(rows), "source_sha256": manifest["source_sha256"],
                      "archive_sha256": manifest["archive_sha256"]}, indent=2))


if __name__ == "__main__":
    main()
