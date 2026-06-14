"""
configure.py — Read all indexed books and auto-generate VS Code instruction files
               plus an AGENTS.md Knowledge Base section.

Usage:
    python books/configure.py [--repo-root PATH]

What it generates:
    .github/instructions/kb-cheatsheets.instructions.md
        applyTo: "**/*.cpp" so VS Code forces it into every Copilot C++ request.
        Contains top patterns for D3D12, RAII, Win32 based on available indexed books.

    AGENTS.md (section between <!-- KB-BEGIN --> and <!-- KB-END --> markers)
        Pre-flight checklist with exact lookup.py commands per topic area.
        Written to be mandatory reading for any agent (Sonnet or mini) before
        writing D3D12/RAII/Win32 code.

Idempotent — safe to run after each new book is indexed.
"""

import json
import sqlite3
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

REPO_LAYOUTS = [".", "..", "../.."]


def find_repo_root(hint: str | None = None) -> Path:
    if hint:
        p = Path(hint)
        if p.is_dir():
            return p
    # Walk up from here
    cwd = Path.cwd()
    for _ in range(6):
        if (cwd / "AGENTS.md").exists() or (cwd / "CMakeLists.txt").exists():
            return cwd
        parent = cwd.parent
        if parent == cwd:
            break
        cwd = parent
    return Path(".")


def load_manifest(repo_root: Path) -> dict:
    manifest_path = repo_root / "books" / "manifest.json"
    if not manifest_path.is_file():
        print(f"WARNING: manifest.json not found at {manifest_path}")
        return {"books": []}
    with open(manifest_path, encoding="utf-8") as fh:
        return json.load(fh)


def db_exists(repo_root: Path, book_dir: str) -> bool:
    db_path = repo_root / book_dir / "index.db"
    return db_path.is_file() and db_path.stat().st_size > 4096


def query_db_for_snippets(repo_root: Path, book_dir: str, queries: list[str], max_per_query: int = 2) -> list[dict]:
    db_path = repo_root / book_dir / "index.db"
    snippets: list[dict] = []
    try:
        conn = sqlite3.connect(db_path)
        conn.row_factory = sqlite3.Row
        for q in queries:
            try:
                rows = conn.execute(
                    "SELECT s.title, s.section_id, snippet(sections_fts, 2, '[[', ']]', '...', 20) AS snip "
                    "FROM sections_fts JOIN sections s ON sections_fts.rowid = s.id "
                    "WHERE sections_fts MATCH ? ORDER BY rank LIMIT ?",
                    (q, max_per_query)
                ).fetchall()
                for r in rows:
                    snippets.append({"title": r["title"], "section_id": r["section_id"], "snip": r["snip"]})
            except sqlite3.OperationalError:
                pass
        conn.close()
    except Exception:
        pass
    return snippets


# ---------------------------------------------------------------------------
# Generate .github/instructions/kb-cheatsheets.instructions.md
# ---------------------------------------------------------------------------

