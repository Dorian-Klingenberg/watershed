"""
lookup.py — Query a book index built by extract_index.py.

Usage:
    python books/lookup.py <book_dir> "search term"      # full-text keyword search
    python books/lookup.py <book_dir> --chapter N        # all sections in chapter N
    python books/lookup.py <book_dir> --section "1.2"    # specific section by ID
    python books/lookup.py <book_dir> --page N           # section containing page N
    python books/lookup.py <book_dir> --list             # list all sections (title only)
    python books/lookup.py <book_dir> --figures          # list all figures
    python books/lookup.py <book_dir> --figures --chapter N   # figures in chapter N
    python books/lookup.py <book_dir> --tables           # list all tables

Options:
    --excerpt N     Characters of text to show per result (default: 500, 0 = full text)
    --results N     Max results to return (default: 5)

Examples:
    python books/lookup.py books/mit-18.06 "dot product"
    python books/lookup.py books/mit-18.06 --chapter 2 --results 20
    python books/lookup.py books/mit-18.06 --page 47
    python books/lookup.py books/mit-18.06 "eigenvalue" --excerpt 800
    python books/lookup.py books/mit-18.06 --figures --chapter 1

No external dependencies — uses Python stdlib only.
"""

import sys
import os
import sqlite3
import argparse
import textwrap

# Ensure Unicode output works on Windows consoles (cp1252 → utf-8)
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

parser = argparse.ArgumentParser(
    description="Query a book index built by extract_index.py.",
    add_help=True
)
parser.add_argument("book_dir", help="Path to the book directory (contains index.db)")
parser.add_argument("query",    nargs="?", default=None, help="Full-text search term")
parser.add_argument("--chapter",      type=int,  default=None, help="Filter by chapter number")
parser.add_argument("--section",      type=str,  default=None, help='Section ID, e.g. "1.2"')
parser.add_argument("--page",         type=int,  default=None, help="Page number to look up")
parser.add_argument("--list",         action="store_true",     help="List all sections (no text)")
parser.add_argument("--figures",      action="store_true",     help="List figures (use with --chapter to filter)")
parser.add_argument("--tables",       action="store_true",     help="List tables extracted from the book")
parser.add_argument("--symbol",       type=str,  default=None, help="Look up a C++ symbol by name (e.g. unique_ptr)")
parser.add_argument("--since",        type=int,  default=None, help="List symbols introduced in C++N or later (e.g. --since 20)")
parser.add_argument("--header",       type=str,  default=None, help="List symbols defined in a specific header (e.g. '<memory>')")
parser.add_argument("--list-symbols", action="store_true",     help="List all indexed symbols (brief)")
parser.add_argument("--tag",          type=str,  default=None, help="Filter sections by tag (e.g. descriptor-heap, RAII)")
parser.add_argument("--excerpt",  type=int,  default=500,  help="Chars of text per result (0=full)")
parser.add_argument("--results",  type=int,  default=5,    help="Max results to return")

args = parser.parse_args()

has_action = any([
    args.query, args.chapter, args.section, args.page,
    args.list, args.figures, args.tables,
    args.symbol, args.since, args.header, args.list_symbols, args.tag,
])
if not has_action:
    print("ERROR: Provide a search term or one of --chapter, --section, --page, --list, --figures, --tables")
    parser.print_help()
    sys.exit(1)


# ---------------------------------------------------------------------------
# Open database
# ---------------------------------------------------------------------------

db_path = os.path.join(args.book_dir, "index.db")

if not os.path.isfile(db_path):
    print(f"ERROR: No index found at {db_path}")
    print(f"       Run: python books/extract_index.py <pdf_path> \"{args.book_dir}\"")
    sys.exit(1)

conn = sqlite3.connect(db_path)
conn.row_factory = sqlite3.Row
cur = conn.cursor()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

SEPARATOR = "-" * 60

