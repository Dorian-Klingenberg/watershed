"""
bootstrap.py — One-command project onboarding: download, index, and configure all books.

Usage:
    python books/bootstrap.py [--repo-root PATH] [--skip-download] [--force] [--dry-run]

What it does (in order):
    1. Reads books/manifest.json to find all books and their sources
    2. Downloads / clones each source (git clone --depth=1 for Markdown repos,
       Invoke-WebRequest for single HTML files)
    3. Runs the appropriate extractor for each book format
    4. Runs configure.py to generate .github/instructions/ and update AGENTS.md

Options:
    --repo-root PATH   Root of the repository (auto-detected if omitted)
    --skip-download    Skip downloads — use existing files in books/downloads/
    --force            Re-index all files even if hash unchanged
    --dry-run          Print what would be done without doing it

Formats supported:
    html-archive   Handled by extract_html.py (cppreference)
    markdown-repo  Handled by extract_markdown.py (DirectX-Specs, Win32)
    html-single    Handled by extract_html_page.py (C++ Core Guidelines)
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path
from urllib.request import urlretrieve


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def find_repo_root(hint: str | None = None) -> Path:
    if hint:
        p = Path(hint)
        if p.is_dir():
            return p
    cwd = Path.cwd()
    for _ in range(6):
        if (cwd / "AGENTS.md").exists() or (cwd / "CMakeLists.txt").exists():
            return cwd
        parent = cwd.parent
        if parent == cwd:
            break
        cwd = parent
    return Path(".")


def run(cmd: list[str], *, check: bool = True, cwd: Path | None = None) -> int:
    print(f"  $ {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if check and result.returncode != 0:
        print(f"  ERROR: command failed (exit {result.returncode})")
        raise RuntimeError(f"Command failed: {' '.join(cmd)}")
    return result.returncode


def git_available() -> bool:
    try:
        result = subprocess.run(
            ["git", "--version"], capture_output=True, text=True
        )
        return result.returncode == 0
    except FileNotFoundError:
        return False


# ---------------------------------------------------------------------------
# Format handlers
# ---------------------------------------------------------------------------

def handle_html_archive(book: dict, repo_root: Path, downloads: Path, *,
                        force: bool, dry_run: bool) -> bool:
    """Download ZIP and index with extract_html.py."""
    book_id  = book["id"]
    source   = book["source"]
    book_dir = repo_root / book.get("book_dir", f"books/{book_id}")
    zip_path = downloads / f"{book_id}.zip"
    extract_dir = downloads / f"{book_id}-archive"

    if not zip_path.exists() or force:
        print(f"\n  [{book_id}] Downloading {source} ...")
        if not dry_run:
            urlretrieve(source, zip_path)
            print(f"  Downloaded → {zip_path}")
        else:
            print(f"  DRY-RUN: would download to {zip_path}")

    if not extract_dir.exists() or force:
        print(f"\n  [{book_id}] Extracting archive ...")
        if not dry_run:
            import zipfile
            with zipfile.ZipFile(zip_path, "r") as z:
                z.extractall(extract_dir)
        else:
            print(f"  DRY-RUN: would extract to {extract_dir}")

    extractor = repo_root / "books" / "extract_html.py"
    cmd = [sys.executable, str(extractor), str(extract_dir), str(book_dir)]
    if force:
        cmd.append("--force")

    print(f"\n  [{book_id}] Indexing ...")
    if not dry_run:
        run(cmd)
    else:
        print(f"  DRY-RUN: would run: {' '.join(cmd)}")
    return True


def handle_markdown_repo(book: dict, repo_root: Path, downloads: Path, *,
                         force: bool, dry_run: bool, skip_download: bool) -> bool:
    """Shallow-clone a GitHub repo and index with extract_markdown.py."""
    book_id    = book["id"]
    source     = book["source"]
    book_dir   = repo_root / book.get("book_dir", f"books/{book_id}")
    clone_dir  = downloads / book_id

    if not skip_download:
        if clone_dir.exists() and not force:
            print(f"\n  [{book_id}] Clone already exists at {clone_dir} — using it.")
        else:
            if not git_available():
                print(f"  ERROR: git not found — cannot clone {source}")
                return False
            if clone_dir.exists():
                # Pull latest instead of re-cloning
                print(f"\n  [{book_id}] Updating existing clone ...")
                if not dry_run:
                    run(["git", "-C", str(clone_dir), "pull", "--depth=1"], check=False)
                else:
                    print(f"  DRY-RUN: would git pull in {clone_dir}")
            else:
                print(f"\n  [{book_id}] Cloning {source} ...")
                if not dry_run:
                    run(["git", "clone", "--depth=1", source, str(clone_dir)])
                else:
                    print(f"  DRY-RUN: would clone to {clone_dir}")
    else:
        if not clone_dir.exists():
            print(f"  [{book_id}] SKIP-DOWNLOAD: {clone_dir} not found — cannot index")
            return False

    filters = book.get("filter", [])
    extractor = repo_root / "books" / "extract_markdown.py"
    cmd = [sys.executable, str(extractor), str(clone_dir), str(book_dir)]
    for f in filters:
        cmd += ["--filter", f]
    if force:
        cmd.append("--force")

    print(f"\n  [{book_id}] Indexing ...")
    if not dry_run:
        run(cmd)
    else:
        print(f"  DRY-RUN: would run: {' '.join(cmd)}")
    return True


def handle_html_single(book: dict, repo_root: Path, downloads: Path, *,
                       force: bool, dry_run: bool, skip_download: bool) -> bool:
    """Download a single HTML page and index with extract_html_page.py."""
    book_id  = book["id"]
    source   = book["source"]
    book_dir = repo_root / book.get("book_dir", f"books/{book_id}")
    html_path = downloads / f"{book_id}.html"

    if not skip_download:
        if html_path.exists() and not force:
            print(f"\n  [{book_id}] HTML already exists at {html_path} — using it.")
        else:
            print(f"\n  [{book_id}] Downloading {source} ...")
            if not dry_run:
                try:
                    urlretrieve(source, html_path)
                    print(f"  Downloaded → {html_path}  ({html_path.stat().st_size // 1024} KB)")
                except Exception as exc:
                    print(f"  ERROR: download failed: {exc}")
                    # Try with requests if urllib fails (some sites require proper UA)
                    try:
                        import urllib.request
                        req = urllib.request.Request(source, headers={"User-Agent": "Mozilla/5.0"})
                        with urllib.request.urlopen(req) as resp, open(html_path, "wb") as fh:
                            fh.write(resp.read())
                        print(f"  Downloaded (UA override) → {html_path}")
                    except Exception as exc2:
                        print(f"  ERROR: fallback also failed: {exc2}")
                        return False
            else:
                print(f"  DRY-RUN: would download to {html_path}")
    else:
        if not html_path.exists():
            print(f"  [{book_id}] SKIP-DOWNLOAD: {html_path} not found — cannot index")
            return False

    extractor = repo_root / "books" / "extract_html_page.py"
    cmd = [sys.executable, str(extractor), str(html_path), str(book_dir)]
    if force:
        cmd.append("--force")

    # Check if bs4 is available
    try:
        import importlib
        importlib.import_module("bs4")
    except ImportError:
        print(f"\n  [{book_id}] beautifulsoup4 not found — installing ...")
        if not dry_run:
            run([sys.executable, "-m", "pip", "install", "beautifulsoup4", "lxml", "--quiet"], check=False)

    print(f"\n  [{book_id}] Indexing ...")
    if not dry_run:
        run(cmd)
    else:
        print(f"  DRY-RUN: would run: {' '.join(cmd)}")
    return True


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="One-command knowledge base setup: download, index, and configure.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--repo-root",     default=None, help="Repository root path")
    parser.add_argument("--skip-download", action="store_true",
                        help="Skip downloads — use existing files in books/downloads/")
    parser.add_argument("--force",         action="store_true",
                        help="Re-download and re-index all books")
    parser.add_argument("--dry-run",       action="store_true",
                        help="Print what would be done without doing it")
    parser.add_argument("--books",         nargs="*",
                        help="Limit to specific book IDs (default: all)")
    args = parser.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    repo_root = find_repo_root(args.repo_root).resolve()
    print(f"Repo root  : {repo_root}")

    manifest_path = repo_root / "books" / "manifest.json"
    if not manifest_path.is_file():
        print(f"ERROR: manifest.json not found at {manifest_path}")
        sys.exit(1)

    with open(manifest_path, encoding="utf-8") as fh:
        manifest = json.load(fh)

    all_books = manifest.get("books", [])
    if args.books:
        all_books = [b for b in all_books if b["id"] in args.books]

    downloads = repo_root / "books" / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)

    print(f"Downloads  : {downloads}")
    print(f"Books      : {[b['id'] for b in all_books]}")
    if args.dry_run:
        print("DRY-RUN    : no changes will be made")

    results: dict[str, bool] = {}
    for book in all_books:
        book_id = book["id"]
        fmt     = book.get("format", "")
        print(f"\n{'='*60}")
        print(f"Book: {book['name']}")
        print(f"{'='*60}")
        try:
            if fmt == "html-archive":
                ok = handle_html_archive(
                    book, repo_root, downloads, force=args.force, dry_run=args.dry_run
                )
            elif fmt == "markdown-repo":
                ok = handle_markdown_repo(
                    book, repo_root, downloads, force=args.force,
                    dry_run=args.dry_run, skip_download=args.skip_download
                )
            elif fmt == "html-single":
                ok = handle_html_single(
                    book, repo_root, downloads, force=args.force,
                    dry_run=args.dry_run, skip_download=args.skip_download
                )
            else:
                print(f"  ERROR: unknown format '{fmt}'")
                ok = False
            results[book_id] = ok
        except Exception as exc:
            print(f"  EXCEPTION for {book_id}: {exc}")
            results[book_id] = False

    # Run configure.py
    print(f"\n{'='*60}")
    print("Generating instruction files ...")
    configure = repo_root / "books" / "configure.py"
    if not args.dry_run:
        run([sys.executable, str(configure), "--repo-root", str(repo_root)], check=False)
    else:
        print(f"  DRY-RUN: would run configure.py")

    # Summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    for book_id, ok in results.items():
        status = "OK " if ok else "FAIL"
        print(f"  [{status}] {book_id}")
    total_ok = sum(1 for ok in results.values() if ok)
    print(f"\n{total_ok}/{len(results)} books indexed successfully.")
    if total_ok < len(results):
        print("Some books failed — check output above for details.")
        sys.exit(1)


if __name__ == "__main__":
    main()
