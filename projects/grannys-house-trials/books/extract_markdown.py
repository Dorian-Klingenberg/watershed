"""
extract_markdown.py — Index a GitHub-style Markdown repository into SQLite.

Usage:
    python books/extract_markdown.py <repo_root> <book_dir> [--filter GLOB] [--force] [--limit N]

Where:
    <repo_root>   Root of the cloned Markdown repository.
    <book_dir>    Output directory — index.db created here.

Options:
    --filter GLOB   Only index files matching this glob (relative to repo_root).
                    Can be specified multiple times. Default: **/*.md
    --force         Re-index all files even if SHA-256 hash is unchanged.
    --limit N       Stop after indexing N files (useful for smoke-testing).

What gets indexed:
    sections + sections_fts — full prose, compatible with lookup.py keyword search
    sources                 — per-file SHA-256 for incremental re-indexing

Requires: No external dependencies — uses Python stdlib only.
"""

import argparse
import fnmatch
import hashlib
import re
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path


# ---------------------------------------------------------------------------
# DB schema
# ---------------------------------------------------------------------------

SCHEMA = """
CREATE TABLE IF NOT EXISTS sections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter     INTEGER NOT NULL DEFAULT 0,
    section_id  TEXT,
    title       TEXT NOT NULL,
    page_start  INTEGER,
    page_end    INTEGER,
    text        TEXT,
    tags        TEXT
);
CREATE INDEX IF NOT EXISTS idx_ch   ON sections(chapter);
CREATE INDEX IF NOT EXISTS idx_sid  ON sections(section_id);
CREATE INDEX IF NOT EXISTS idx_page ON sections(page_start, page_end);
CREATE INDEX IF NOT EXISTS idx_tags ON sections(tags);

CREATE VIRTUAL TABLE IF NOT EXISTS sections_fts USING fts5(
    section_id, title, text, tags,
    content='sections', content_rowid='id'
);

CREATE TABLE IF NOT EXISTS figures (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter INTEGER NOT NULL DEFAULT 0,
    page INTEGER, label TEXT, caption TEXT, image_path TEXT
);

CREATE TABLE IF NOT EXISTS book_tables (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter INTEGER NOT NULL DEFAULT 0,
    page INTEGER, caption TEXT, content_csv TEXT
);

CREATE TABLE IF NOT EXISTS sources (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path   TEXT UNIQUE NOT NULL,
    sha256      TEXT NOT NULL,
    indexed_at  TEXT NOT NULL
);
"""


# ---------------------------------------------------------------------------
# Tag vocabulary — path segments and title/text patterns
# ---------------------------------------------------------------------------

_PATH_TAGS: list[tuple[str, list[str]]] = [
    ("d3d",        ["d3d12", "directx"]),
    ("directx",    ["d3d12", "directx"]),
    ("descriptor", ["descriptor-heap"]),
    ("barrier",    ["resource-barrier"]),
    ("raytracing", ["raytracing"]),
    ("raytrace",   ["raytracing"]),
    ("mesh",       ["mesh-shader"]),
    ("resource",   ["d3d12-resource"]),
    ("command",    ["command-list"]),
    ("heap",       ["descriptor-heap"]),
    ("com",        ["COM"]),
    ("window",     ["win32", "windowing"]),
    ("winmsg",     ["win32", "windowing", "message-loop"]),
    ("message",    ["message-loop"]),
    ("input",      ["input", "win32"]),
    ("inputdev",   ["input", "win32"]),
    ("sync",       ["synchronization", "win32"]),
    ("algorithm",  ["algorithms", "stdlib"]),
    ("container",  ["containers", "stdlib"]),
    ("memory",     ["memory-management"]),
    ("ranges",     ["ranges", "cpp20"]),
    ("coroutine",  ["coroutines", "cpp20"]),
    ("concept",    ["concepts", "cpp20"]),
    ("raii",       ["RAII"]),
    ("shader",     ["shader", "hlsl"]),
    ("hlsl",       ["hlsl", "shader"]),
    ("pipeline",   ["pipeline-state"]),
    ("rootsig",    ["root-signature"]),
    ("fence",      ["synchronization"]),
    ("swapchain",  ["swap-chain"]),
]