def print_section(row, excerpt_len):
    sid = row["section_id"] or "n/a"
    print(SEPARATOR)
    print(f"Section {sid}  |  {row['title']}")
    print(f"Chapter {row['chapter']}  |  Pages {row['page_start']}-{row['page_end']}")
    print()
    text = row["text"] or ""
    if excerpt_len > 0 and len(text) > excerpt_len:
        text = text[:excerpt_len].rstrip() + f"\n… [{len(row['text']) - excerpt_len} more chars]"
    print(textwrap.fill(text, width=80, subsequent_indent="  "))
    print()


def print_list_row(row):
    sid = row["section_id"] or "n/a"
    print(f"  Ch {row['chapter']:2d}  s{sid:<8}  p.{row['page_start'] or '?':>4}-{row['page_end'] or '?':<4}  {row['title']}")


def table_exists(name: str) -> bool:
    cur.execute("SELECT name FROM sqlite_master WHERE type='table' AND name=?", (name,))
    return cur.fetchone() is not None


# ---------------------------------------------------------------------------
# Query: list figures
# ---------------------------------------------------------------------------

if args.figures:
    if not table_exists("figures"):
        print("No figures table found. Re-run extract_index.py to rebuild the index.")
        conn.close()
        sys.exit(1)

    if args.chapter is not None:
        cur.execute(
            "SELECT * FROM figures WHERE chapter = ? ORDER BY page, id LIMIT ?",
            (args.chapter, args.results * 10)
        )
    else:
        cur.execute("SELECT * FROM figures ORDER BY chapter, page, id")

    rows = cur.fetchall()
    if not rows:
        ch_note = f" in chapter {args.chapter}" if args.chapter else ""
        print(f"No figures found{ch_note}.")
    else:
        ch_note = f" (chapter {args.chapter})" if args.chapter else ""
        print(f"\nFigures{ch_note} — {len(rows)} total:\n")
        print(f"  {'Ch':>3}  {'Page':>4}  {'Label':<12}  {'Image':<30}  Caption")
        print(SEPARATOR)
        for row in rows:
            img = row["image_path"] or "(no image)"
            cap = (row["caption"] or "")[:60]
            print(f"  {row['chapter']:>3}  {(row['page'] or '?'):>4}  {(row['label'] or ''):.<12}  {img:<30}  {cap}")
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Query: list tables
# ---------------------------------------------------------------------------

if args.tables:
    if not table_exists("book_tables"):
        print("No book_tables table found. Re-run extract_index.py to rebuild the index.")
        conn.close()
        sys.exit(1)

    cur.execute("SELECT * FROM book_tables ORDER BY chapter, page, id")
    rows = cur.fetchall()
    if not rows:
        print("No tables found in index.")
    else:
        print(f"\nTables — {len(rows)} total:\n")
        for row in rows:
            print(SEPARATOR)
            print(f"Chapter {row['chapter']}  |  Page {row['page'] or '?'}")
            cap = row["caption"] or "(no caption)"
            print(f"Caption: {cap}")
            csv_preview = (row["content_csv"] or "")[:400]
            if csv_preview:
                print("Content (CSV preview):")
                for line in csv_preview.splitlines()[:6]:
                    print(f"  {line}")
            print()
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Helpers for symbol queries
# ---------------------------------------------------------------------------

def _symbols_table_exists() -> bool:
    return table_exists("symbols")

def _fmt_std(since_cxx) -> str:
    """Convert stored year (e.g. 2011) to display string (e.g. 'C++11')."""
    if not since_cxx:
        return "?"
    y = int(since_cxx)
    return f"C++{y % 100:02d}"

def print_symbol(row) -> None:
    name  = row["full_name"] or row["name"]
    hdr   = row["header"]    or "-"
    cat   = row["category"]  or "-"
    since = _fmt_std(row["since_cxx"])
    dep   = "  [DEPRECATED]" if row["deprecated"] else ""
    print(SEPARATOR)
    print(f"{name}{dep}")
    print(f"  Header   : {hdr}")
    print(f"  Category : {cat}   Since: {since}")
    syn = (row["synopsis"] or "").strip()
    if syn:
        print(f"  Synopsis :")
        for line in syn.splitlines()[:6]:
            print(f"    {line}")
    print()

