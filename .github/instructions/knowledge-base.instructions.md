---
description: Guidelines for using the local C++ knowledge base in projects/grannys-house-trials/books/ — cppreference, DirectX-Specs, C++ Core Guidelines, Win32 API.
---

# Local Knowledge Base (books/)

The GHT project contains a local SQLite-backed knowledge base at `projects/grannys-house-trials/books/`
for C++/DirectX/Win32 reference material.
**Always query this before fetching from the web** — it is faster, works offline, and is already indexed for agent use.

## What is indexed

| Directory | Contents |
|---|---|
| `books/cppreference/` | cppreference.com — full C++ stdlib + language reference, all symbols, since-versions |
| `books/directx-specs/` | microsoft/DirectX-Specs — D3D12 engineering specs (barriers, descriptor heaps, raytracing…) |
| `books/cpp-guidelines/` | C++ Core Guidelines (Stroustrup + Sutter) — RAII, ownership, lifetimes, interfaces |
| `books/win32-api/` | MicrosoftDocs/win32 — COM, input, windowing, synchronization, DirectX API reference |
| `books/<slug>/` | Any additional PDF books added and indexed |

## How to query

Run from the repository root (`D:\Repos\watershed`):

```powershell
# Keyword search (any index)
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cppreference "unique_ptr move"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "descriptor heap"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cpp-guidelines "RAII ownership"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/win32-api "message loop"

# Tag filter — fast topic lookup without FTS
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs --tag descriptor-heap
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cpp-guidelines --tag RAII
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/win32-api --tag COM

# Symbol lookup (cppreference only) — fastest for "what's the signature of X?"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cppreference --symbol unique_ptr
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cppreference --since 23
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cppreference --header "<memory>"
```

### Tag vocabulary
`descriptor-heap` · `resource-barrier` · `command-list` · `root-signature` · `swap-chain` · `raytracing` · `mesh-shader` · `RAII` · `smart-pointer` · `ownership` · `COM` · `win32-windowing` · `synchronization` · `cpp-best-practices`

## One-command setup (new machine / new project)

```powershell
python projects/grannys-house-trials/books/bootstrap.py          # downloads, indexes, and configures everything
python projects/grannys-house-trials/books/bootstrap.py --books directx-specs cpp-guidelines   # specific books only
python projects/grannys-house-trials/books/bootstrap.py --skip-download --force                 # re-index existing downloads
```

## Post-indexing cleanup

**Delete source downloads after indexing** — they are large and not needed once the DB is built.
`bootstrap.py` will re-download automatically on the next run if needed (SHA-256 verified).

```powershell
# Safe to delete after indexing — total ~780 MB recovered
Remove-Item projects/grannys-house-trials/books/downloads/directx-specs -Recurse -Force   # 25 MB
Remove-Item projects/grannys-house-trials/books/downloads/win32 -Recurse -Force            # 417 MB — very large!
Remove-Item projects/grannys-house-trials/books/downloads/cpp-guidelines.html -Force       # 1.2 MB
Remove-Item projects/grannys-house-trials/books/cppreference-archive -Recurse -Force       # 337 MB
```

## How to add new material

### Markdown repo (GitHub)
```powershell
python projects/grannys-house-trials/books/extract_markdown.py "path/to/clone" projects/grannys-house-trials/books/my-book --filter "docs/**/*.md"
python projects/grannys-house-trials/books/configure.py   # regenerate instruction files
```

### Single HTML page
```powershell
pip install beautifulsoup4 lxml   # first time only
python projects/grannys-house-trials/books/extract_html_page.py "path/to/page.html" projects/grannys-house-trials/books/my-book
python projects/grannys-house-trials/books/configure.py
```

### cppreference HTML archive
```powershell
# Download: https://github.com/PeterFeicht/cppreference-doc/releases
python projects/grannys-house-trials/books/extract_html.py "path/to/extracted/archive" projects/grannys-house-trials/books/cppreference
```

### PDF book
```powershell
pip install docling pyyaml   # first time only
python projects/grannys-house-trials/books/extract_index.py "path/to/book.pdf" projects/grannys-house-trials/books/book-slug
```

## Database schema quick reference

- `sections` + `sections_fts` — full prose, FTS5 keyword search, chapter/section/page nav, `tags` column
- `symbols` + `symbols_fts` — compact API metadata: name, header, synopsis, `since_cxx` (cppreference only)
- `sources` — per-file SHA-256 for incremental re-runs (re-indexing skips unchanged files)
- `figures`, `book_tables` — extracted images and tables (PDF only)

## Learnings

- cppreference.com does NOT offer a PDF download. Use the HTML archive from the PeterFeicht GitHub releases.
- The C++ Core Guidelines is a single ~1.2 MB HTML page — use `extract_html_page.py`, not `extract_html.py`.
- `extract_html_page.py` must use `find_all(['h1','h2','h3'])` to enumerate headings; iterating `content.children` only gets top-level nodes and misses deeply nested headings (yields 1 section instead of 600+).
- The Win32 MicrosoftDocs repo is 56,000+ files and 417 MB — always use `--filter` flags; indexing without them is impractical.
- `lookup.py --tag` uses a LIKE query on the `tags` column (space-separated strings) — fast, no FTS overhead.
- `lookup.py` symbol queries (`--symbol`, `--since`, `--header`) only work on cppreference; they print a clear error on other indexes.
- Both `extract_markdown.py` and `extract_html_page.py` store SHA-256 hashes in `sources`; re-runs on unchanged content are nearly instant.
- FTS5 keyword queries return in < 10 ms; exact-symbol B-tree lookups are even faster.
- After indexing, delete `books/downloads/` and `books/cppreference-archive/` — ~780 MB recovered with no loss of functionality.