def generate_instruction_file(repo_root: Path, available_books: list[dict]) -> None:
    out_dir  = repo_root / ".github" / "instructions"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "kb-cheatsheets.instructions.md"

    book_ids = {b["id"] for b in available_books}

    lines = [
        "---",
        'description: "Knowledge base query guide — automatically injected for all C++ files."',
        'applyTo: "**/*.cpp"',
        "---",
        "",
        "# Knowledge Base — C++ / DirectX 12 / Win32 Cheatsheet",
        "",
        "> **Before writing any D3D12, RAII, or Win32 code, query the local knowledge base.**",
        "> Run commands from the repository root.",
        "",
        "## Available Books",
        "",
    ]

    for book in available_books:
        lines.append(f"- **{book['name']}** → `books/{book.get('book_dir', book['id']).split('/')[-1]}`")

    lines += [
        "",
        "## Quick Reference — Lookup Commands",
        "",
        "```powershell",
        "# --- D3D12 patterns ---",
    ]
    if "directx-specs" in book_ids:
        lines += [
            'python books/lookup.py books/directx-specs "descriptor heap"',
            'python books/lookup.py books/directx-specs --tag descriptor-heap',
            'python books/lookup.py books/directx-specs "command list recording"',
            'python books/lookup.py books/directx-specs "resource barrier"',
            'python books/lookup.py books/directx-specs "root signature"',
        ]
    lines.append("")
    lines.append("# --- RAII / Modern C++ patterns ---")
    if "cpp-guidelines" in book_ids:
        lines += [
            'python books/lookup.py books/cpp-guidelines "RAII"',
            'python books/lookup.py books/cpp-guidelines --tag RAII',
            'python books/lookup.py books/cpp-guidelines "ownership lifetime"',
            'python books/lookup.py books/cpp-guidelines "unique_ptr"',
        ]
    if "cppreference" in book_ids:
        lines += [
            'python books/lookup.py books/cppreference --symbol unique_ptr',
            'python books/lookup.py books/cppreference --symbol span',
            'python books/lookup.py books/cppreference --since 23',
        ]
    lines.append("")
    lines.append("# --- Win32 API ---")
    if "win32-api" in book_ids:
        lines += [
            'python books/lookup.py books/win32-api "message loop"',
            'python books/lookup.py books/win32-api --tag win32-windowing',
            'python books/lookup.py books/win32-api "COM IUnknown"',
        ]
    lines += [
        "```",
        "",
        "## Usage Notes",
        "",
        "- Add `| Select-Object -First 10` to truncate long output.",
        "- Pipe to `grep -i keyword` to filter results.",
        "- `--tag TAG` filters by auto-tagged topic (fast, no FTS).",
        "- FTS supports: `'descriptor heap'`, `'RAII OR ownership'`, `'unique_ptr -shared_ptr'`",
        "",
        "## D3D12 Patterns to Query Before Coding",
        "",
        "| Task | Command |",
        "|------|---------|",
    ]
    if "directx-specs" in book_ids:
        lines += [
            '| Creating descriptor heap | `python books/lookup.py books/directx-specs "descriptor heap creation"` |',
            '| Resource barriers | `python books/lookup.py books/directx-specs "resource barrier transition"` |',
            '| Command list | `python books/lookup.py books/directx-specs "command list"` |',
            '| Root signature | `python books/lookup.py books/directx-specs "root signature"` |',
            '| Swap chain | `python books/lookup.py books/directx-specs "swap chain"` |',
        ]
    lines += [
        "",
        "## RAII Patterns to Query Before Coding",
        "",
        "| Task | Command |",
        "|------|---------|",
    ]
    if "cpp-guidelines" in book_ids:
        lines += [
            '| RAII wrapper design | `python books/lookup.py books/cpp-guidelines "RAII"` |',
            '| Ownership transfer | `python books/lookup.py books/cpp-guidelines "ownership"` |',
            '| Destructor rules | `python books/lookup.py books/cpp-guidelines "destructor"` |',
        ]
    if "cppreference" in book_ids:
        lines += [
            '| unique_ptr API | `python books/lookup.py books/cppreference --symbol unique_ptr` |',
        ]
    lines.append("")

    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"  Generated: {out_path.relative_to(repo_root)}")


# ---------------------------------------------------------------------------
# Update AGENTS.md between marker comments
# ---------------------------------------------------------------------------

AGENTS_KB_MARKER_BEGIN = "<!-- KB-BEGIN -->"
AGENTS_KB_MARKER_END   = "<!-- KB-END -->"


