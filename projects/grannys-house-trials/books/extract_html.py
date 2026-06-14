"""
extract_html.py — Index a cppreference.com offline HTML archive into SQLite.

Usage:
    python books/extract_html.py <archive_root> <book_dir> [--force] [--limit N]

Where:
    <archive_root>  Root of the extracted cppreference archive.
                    Must contain en/cpp/ (checked automatically in several places).
    <book_dir>      Output directory — index.db created here.
                    Example:  books/cppreference

Options:
    --force         Re-index all files even if SHA-256 hash is unchanged.
    --limit N       Stop after indexing N pages (useful for smoke-testing).

How to get the offline archive:
    1. Go to:  https://en.cppreference.com/w/Cppreference:Archives
    2. Download the HTML archive (.tar.xz or .zip, NOT the Qt .qch file).
    3. Extract it — you should see a directory tree containing en/cpp/*.html.
    4. Point <archive_root> at the extracted root.

What gets indexed:
    sections + sections_fts — full prose, compatible with lookup.py keyword search
    symbols  + symbols_fts  — compact API metadata: name, header, synopsis, since C++NN
    sources                 — per-file SHA-256 for incremental re-indexing

Requires:
    pip install beautifulsoup4 lxml
"""

import argparse
import hashlib
import os
import re
import sqlite3
import sys
from copy import copy
from datetime import datetime, timezone
from pathlib import Path

try:
    from bs4 import BeautifulSoup, Tag
except ImportError:
    print("ERROR: beautifulsoup4 not installed.  Run:  pip install beautifulsoup4 lxml")
    sys.exit(1)


# ---------------------------------------------------------------------------
# DB schema  (all CREATE TABLE IF NOT EXISTS so incremental runs are safe)
# ---------------------------------------------------------------------------

SCHEMA = """
CREATE TABLE IF NOT EXISTS sections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter     INTEGER NOT NULL DEFAULT 0,
    section_id  TEXT,
    title       TEXT NOT NULL,
    page_start  INTEGER,
    page_end    INTEGER,
    text        TEXT
);
CREATE INDEX IF NOT EXISTS idx_ch   ON sections(chapter);
CREATE INDEX IF NOT EXISTS idx_sid  ON sections(section_id);
CREATE INDEX IF NOT EXISTS idx_page ON sections(page_start, page_end);

CREATE VIRTUAL TABLE IF NOT EXISTS sections_fts USING fts5(
    section_id, title, text,
    content='sections', content_rowid='id'
);

-- Stub tables so lookup.py doesn't complain about missing tables.
CREATE TABLE IF NOT EXISTS figures (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter     INTEGER NOT NULL DEFAULT 0,
    page        INTEGER,
    label       TEXT,
    caption     TEXT,
    image_path  TEXT
);

CREATE TABLE IF NOT EXISTS book_tables (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter     INTEGER NOT NULL DEFAULT 0,
    page        INTEGER,
    caption     TEXT,
    content_csv TEXT
);

-- Per-file hash tracking for incremental re-runs.
CREATE TABLE IF NOT EXISTS sources (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path   TEXT UNIQUE NOT NULL,
    sha256      TEXT NOT NULL,
    indexed_at  TEXT NOT NULL
);

-- Compact API-reference metadata (cppreference-specific).
CREATE TABLE IF NOT EXISTS symbols (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    full_name   TEXT NOT NULL,
    header      TEXT,
    category    TEXT,
    since_cxx   INTEGER,
    deprecated  INTEGER NOT NULL DEFAULT 0,
    synopsis    TEXT,
    url_path    TEXT,
    section_id  INTEGER
);
CREATE INDEX IF NOT EXISTS idx_sym_name ON symbols(name COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS idx_sym_full ON symbols(full_name COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS idx_sym_hdr  ON symbols(header);
CREATE INDEX IF NOT EXISTS idx_sym_cxx  ON symbols(since_cxx);

CREATE VIRTUAL TABLE IF NOT EXISTS symbols_fts USING fts5(
    name, full_name, header, synopsis,
    content='symbols', content_rowid='id'
);
"""


# ---------------------------------------------------------------------------
# Category & chapter inference from URL path
# ---------------------------------------------------------------------------

