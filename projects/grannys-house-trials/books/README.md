# Local Knowledge Base

Agent-accessible reference material stored as SQLite full-text indexes.
Query with `lookup.py`; index new sources with `extract_index.py` (PDFs) or `extract_html.py` (cppreference HTML archive).

---

## Directory layout

```
books/
  extract_index.py   — PDF → SQLite indexer (uses Docling)
  extract_html.py    — cppreference HTML archive → SQLite indexer
  lookup.py          — unified query interface (works with both index types)
  README.md          — this file
  <book_slug>/       — one directory per indexed source
    index.db         — SQLite database (sections FTS + symbols FTS)
    text/            — Markdown chapters extracted from PDF (PDF indexes only)
    figures/         — PNG figures (PDF indexes only)
    curriculum.yaml  — optional chapter → page mapping (PDF indexes only)
```

---

## Adding a PDF book

```powershell
# Install deps (once)
pip install docling pyyaml

# Index the PDF
python books/extract_index.py "path/to/book.pdf" books/my-book

# Optional: re-index if the PDF changed (hash check skips unchanged files)
python books/extract_index.py "path/to/book.pdf" books/my-book --force
```

---

## Adding the cppreference offline archive

1. Download the HTML archive from <https://en.cppreference.com/w/Cppreference:Archives>
   - Choose the `.tar.xz` or `.zip` file (NOT the Qt `.qch` file).
2. Extract it so you have a directory containing `en/cpp/*.html`.
3. Index it:

```powershell
# Install deps (once)
pip install beautifulsoup4 lxml

# First run — indexes ~10 000 pages, takes several minutes
python books/extract_html.py "path/to/extracted/archive" books/cppreference

# Subsequent runs — only changed/new files are re-indexed (fast)
python books/extract_html.py "path/to/extracted/archive" books/cppreference

# Force full re-index
python books/extract_html.py "path/to/extracted/archive" books/cppreference --force

# Test with a small sample before committing to the full run
python books/extract_html.py "path/to/extracted/archive" books/cppreference --limit 200
```

---

## Querying

### Full-text keyword search (works with both index types)

```powershell
python books/lookup.py books/cppreference "unique_ptr"
python books/lookup.py books/my-book "RAII destructor"
python books/lookup.py books/cppreference "move semantics" --results 10 --excerpt 800
```

### Symbol lookup (cppreference only)

```powershell
# Exact / fuzzy name match — shows synopsis + header
python books/lookup.py books/cppreference --symbol unique_ptr
python books/lookup.py books/cppreference --symbol "ranges::sort"

# All symbols introduced in C++20 or later
python books/lookup.py books/cppreference --since 20

# All symbols in <memory> introduced since C++11
python books/lookup.py books/cppreference --since 11 --header "<memory>"

# Browse everything in <expected>
python books/lookup.py books/cppreference --header "<expected>"

# Full symbol listing (can be long)
python books/lookup.py books/cppreference --list-symbols
```

### Chapter / section / page navigation (PDF indexes)

```powershell
python books/lookup.py books/my-book --chapter 3
python books/lookup.py books/my-book --section "3.2"
python books/lookup.py books/my-book --page 47
python books/lookup.py books/my-book --list        # table of contents
python books/lookup.py books/my-book --figures
python books/lookup.py books/my-book --tables
```

---

## Database schema

All index databases share a common base schema so `lookup.py` works with any of them.

| Table | Purpose |
|---|---|
| `sections` | Prose text split by chapter/section; FTS5 via `sections_fts` |
| `figures` | Extracted figure metadata + image paths (PDF only) |
| `book_tables` | Extracted table data as CSV (PDF only) |
| `sources` | Per-file SHA-256 hash for incremental re-indexing |
| `symbols` | Compact API metadata: name, header, synopsis, since C++NN (cppreference only) |

---

## Performance notes

- **Incremental hashing** — both indexers hash each input file and skip it on re-runs if the hash matches. Large archives (cppreference ~10k files) run in seconds on subsequent calls.
- **FTS5** — SQLite's built-in full-text search. Keyword queries return in < 10 ms even on large indexes.
- **Symbol B-tree indexes** — `idx_sym_name` / `idx_sym_full` on `symbols(name)` and `symbols(full_name)` make exact symbol lookup O(log n) regardless of index size.
- **`symbols_fts`** — falls back to full-text for fuzzy/concept searches ("all things related to allocators").