_TEXT_TAGS: list[tuple[str, str]] = [
    (r"descriptor.?heap",                   "descriptor-heap"),
    (r"command.?(?:list|queue|allocator)",   "command-list"),
    (r"resource.?barrier|barrier.?type",    "resource-barrier"),
    (r"ray.?trac(?:ing|er)",                "raytracing"),
    (r"mesh.?shader",                       "mesh-shader"),
    (r"RAII|resource.?acquisition",         "RAII"),
    (r"unique_ptr|shared_ptr|weak_ptr",     "smart-pointer"),
    (r"\bCOM\b|IUnknown|QueryInterface",    "COM"),
    (r"\bHWND\b|WndProc|WM_[A-Z]",         "win32-windowing"),
    (r"ownership|lifetime",                 "RAII"),
    (r"fence|Signal|Wait\b|synchroniz",     "synchronization"),
    (r"root.?signature",                    "root-signature"),
    (r"pipeline.?state",                    "pipeline-state"),
    (r"swap.?chain",                        "swap-chain"),
    (r"render.?target",                     "render-target"),
    (r"\[\[nodiscard\]\]",                  "nodiscard"),
    (r"constexpr",                          "constexpr"),
    (r"ComPtr|wrl::",                       "COM"),
]


def _auto_tags(rel_path: str, title: str, text_sample: str) -> str:
    tags: set[str] = set()
    parts = rel_path.replace("\\", "/").lower().split("/")
    for part in parts:
        for seg, tag_list in _PATH_TAGS:
            if seg in part:
                tags.update(tag_list)
    combined = (title + " " + text_sample[:600])
    for pattern, tag in _TEXT_TAGS:
        if re.search(pattern, combined, re.IGNORECASE):
            tags.add(tag)
    return " ".join(sorted(tags))


# ---------------------------------------------------------------------------
# Chapter inference
# ---------------------------------------------------------------------------

_CHAPTER_MAP: dict[str, int] = {
    "d3d": 1, "d3d12": 1, "directx": 1, "hlsl": 2,
    "com": 3, "desktop-src": 4, "docs": 5,
    "p-philosophy": 1, "i-interfaces": 2, "f-functions": 3,
    "c-classes": 4, "r-resource": 5, "e-error": 6, "es-expressions": 7,
}


def _infer_chapter(rel_path: str) -> int:
    parts = rel_path.replace("\\", "/").lower().split("/")
    for part in parts:
        ch = _CHAPTER_MAP.get(part)
        if ch is not None:
            return ch
    return 99


# ---------------------------------------------------------------------------
# Markdown parsing
# ---------------------------------------------------------------------------

def _parse_frontmatter(text: str) -> tuple[dict, str]:
    """Extract YAML frontmatter and return (meta, body)."""
    meta: dict = {}
    if text.startswith("---"):
        end = text.find("\n---", 3)
        if end != -1:
            front = text[3:end].strip()
            body  = text[end + 4:].lstrip()
            for line in front.splitlines():
                if ":" in line:
                    k, _, v = line.partition(":")
                    meta[k.strip().lower()] = v.strip().strip('"').strip("'")
            return meta, body
    return meta, text


def _extract_first_code_block(text: str) -> str | None:
    m = re.search(r"```[^\n]*\n(.*?)```", text, re.DOTALL)
    if m:
        code = m.group(1).strip()
        return code[:600] if len(code) > 600 else code
    return None