_CATEGORY_SEGMENTS: list[tuple[str, str]] = [
    ("/algorithm",    "function"),
    ("/numeric",      "function"),
    ("/ranges",       "function"),
    ("/execution",    "function"),
    ("/container",    "class"),
    ("/memory",       "class"),
    ("/string",       "class"),
    ("/thread",       "class"),
    ("/chrono",       "class"),
    ("/io",           "class"),
    ("/iostream",     "class"),
    ("/filesystem",   "class"),
    ("/locale",       "class"),
    ("/regex",        "class"),
    ("/coroutine",    "class"),
    ("/iterator",     "class"),
    ("/concepts",     "concept"),
    ("/utility",      "function"),
    ("/types",        "type"),
    ("/language",     "language"),
    ("/preprocessor", "preprocessor"),
    ("/keyword",      "keyword"),
    ("/error",        "class"),
]

_CHAPTER_BY_SEGMENT: dict[str, int] = {
    "algorithm": 1, "numeric": 2, "ranges": 3, "execution": 4,
    "container": 5, "memory": 6, "string": 7, "thread": 8,
    "chrono": 9, "io": 10, "iostream": 10, "filesystem": 11,
    "locale": 12, "regex": 13, "coroutine": 14, "iterator": 15,
    "concepts": 16, "utility": 17, "types": 18, "language": 19,
    "preprocessor": 20, "keyword": 20, "error": 21,
}

# Pages in these categories are indexed in sections but NOT in symbols.
_SKIP_SYMBOLS = {"language", "preprocessor", "keyword"}


def _infer_category(url_path: str) -> str:
    norm = "/" + url_path.replace("\\", "/").lower()
    for seg, cat in _CATEGORY_SEGMENTS:
        if seg in norm:
            return cat
    return "other"


def _infer_chapter(url_path: str) -> int:
    parts = url_path.replace("\\", "/").lower().split("/")
    for part in parts:
        ch = _CHAPTER_BY_SEGMENT.get(part)
        if ch is not None:
            return ch
    return 99


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
# cppreference HTML parsing
# ---------------------------------------------------------------------------

def _extract_header(content: Tag) -> str | None:
    """Return the first '<foo>' header found in 'Defined in header ...' text."""
    for row in content.find_all(class_="t-dsc-header"):
        code = row.find("code") or row.find("tt")
        if code:
            txt = code.get_text(strip=True)
            if txt:
                return txt if txt.startswith("<") else f"<{txt}>"

    # Plain-text fallback
    raw = content.get_text(" ", strip=True)
    m = re.search(r"Defined in header\s+[<\"]([^>\"\n]{1,80})[>\"]", raw)
    if m:
        return f"<{m.group(1).strip()}>"
    return None


def _extract_since_version(content: Tag) -> int | None:
    """Return the C++ standard version when this symbol was first introduced.

    Strategy: search the t-dcl-begin declaration table first (most accurate —
    each row carries its own t-since-cxxNN class).  Only if that yields nothing
    do we fall back to a page-wide class scan, then a plain-text regex.  Taking
    the minimum of the DCL table's results avoids picking up C++11 markers from
    sidebars/navboxes on newer (e.g. C++20 ranges) pages.
    """
    def _parse_years(els) -> list[int]:
        out: list[int] = []
        for el in els:
            for cls in el.get("class", []):
                mm = re.match(r"t-since-cxx(\d+)$", cls)
                if mm:
                    y = int(mm.group(1))
                    # 2-digit: 98/03 -> 1998/2003; 11-26 -> 2011-2026; 4-digit pass through
                    if y < 100:
                        full_y = 1900 + y if y >= 50 else 2000 + y
                    else:
                        full_y = y
                    out.append(full_y)
        return out

    # 1. Prefer the t-dcl-begin declaration table — per-overload version tags.
    dcl_begin = content.find(class_="t-dcl-begin")
    if dcl_begin:
        years = _parse_years(dcl_begin.find_all(class_=re.compile(r"t-since-cxx\d+")))
        if years:
            return min(years)

    # 2. Page-wide CSS class scan (catches named-requirements pages, etc.)
    years = _parse_years(content.find_all(class_=re.compile(r"t-since-cxx\d+")))
    if years:
        return min(years)

    # 3. Plain-text fallback: "(since C++23)"
    for mm in re.finditer(r"\(since C\+\+(\d+)\)", content.get_text()):
        y = int(mm.group(1))
        full_y = 1900 + y if (y < 100 and y >= 50) else (2000 + y if y < 100 else y)
        years.append(full_y)

    return min(years) if years else None


