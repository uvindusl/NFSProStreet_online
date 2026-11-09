#!/usr/bin/env python3
"""Serve the ProStreet browser shell and a user-owned game directory."""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import re
from pathlib import Path
from urllib.parse import unquote, urlsplit
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

GAME_ENTRIES = {
    "nfsps.exe",
    "airacelines",
    "cars",
    "credits",
    "dat.bin",
    "frontend",
    "global",
    "languages",
    "memcard",
    "mini_maps",
    "movies",
    "nis",
    "online",
    "pfdata",
    "pb",
    "scripts",
    "sound",
    "speech",
    "subtitles",
    "tracks",
    "server.cfg",
    "server.dll",
}


def real_path(path: Path) -> Path:
    return path.resolve(strict=True)


def inside(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def child_casefold(root: Path, name: str) -> Path | None:
    exact = root / name
    if exact.exists() and not exact.is_symlink():
        return exact
    folded = name.casefold()
    try:
        matches = [entry for entry in root.iterdir()
                   if entry.name.casefold() == folded and not entry.is_symlink()]
    except OSError:
        return None
    return matches[0] if len(matches) == 1 else None


def resolve_game(root: Path, request: str) -> Path | None:
    parts = [part for part in request.replace("\\", "/").split("/")
             if part and part != "."]
    if not parts or any(part == ".." or "/" in part or "\\" in part for part in parts):
        return None
    if parts[0].casefold() not in GAME_ENTRIES:
        return None
    current = root
    for part in parts:
        current = child_casefold(current, part)
        if current is None:
            return None
    try:
        resolved = real_path(current)
    except OSError:
        return None
    return resolved if inside(resolved, root) else None


def walk_game(root: Path) -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []
    for top in sorted(root.iterdir(), key=lambda item: item.name.casefold()):
        if top.name.startswith(".") or top.name.casefold() not in GAME_ENTRIES:
            continue
        if top.is_symlink():
            continue
        paths = [top] if top.is_file() else list(top.rglob("*"))
        for path in sorted(paths, key=lambda item: item.as_posix().casefold()):
            if path.name.startswith(".") or path.is_symlink():
                continue
            try:
                relative = path.relative_to(root).as_posix()
                stat = path.stat()
            except OSError:
                continue
            item: dict[str, object] = {"path": relative}
            if path.is_dir():
                item["directory"] = True
            elif path.is_file():
                item["size"] = stat.st_size
                item["mtime"] = int(stat.st_mtime)
            else:
                continue
            entries.append(item)
    return entries


class Handler(SimpleHTTPRequestHandler):
    server_version = "ProStreetLocal/0.1"

    def __init__(self, *args, web_root: Path, runtime_root: Path,
                 game_root: Path, wall_art: Path | None, **kwargs):
        self.web_root = web_root
        self.runtime_root = runtime_root
        self.game_root = game_root
        self.wall_art = wall_art
        super().__init__(*args, directory=str(web_root), **kwargs)

    def do_GET(self) -> None:
        parsed = urlsplit(self.path)
        path = unquote(parsed.path)
        if path == "/game.json":
            self.send_json({"version": 1, "files": walk_game(self.game_root)})
            return
        if path == "/wall-art":
            self.send_wall_art()
            return
        if path.startswith("/data/"):
            self.send_game_file(path[6:])
            return
        if path in {"/prostreet.wasm", "/prostreet.json"}:
            self.send_runtime_file(path[1:])
            return
        super().do_GET()

    def send_json(self, value: object) -> None:
        body = json.dumps(value, separators=(",", ":")).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_game_file(self, request: str) -> None:
        path = resolve_game(self.game_root, request)
        if path is None or not path.is_file():
            self.send_error(404, "game file not found")
            return
        try:
            size = path.stat().st_size
            with path.open("rb") as stream:
                start, end = self.parse_range(self.headers.get("Range"), size)
                stream.seek(start)
                length = end - start + 1
                self.send_response(206 if start or end != size - 1 else 200)
                self.send_header("Content-Type", mimetypes.guess_type(path.name)[0]
                                 or "application/octet-stream")
                self.send_header("Content-Length", str(length))
                self.send_header("Accept-Ranges", "bytes")
                if start or end != size - 1:
                    self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                remaining = length
                while remaining and (chunk := stream.read(min(1024 * 1024, remaining))):
                    self.wfile.write(chunk)
                    remaining -= len(chunk)
        except ValueError:
            self.send_error(416, "range not satisfiable")
        except OSError:
            self.send_error(404, "game file unavailable")

    @staticmethod
    def parse_range(value: str | None, size: int) -> tuple[int, int]:
        if size <= 0 or not value:
            return 0, max(0, size - 1)
        match = re.fullmatch(r"bytes=(\d*)-(\d*)", value.strip())
        if not match:
            raise ValueError("invalid range")
        first, last = match.groups()
        if not first:
            length = min(int(last), size)
            return size - length, size - 1
        start = int(first)
        end = min(int(last), size - 1) if last else size - 1
        if start >= size or start > end:
            raise ValueError("range outside file")
        return start, end

    def send_runtime_file(self, name: str) -> None:
        path = self.runtime_root / name
        if not path.is_file() or path.is_symlink():
            self.send_error(404, "runtime file not found")
            return
        try:
            body = path.read_bytes()
        except OSError:
            self.send_error(404, "runtime file unavailable")
            return
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(path.name)[0]
                        or "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_wall_art(self) -> None:
        if self.wall_art is None or not self.wall_art.is_file():
            self.send_error(404, "wall art not configured")
            return
        try:
            body = self.wall_art.read_bytes()
        except OSError:
            self.send_error(404, "wall art unavailable")
            return
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(self.wall_art.name)[0]
                        or "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args: object) -> None:
        print("%s - %s" % (self.address_string(), fmt % args))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True, type=Path,
                        help="user-owned game directory containing nfsps.exe")
    parser.add_argument("--web-root", type=Path,
                        default=Path(__file__).resolve().parent)
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--wall-art", type=Path,
                        help="optional local image served as /wall-art")
    args = parser.parse_args()

    game_root = real_path(args.data)
    if not game_root.is_dir() or resolve_game(game_root, "nfsps.exe") is None:
        parser.error("--data must contain nfsps.exe")
    web_root = real_path(args.web_root)
    runtime_root = web_root.parent / "out" / "web"
    wall_art = real_path(args.wall_art) if args.wall_art else None
    if wall_art and not wall_art.is_file():
        parser.error("--wall-art must be an existing image file")
    server = ThreadingHTTPServer(
        ("127.0.0.1", args.http_port),
        lambda *handler_args: Handler(
            *handler_args, web_root=web_root, runtime_root=runtime_root,
            game_root=game_root, wall_art=wall_art
        ),
    )
    print(f"Serving ProStreet at http://127.0.0.1:{args.http_port}/")
    print(f"Game data: {game_root}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