def print_symbol_brief(row) -> None:
    name  = (row["full_name"] or row["name"])[:48]
    hdr   = (row["header"] or "-")[:20]
    since = _fmt_std(row["since_cxx"])
    dep   = " *deprecated*" if row["deprecated"] else ""
    print(f"  {since:<8}  {hdr:<22}  {name}{dep}")


# ---------------------------------------------------------------------------
# Query: --symbol NAME
# ---------------------------------------------------------------------------

if args.symbol:
    if not _symbols_table_exists():
        print("No symbols table in this index.  (Only cppreference indexes have symbols.)")
        conn.close()
        sys.exit(1)

    term = args.symbol.strip()
    # Try exact match on short name or full name first
    cur.execute(
        "SELECT * FROM symbols WHERE name = ? OR full_name = ? "
        "ORDER BY since_cxx LIMIT ?",
        (term, term, args.results)
    )
    rows = cur.fetchall()

    if not rows:
        # Fuzzy: LIKE on name
        cur.execute(
            "SELECT * FROM symbols WHERE name LIKE ? OR full_name LIKE ? "
            "ORDER BY since_cxx LIMIT ?",
            (f"%{term}%", f"%{term}%", args.results)
        )
        rows = cur.fetchall()

    if not rows:
        # FTS fallback
        try:
            cur.execute(
                """
                SELECT s.* FROM symbols_fts f
                JOIN symbols s ON f.rowid = s.id
                WHERE symbols_fts MATCH ?
                ORDER BY rank LIMIT ?
                """,
                (term, args.results)
            )
            rows = cur.fetchall()
        except Exception:
            pass

    if not rows:
        print(f"No symbol found matching '{term}'.")
    else:
        print(f"\nSymbol results for '{term}' — {len(rows)} match(es):\n")
        for row in rows:
            print_symbol(row)
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Query: --since N  and/or  --header HEADER
# ---------------------------------------------------------------------------

if args.since is not None or args.header is not None:
    if not _symbols_table_exists():
        print("No symbols table in this index.  (Only cppreference indexes have symbols.)")
        conn.close()
        sys.exit(1)

    sql_q  = "SELECT * FROM symbols WHERE 1=1"
    params = []

    if args.since is not None:
        # Normalise: 98->1998, 03->2003, 11->2011, 20->2020, 23->2023, etc.
        x = args.since
        if x >= 2000:
            since_year = x
        elif x >= 50:
            since_year = 1900 + x  # 98 -> 1998, 03 isn't reachable this way but safe
        else:
            since_year = 2000 + x  # 11->2011, 20->2020, 23->2023
        sql_q += " AND since_cxx >= ?"
        params.append(since_year)

    if args.header is not None:
        hdr = args.header.strip()
        sql_q += " AND header LIKE ?"
        params.append(f"%{hdr}%")

    sql_q += " ORDER BY since_cxx, name LIMIT ?"
    params.append(args.results * 10)

    cur.execute(sql_q, params)
    rows = cur.fetchall()

    label_parts = []
    if args.since is not None:
        label_parts.append(f"since C++{args.since}")
    if args.header is not None:
        label_parts.append(f"header={args.header}")

    if not rows:
        print(f"No symbols found ({', '.join(label_parts)}).")
    else:
        print(f"\nSymbols ({', '.join(label_parts)}) - {len(rows)} result(s):\n")
        print(f"  {'Since':<8}  {'Header':<22}  Name")
        print(SEPARATOR)
        for row in rows:
            print_symbol_brief(row)
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Query: --list-symbols
# ---------------------------------------------------------------------------