def _extract_synopsis(content: Tag) -> str | None:
    """Extract function/class declaration from the t-dcl-begin table."""
    dcl = content.find(class_="t-dcl-begin")
    if dcl:
        parts: list[str] = []
        for row in dcl.find_all(class_=re.compile(r"\bt-dcl\b")):
            cells = row.find_all("td", recursive=False)
            if cells:
                txt = cells[0].get_text(" ", strip=True)
                if len(txt) > 3:
                    parts.append(txt)
            if len(parts) >= 4:
                parts.append("...")
                break
        if parts:
            return "\n".join(parts)

    # Fallback: first substantial <code> block
    code = content.find("code")
    if code:
        txt = code.get_text(strip=True)
        if len(txt) > 10:
            return txt[:500]
    return None


def _extract_prose(content: Tag) -> str:
    """Return clean readable text from the page body, capped at 60 KB."""
    c = copy(content)
    for sel in ["#toc", ".navbox", ".t-navbar", "script", "style", ".noprint"]:
        for el in c.select(sel):
            el.decompose()
    text = c.get_text("\n", strip=True)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text[:60000]


def parse_cpp_page(html_path: Path, url_path: str) -> dict | None:
    """
    Parse one cppreference HTML page.  Returns a data dict or None for
    navigation / stub pages with no real content heading.
    """
    try:
        raw = html_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None

    soup = BeautifulSoup(raw, "lxml")

    heading = soup.find("h1", id="firstHeading") or soup.find("h1", class_="firstHeading")
    if not heading:
        return None

    raw_title = heading.get_text(" ", strip=True)
    if not raw_title or len(raw_title) < 2:
        return None

    content = soup.find("div", id="mw-content-text") or soup.find("div", class_="mw-parser-output")
    if not content:
        return None

    # Strip template params for the short lookup name
    clean_name = re.sub(r"<[^>]*>", "", raw_title).strip()
    short_name = re.sub(r"[\s,]+$", "", clean_name.split("::")[-1].strip())

    return {
        "name":       short_name,
        "full_name":  clean_name,
        "title":      raw_title,
        "header":     _extract_header(content),
        "category":   _infer_category(url_path),
        "chapter":    _infer_chapter(url_path),
        "since_cxx":  _extract_since_version(content),
        "deprecated": 1 if content.find(class_=re.compile(r"t-deprecated")) else 0,
        "synopsis":   _extract_synopsis(content),
        "text":       _extract_prose(content),
        "url_path":   url_path,
    }


# ---------------------------------------------------------------------------
# DB helpers
# ---------------------------------------------------------------------------

def open_db(db_path: Path) -> sqlite3.Connection:
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    conn.executescript(SCHEMA)
    conn.commit()
    return conn


def get_indexed_hashes(conn: sqlite3.Connection) -> dict[str, str]:
    """Return {file_path: sha256} for all already-indexed files."""
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


def insert_page(conn: sqlite3.Connection, data: dict) -> None:
    """Insert or replace a parsed page into sections and optionally symbols."""
    # Remove any previously indexed version of this URL
    conn.execute("DELETE FROM sections WHERE section_id = ?", (data["url_path"],))

    cur = conn.execute(
        "INSERT INTO sections (chapter, section_id, title, page_start, page_end, text) "
        "VALUES (:chapter, :url_path, :title, NULL, NULL, :text)",
        data,
    )
    section_row_id = cur.lastrowid

    if data["category"] not in _SKIP_SYMBOLS:
        conn.execute("DELETE FROM symbols WHERE url_path = ?", (data["url_path"],))
        conn.execute(
            "INSERT INTO symbols "
            "(name, full_name, header, category, since_cxx, deprecated, synopsis, url_path, section_id) "
            "VALUES (:name, :full_name, :header, :category, :since_cxx, :deprecated, :synopsis, :url_path, :section_id)",
            {**data, "section_id": section_row_id},
        )