def _markdown_to_text(md: str) -> str:
    text = re.sub(r"```[^\n]*\n.*?```", " ", md, flags=re.DOTALL)
    text = re.sub(r"`([^`]+)`", r"\1", text)
    text = re.sub(r"!\[[^\]]*\]\([^)]*\)", "", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"<[^>]+>", " ", text)
    text = re.sub(r"^#{1,6}\s+", "", text, flags=re.MULTILINE)
    text = re.sub(r"^\|[-: |]+\|$", "", text, flags=re.MULTILINE)
    text = re.sub(r"\*{1,3}([^*\n]+)\*{1,3}", r"\1", text)
    text = re.sub(r"_{1,3}([^_\n]+)_{1,3}", r"\1", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()[:60000]


def _extract_title(meta: dict, body: str, rel_path: str) -> str:
    if "title" in meta:
        return meta["title"]
    m = re.search(r"^#{1,3}\s+(.+)$", body, re.MULTILINE)
    if m:
        return m.group(1).strip()
    return Path(rel_path).stem.replace("-", " ").replace("_", " ").title()


# ---------------------------------------------------------------------------
# SHA-256 hashing
# ---------------------------------------------------------------------------

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# DB helpers
# ---------------------------------------------------------------------------

def open_db(db_path: Path) -> sqlite3.Connection:
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    conn.executescript(SCHEMA)
    # Migrate older DBs that predate the tags column.
    try:
        conn.execute("ALTER TABLE sections ADD COLUMN tags TEXT")
        conn.commit()
    except sqlite3.OperationalError:
        pass
    return conn


def get_indexed_hashes(conn: sqlite3.Connection) -> dict[str, str]:
    try:
        rows = conn.execute("SELECT file_path, sha256 FROM sources").fetchall()
        return {r["file_path"]: r["sha256"] for r in rows}
    except sqlite3.OperationalError:
        return {}


def upsert_source(conn: sqlite3.Connection, file_path: str, sha256: str) -> None:
    now = datetime.now(timezone.utc).isoformat()
    conn.execute(
        "INSERT INTO sources (file_path, sha256, indexed_at) VALUES (?, ?, ?) "
        "ON CONFLICT(file_path) DO UPDATE SET sha256=excluded.sha256, indexed_at=excluded.indexed_at",
        (file_path, sha256, now),
    )


def insert_section(conn: sqlite3.Connection, data: dict) -> None:
    conn.execute("DELETE FROM sections WHERE section_id = ?", (data["section_id"],))
    conn.execute(
        "INSERT INTO sections (chapter, section_id, title, page_start, page_end, text, tags) "
        "VALUES (:chapter, :section_id, :title, NULL, NULL, :text, :tags)",
        data,
    )


# ---------------------------------------------------------------------------
# File filter
# ---------------------------------------------------------------------------

def _matches_filters(rel_path: str, filters: list[str]) -> bool:
    if not filters:
        return True
    norm = rel_path.replace("\\", "/")
    for f in filters:
        if fnmatch.fnmatch(norm, f):
            return True
        # Also match if the filter is a prefix directory
        if norm.startswith(f.rstrip("/*")):
            return True
    return False


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Index a GitHub Markdown repository into SQLite.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("repo_root", help="Root of the cloned Markdown repository")
    parser.add_argument("book_dir",  help="Output directory for index.db")
    parser.add_argument("--filter",  dest="filters", action="append", default=[],
                        metavar="GLOB", help="Only index files matching this glob (repeatable)")
    parser.add_argument("--force",   action="store_true", help="Re-index regardless of hash")
    parser.add_argument("--limit",   type=int, default=0, help="Stop after N files (0=no limit)")
    args = parser.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    repo_root = Path(args.repo_root)
    book_dir  = Path(args.book_dir)
    book_dir.mkdir(parents=True, exist_ok=True)
    db_path = book_dir / "index.db"

    if not repo_root.is_dir():
        print(f"ERROR: repo_root not found: {repo_root}")
        sys.exit(1)

    filters = args.filters or ["**/*.md"]

    print(f"Repo root  : {repo_root}")
    print(f"Output DB  : {db_path}")
    print(f"Filters    : {filters}")

    conn = open_db(db_path)
    known = {} if args.force else get_indexed_hashes(conn)

    all_md = sorted(repo_root.rglob("*.md"))
    filtered = [f for f in all_md
                if _matches_filters(str(f.relative_to(repo_root)), filters)]

    print(f"MD files   : {len(all_md)} found, {len(filtered)} after filter")
    if args.limit:
        print(f"Limit      : {args.limit}")
    if known:
        print(f"Already idx: {len(known)} files (--force to re-index)")

    processed = skipped = errors = 0
    limit_hit = False

    try:
        for md_path in filtered:
            if args.limit and processed >= args.limit:
                limit_hit = True
                break

            rel = str(md_path.relative_to(repo_root))
            sha = sha256_file(md_path)

            if not args.force and known.get(rel) == sha:
                skipped += 1
                continue

            try:
                raw = md_path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                errors += 1
                continue

            meta, body   = _parse_frontmatter(raw)
            title        = _extract_title(meta, body, rel)
            text         = _markdown_to_text(body)
            chapter      = _infer_chapter(rel)
            tags         = _auto_tags(rel, title, text)

            if len(text) < 30:
                upsert_source(conn, rel, sha)
                skipped += 1
                continue

            try:
                insert_section(conn, {
                    "section_id": rel,
                    "title":      title,
                    "chapter":    chapter,
                    "text":       text,
                    "tags":       tags,
                })
                upsert_source(conn, rel, sha)
                processed += 1
            except Exception as exc:
                errors += 1
                print(f"\n  [error] {rel}: {exc}")

            if processed % 100 == 0 and processed > 0:
                conn.commit()
                print(f"  {processed} files indexed ...", flush=True)

        if not limit_hit:
            print("Rebuilding FTS indexes ...")
            conn.executescript("INSERT INTO sections_fts(sections_fts) VALUES('rebuild');")
        conn.commit()

    finally:
        conn.close()

    print(f"\n{'='*50}")
    print(f"Indexed  : {processed}")
    print(f"Skipped  : {skipped}")
    print(f"Errors   : {errors}")
    print(f"DB       : {db_path}")
    if limit_hit:
        print(f"(Limit of {args.limit} reached — FTS not rebuilt; run without --limit for full index)")
    print(f"\nQuery with:")
    print(f'  python books/lookup.py "{book_dir}" "descriptor heap"')
    print(f'  python books/lookup.py "{book_dir}" --tag descriptor-heap')


if __name__ == "__main__":
    main()