if args.list_symbols:
    if not _symbols_table_exists():
        print("No symbols table in this index.  (Only cppreference indexes have symbols.)")
        conn.close()
        sys.exit(1)

    cur.execute("SELECT * FROM symbols ORDER BY header, name")
    rows = cur.fetchall()
    print(f"\n  {'Since':<8}  {'Header':<22}  Name")
    print(SEPARATOR)
    try:
        for row in rows:
            print_symbol_brief(row)
        print(f"\n{len(rows)} symbol(s) in index.")
    except (BrokenPipeError, OSError):
        pass
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Query: list all sections
# ---------------------------------------------------------------------------

if args.list:
    cur.execute("SELECT * FROM sections ORDER BY chapter, page_start")
    rows = cur.fetchall()
    print(f"\n{'Ch':>4}  {'Section':<10}  {'Pages':<12}  Title")
    print(SEPARATOR)
    for row in rows:
        print_list_row(row)
    print(f"\n{len(rows)} section(s) in index.")
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Query: by page
# ---------------------------------------------------------------------------

if args.page is not None:
    cur.execute(
        "SELECT * FROM sections WHERE page_start <= ? AND page_end >= ? "
        "ORDER BY page_start LIMIT ?",
        (args.page, args.page, args.results)
    )
    rows = cur.fetchall()
    if not rows:
        print(f"No section found containing page {args.page}.")
    else:
        print(f"\nPage {args.page}:\n")
        for row in rows:
            print_section(row, args.excerpt)
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Query: by section ID
# ---------------------------------------------------------------------------

if args.section is not None:
    cur.execute(
        "SELECT * FROM sections WHERE section_id = ? LIMIT ?",
        (args.section, args.results)
    )
    rows = cur.fetchall()
    if not rows:
        print(f"No section with ID '{args.section}' found.")
    else:
        for row in rows:
            print_section(row, args.excerpt)
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Query: by chapter (with optional keyword filter)
# ---------------------------------------------------------------------------

if args.chapter is not None and not args.query:
    cur.execute(
        "SELECT * FROM sections WHERE chapter = ? ORDER BY page_start LIMIT ?",
        (args.chapter, args.results)
    )
    rows = cur.fetchall()
    if not rows:
        print(f"No sections found for chapter {args.chapter}.")
    else:
        print(f"\nChapter {args.chapter} — {len(rows)} section(s):\n")
        for row in rows:
            print_section(row, args.excerpt)
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Query: --tag TAG  — filter by auto-tagged topic
# ---------------------------------------------------------------------------

if args.tag is not None:
    tag = args.tag.strip()
    cur.execute(
        "SELECT * FROM sections WHERE tags LIKE ? ORDER BY chapter, page_start LIMIT ?",
        (f"%{tag}%", args.results)
    )
    rows = cur.fetchall()
    if not rows:
        print(f"No sections tagged with '{tag}'.")
        print("Tip: run --list to see all sections, or check tag vocabulary in AGENTS.md.")
    else:
        print(f"\nSections tagged '{tag}' — {len(rows)} result(s):\n")
        for row in rows:
            print_section(row, args.excerpt)
    conn.close()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Query: full-text keyword search (with optional chapter filter)
# ---------------------------------------------------------------------------

if args.query:
    if args.chapter is not None:
        cur.execute(
            """
            SELECT s.* FROM sections_fts f
            JOIN sections s ON f.rowid = s.id
            WHERE sections_fts MATCH ?
              AND s.chapter = ?
            ORDER BY rank
            LIMIT ?
            """,
            (args.query, args.chapter, args.results)
        )
    else:
        cur.execute(
            """
            SELECT s.* FROM sections_fts f
            JOIN sections s ON f.rowid = s.id
            WHERE sections_fts MATCH ?
            ORDER BY rank
            LIMIT ?
            """,
            (args.query, args.results)
        )
    rows = cur.fetchall()
    if not rows:
        print(f"No results for '{args.query}'.")
    else:
        ch_note = f" in chapter {args.chapter}" if args.chapter else ""
        print(f"\nResults for '{args.query}'{ch_note} — {len(rows)} match(es):\n")
        for row in rows:
            print_section(row, args.excerpt)

conn.close()