# ---------------------------------------------------------------------------
# Archive root discovery
# ---------------------------------------------------------------------------

def find_cpp_root(archive_root: Path) -> Path | None:
    """Return the en/cpp/ subtree inside the archive, checking common layouts."""
    for candidate in [
        archive_root / "reference" / "en" / "cpp",
        archive_root / "en" / "cpp",
        archive_root / "cpp",
    ]:
        if candidate.is_dir():
            return candidate

    # Deep search fallback
    for dirpath, _dirs, _files in os.walk(archive_root):
        p = Path(dirpath)
        if p.name == "cpp" and p.parent.name == "en":
            return p
    return None


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Index a cppreference offline HTML archive into SQLite.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("archive_root", help="Root of the extracted cppreference archive")
    parser.add_argument("book_dir",     help="Output directory for index.db  (e.g. books/cppreference)")
    parser.add_argument("--force",  action="store_true", help="Re-index all files regardless of hash")
    parser.add_argument("--limit",  type=int, default=0,  help="Stop after indexing N pages (0 = no limit)")
    args = parser.parse_args()

    archive_root = Path(args.archive_root)
    book_dir     = Path(args.book_dir)
    book_dir.mkdir(parents=True, exist_ok=True)
    db_path = book_dir / "index.db"

    cpp_root = find_cpp_root(archive_root)
    if cpp_root is None:
        print(f"ERROR: Could not find en/cpp/ under {archive_root}")
        print("Expected layout:  <archive_root>/reference/en/cpp/*.html")
        sys.exit(1)

    print(f"Archive root : {archive_root}")
    print(f"cpp/ root    : {cpp_root}")
    print(f"Output DB    : {db_path}")

    conn = open_db(db_path)
    known = {} if args.force else get_indexed_hashes(conn)

    html_files = sorted(cpp_root.rglob("*.html"))
    total = len(html_files)
    print(f"HTML pages   : {total}")
    if args.limit:
        print(f"Limit        : {args.limit}")
    if known:
        print(f"Already idx  : {len(known)} files (use --force to re-index all)")

    processed = skipped = errors = 0
    limit_hit = False

    try:
        for html_path in html_files:
            if args.limit and processed >= args.limit:
                limit_hit = True
                break

            rel = str(html_path.relative_to(cpp_root))
            sha = sha256_file(html_path)

            if not args.force and known.get(rel) == sha:
                skipped += 1
                continue

            data = parse_cpp_page(html_path, rel)
            if data is None:
                # Nav / stub page — record hash so we don't re-check it
                upsert_source(conn, rel, sha)
                skipped += 1
                continue

            try:
                insert_page(conn, data)
                upsert_source(conn, rel, sha)
                processed += 1
            except Exception as exc:
                errors += 1
                print(f"\n  [error] {rel}: {exc}")

            if processed % 250 == 0 and processed > 0:
                conn.commit()
                print(f"  {processed} pages indexed ...", flush=True)

        if not limit_hit:
            print("Rebuilding FTS indexes ...")
            conn.executescript("""
                INSERT INTO sections_fts(sections_fts) VALUES('rebuild');
                INSERT INTO symbols_fts(symbols_fts)   VALUES('rebuild');
            """)
        conn.commit()

    finally:
        conn.close()

    print(f"\n{'='*50}")
    print(f"Indexed   : {processed}")
    print(f"Skipped   : {skipped}  (hash unchanged or non-content)")
    print(f"Errors    : {errors}")
    print(f"DB        : {db_path}")
    if limit_hit:
        print(f"(Limit of {args.limit} reached — FTS not rebuilt; run without --limit for a full index)")
    print(f"\nQuery with:")
    print(f'  python books/lookup.py "{book_dir}" "unique_ptr"')
    print(f'  python books/lookup.py "{book_dir}" --symbol unique_ptr')
    print(f'  python books/lookup.py "{book_dir}" --since 20 --header "<memory>"')


if __name__ == "__main__":
    main()