def update_agents_md(repo_root: Path, available_books: list[dict]) -> None:
    agents_path = repo_root / "AGENTS.md"
    if not agents_path.is_file():
        print(f"  WARNING: AGENTS.md not found at {agents_path} — skipping")
        return

    book_ids = {b["id"] for b in available_books}

    section_lines = [
        AGENTS_KB_MARKER_BEGIN,
        "",
        "## Knowledge Base Pre-Flight Checklist",
        "",
        "> **Run these queries before writing D3D12, RAII, or Win32 code.**",
        "> The knowledge base is indexed from official sources and is authoritative.",
        "",
        "### Available Books",
        "",
    ]
    for book in available_books:
        section_lines.append(f"- **{book['name']}**: {book.get('description', '')}")

    section_lines += [
        "",
        "### Mandatory Lookups by Task",
        "",
        "**Before writing D3D12 code:**",
        "```powershell",
    ]
    if "directx-specs" in book_ids:
        section_lines += [
            'python books/lookup.py books/directx-specs "descriptor heap"',
            'python books/lookup.py books/directx-specs "resource barrier"',
            'python books/lookup.py books/directx-specs "command list"',
        ]
    section_lines += [
        "```",
        "",
        "**Before writing RAII wrappers:**",
        "```powershell",
    ]
    if "cpp-guidelines" in book_ids:
        section_lines.append('python books/lookup.py books/cpp-guidelines "RAII"')
    if "cppreference" in book_ids:
        section_lines.append('python books/lookup.py books/cppreference --symbol unique_ptr')
    section_lines += [
        "```",
        "",
        "**Before writing Win32 windowing/input code:**",
        "```powershell",
    ]
    if "win32-api" in book_ids:
        section_lines += [
            'python books/lookup.py books/win32-api "message loop"',
            'python books/lookup.py books/win32-api --tag win32-windowing',
        ]
    section_lines += [
        "```",
        "",
        "### Tag Vocabulary (for --tag queries)",
        "",
        "| Tag | Meaning |",
        "|-----|---------|",
        "| `descriptor-heap` | D3D12 descriptor heap creation and management |",
        "| `resource-barrier` | D3D12 resource state transitions |",
        "| `command-list` | D3D12 command list recording and submission |",
        "| `root-signature` | D3D12 root signature design |",
        "| `RAII` | Resource Acquisition Is Initialization patterns |",
        "| `smart-pointer` | unique_ptr, shared_ptr, weak_ptr |",
        "| `win32-windowing` | Win32 window creation, message loop |",
        "| `COM` | COM IUnknown, QueryInterface, ComPtr |",
        "| `synchronization` | Fence, mutex, event, wait |",
        "| `swap-chain` | DXGI swap chain setup and present |",
        "",
        AGENTS_KB_MARKER_END,
    ]

    section_text = "\n".join(section_lines)
    existing = agents_path.read_text(encoding="utf-8")

    begin_pos = existing.find(AGENTS_KB_MARKER_BEGIN)
    end_pos   = existing.find(AGENTS_KB_MARKER_END)

    if begin_pos != -1 and end_pos != -1:
        new_content = existing[:begin_pos] + section_text + existing[end_pos + len(AGENTS_KB_MARKER_END):]
    else:
        new_content = existing.rstrip() + "\n\n" + section_text + "\n"

    agents_path.write_text(new_content, encoding="utf-8")
    print(f"  Updated : {agents_path.relative_to(repo_root)}")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(
        description="Generate VS Code instruction files and AGENTS.md KB section.",
    )
    parser.add_argument("--repo-root", default=None, help="Repository root path")
    args = parser.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    repo_root = find_repo_root(args.repo_root)
    print(f"Repo root : {repo_root.resolve()}")

    manifest = load_manifest(repo_root)
    all_books = manifest.get("books", [])

    available: list[dict] = []
    for book in all_books:
        book_dir = book.get("book_dir", f"books/{book['id']}")
        if db_exists(repo_root, book_dir):
            print(f"  [OK] {book['id']:20s} — {repo_root / book_dir / 'index.db'}")
            available.append(book)
        else:
            print(f"  [--] {book['id']:20s} — not indexed yet, skipping")

    if not available:
        print("\nNo books indexed yet — run bootstrap.py first.")
        return

    print(f"\nGenerating files for {len(available)} book(s) ...")
    generate_instruction_file(repo_root, available)
    update_agents_md(repo_root, available)
    print("\nDone. Run again whenever a new book is indexed.")


if __name__ == "__main__":
    main()
