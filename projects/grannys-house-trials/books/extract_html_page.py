"""
extract_html_page.py — Index a single large HTML reference page into SQLite.

Designed for the C++ Core Guidelines and similar single-file HTML references
that use h2/h3 headings to divide content into sections.

Usage:
    python books/extract_html_page.py <html_file> <book_dir> [--force]

Where:
    <html_file>   Path to the downloaded HTML file.
    <book_dir>    Output directory — index.db created here.

Options:
    --force       Re-index even if the file hash is unchanged.

What gets indexed:
    sections + sections_fts — one row per h2/h3 heading block
    sources                 — file SHA-256 for incremental re-indexing

Requires:
    pip install beautifulsoup4 lxml
"""

import argparse
import hashlib
import re
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    from bs4 import BeautifulSoup, Tag
except ImportError:
    print("ERROR: beautifulsoup4 not installed.  Run:  pip install beautifulsoup4 lxml")
    sys.exit(1)


# ---------------------------------------------------------------------------
# DB schema — same structure as other extractors for lookup.py compatibility
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
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path TEXT UNIQUE NOT NULL,
    sha256    TEXT NOT NULL,
    indexed_at TEXT NOT NULL
);
"""


# ---------------------------------------------------------------------------
# Tag inference for Core Guidelines sections
# ---------------------------------------------------------------------------

_SECTION_TAGS: list[tuple[str, str]] = [
    (r"\bRAII\b|resource.?acquisition",             "RAII"),
    (r"unique_ptr|shared_ptr|weak_ptr|smart.?pointer","smart-pointer"),
    (r"\bownership\b|\blifetime\b",                 "RAII"),
    (r"\[\[nodiscard\]\]",                          "nodiscard"),
    (r"const(?:expr|ness|ant)?",                    "const-correctness"),
    (r"interface|abstract",                         "interfaces"),
    (r"destructor|~[A-Z]",                          "RAII"),
    (r"noexcept|exception",                         "exceptions"),
    (r"\bspan\b|\bgsl::",                           "gsl"),
    (r"concurren|thread|mutex",                     "concurrency"),
    (r"template|concept|requires",                  "templates"),
    (r"move.?semantic|rvalue",                       "move-semantics"),
    (r"copy|assignment.?operator",                  "value-semantics"),
    (r"inherit|virtual|polymorphi",                 "OOP"),
    (r"enum(?:\s+class)?",                          "enums"),
    (r"assert|contract|precondition|postcondition", "contracts"),
]

# Chapter mapping from Core Guidelines section letter prefixes
_CHAPTER_MAP: dict[str, int] = {
    "p":  1,   # Philosophy
    "i":  2,   # Interfaces
    "f":  3,   # Functions
    "c":  4,   # Classes and class hierarchies
    "r":  5,   # Resource management
    "e":  6,   # Error handling
    "es": 7,   # Expressions and statements
    "per":8,   # Performance
    "cp": 9,   # Concurrency and parallelism
    "t":  10,  # Templates and generic programming
    "cpl":11,  # C-style programming
    "sf": 12,  # Source files
    "sl": 13,  # The Standard Library
    "a":  14,  # Architectural ideas
    "nr": 15,  # Non-Rules and myths
    "rf": 16,  # References
    "pro":17,  # Profiles
    "gsl":18,  # GSL
    "nl": 19,  # Naming and layout
    "faq":20,  # FAQ
    "appendix":21,
}


def _infer_chapter(section_id: str) -> int:
    """Map section ID (like 'Rp-aims' or 'S-introduction') to chapter number."""
    s = section_id.lstrip("SR-").lower().split("-")[0].split(".")[0]
    return _CHAPTER_MAP.get(s, 99)


def _infer_tags(title: str, text: str) -> str:
    tags: set[str] = set()
    combined = title + " " + text[:800]
    for pattern, tag in _SECTION_TAGS:
        if re.search(pattern, combined, re.IGNORECASE):
            tags.add(tag)
    # Always tag everything as cpp-best-practices
    tags.add("cpp-best-practices")
    return " ".join(sorted(tags))


# ---------------------------------------------------------------------------
# HTML parsing — split on h2/h3 headings
# ---------------------------------------------------------------------------

def parse_single_page(html_path: Path) -> list[dict]:
    """
    Parse a single large HTML reference page into a list of section dicts.
    Splits on h2 and h3 headings — each heading starts a new section.

    Uses find_all to locate headings, then collects text between consecutive
    headings via the 'next_siblings' pattern. This works regardless of how
    deeply the headings are nested in the document tree.
    """
    raw = html_path.read_text(encoding="utf-8", errors="replace")
    soup = BeautifulSoup(raw, "lxml")

    # Find the main content area
    content = (
        soup.find("div", id="content") or
        soup.find("div", class_="markdown-body") or
        soup.find("article") or
        soup.find("body")
    )
    if not content:
        return []

    # Collect all headings as anchor points — include h1 so we split
    # at every top-level rule group (P, I, F, C, R, E, ES …)
    headings = content.find_all(["h1", "h2", "h3"])
    if not headings:
        return []

    sections: list[dict] = []

    for i, heading in enumerate(headings):
        heading_id    = heading.get("id", "")
        heading_title = heading.get_text(" ", strip=True)
        if not heading_id:
            heading_id = re.sub(r"[^\w-]", "-", heading_title.lower())[:80].strip("-")

        # Collect text from all nodes between this heading and the next
        # by traversing next siblings at the same depth as `heading`.
        # Because the document may not be flat, we fall back to grabbing
        # the parent's text range — but the simplest reliable way for a
        # large flat file like the Core Guidelines is to use `.next_siblings`.
        text_parts: list[str] = []
        next_heading = headings[i + 1] if i + 1 < len(headings) else None

        for sibling in heading.next_siblings:
            if not isinstance(sibling, Tag):
                continue
            # Stop when we hit the next heading (same level or higher)
            if sibling.name in ("h1", "h2", "h3"):
                break
            # If this sibling *contains* the next heading, stop
            if next_heading and sibling.find(next_heading.name, id=next_heading.get("id")):
                # Collect text only up to the sub-heading
                for child in sibling.children:
                    if isinstance(child, Tag) and child.find(
                        next_heading.name, id=next_heading.get("id")
                    ):
                        break
                    txt = child.get_text(" ", strip=True) if isinstance(child, Tag) else str(child).strip()
                    if txt:
                        text_parts.append(txt)
                break
            txt = sibling.get_text(" ", strip=True)
            if txt:
                text_parts.append(txt)

        text = "\n".join(text_parts).strip()
        text = re.sub(r"\n{3,}", "\n\n", text)[:60000]

        if len(text) < 20:
            continue  # Skip headings with no substantive text

        sections.append({
            "section_id": heading_id,
            "title":      heading_title,
            "chapter":    _infer_chapter(heading_id),
            "text":       text,
            "tags":       _infer_tags(heading_title, text),
        })

    return sections


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
    return conn


def upsert_source(conn: sqlite3.Connection, file_path: str, sha256: str) -> None:
    now = datetime.now(timezone.utc).isoformat()
    conn.execute(
        "INSERT INTO sources (file_path, sha256, indexed_at) VALUES (?, ?, ?) "
        "ON CONFLICT(file_path) DO UPDATE SET sha256=excluded.sha256, indexed_at=excluded.indexed_at",
        (file_path, sha256, now),
    )


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Index a single HTML reference page into SQLite.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("html_file", help="Path to the downloaded HTML file")
    parser.add_argument("book_dir",  help="Output directory for index.db")
    parser.add_argument("--force",   action="store_true", help="Re-index regardless of hash")
    args = parser.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    html_path = Path(args.html_file)
    book_dir  = Path(args.book_dir)
    book_dir.mkdir(parents=True, exist_ok=True)
    db_path = book_dir / "index.db"

    if not html_path.is_file():
        print(f"ERROR: HTML file not found: {html_path}")
        sys.exit(1)

    print(f"HTML file  : {html_path}  ({html_path.stat().st_size // 1024} KB)")
    print(f"Output DB  : {db_path}")

    conn  = open_db(db_path)
    sha   = sha256_file(html_path)
    rel   = str(html_path.name)

    if not args.force:
        existing = conn.execute(
            "SELECT sha256 FROM sources WHERE file_path = ?", (rel,)
        ).fetchone()
        if existing and existing["sha256"] == sha:
            print("No changes detected (hash unchanged). Use --force to re-index.")
            conn.close()
            return

    print("Parsing HTML ...")
    sections = parse_single_page(html_path)
    print(f"Sections found: {len(sections)}")

    # Clear existing data from this source
    conn.execute("DELETE FROM sections")
    conn.execute("DELETE FROM sources")

    for sec in sections:
        conn.execute(
            "INSERT INTO sections (chapter, section_id, title, page_start, page_end, text, tags) "
            "VALUES (:chapter, :section_id, :title, NULL, NULL, :text, :tags)",
            sec,
        )

    upsert_source(conn, rel, sha)

    print("Rebuilding FTS indexes ...")
    conn.executescript("INSERT INTO sections_fts(sections_fts) VALUES('rebuild');")
    conn.commit()
    conn.close()

    print(f"\n{'='*50}")
    print(f"Indexed  : {len(sections)} sections")
    print(f"DB       : {db_path}")
    print(f"\nQuery with:")
    print(f'  python books/lookup.py "{book_dir}" "RAII"')
    print(f'  python books/lookup.py "{book_dir}" --tag RAII')
    print(f'  python books/lookup.py "{book_dir}" "ownership lifetime"')


if __name__ == "__main__":
    main()
