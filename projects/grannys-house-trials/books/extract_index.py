"""
extract_index.py — Index a PDF using Docling into Markdown chapters, PNG figures,
                   and a SQLite database with full-text search.

Usage:
    python books/extract_index.py <pdf_path> <book_dir>

Example:
    python books/extract_index.py "Strang.pdf" books/mit-18.06

Outputs (inside <book_dir>/):
    text/chNN.md        — Docling Markdown per chapter
    figures/chNN_*.png  — extracted figure images
    index.db            — SQLite with sections (FTS5), figures, book_tables

If <book_dir>/curriculum.yaml exists, its top-level chapter 'pages' ranges are used
as authoritative chapter boundaries.

Requires:
    pip install docling pyyaml

Note: First run downloads Docling layout models (~500 MB).
      If HuggingFace is blocked on your network, run first:
          set HF_ENDPOINT=https://hf-mirror.com   (Windows)
          export HF_ENDPOINT=https://hf-mirror.com (macOS/Linux)
"""

import csv
import hashlib
import io
import os
import re
import shutil
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import yaml
except ImportError:
    print("ERROR: pyyaml not installed. Run: pip install pyyaml")
    sys.exit(1)

try:
    from docling.document_converter import DocumentConverter, PdfFormatOption
    from docling.datamodel.base_models import InputFormat
    from docling.datamodel.pipeline_options import PdfPipelineOptions
except ImportError:
    print("ERROR: docling not installed. Run: pip install docling")
    sys.exit(1)


# ---------------------------------------------------------------------------
# DB schema
# ---------------------------------------------------------------------------

SCHEMA = """
CREATE TABLE sections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter     INTEGER NOT NULL,
    section_id  TEXT,
    title       TEXT NOT NULL,
    page_start  INTEGER,
    page_end    INTEGER,
    text        TEXT
);
CREATE INDEX idx_ch   ON sections(chapter);
CREATE INDEX idx_sid  ON sections(section_id);
CREATE INDEX idx_page ON sections(page_start, page_end);

CREATE VIRTUAL TABLE sections_fts USING fts5(
    section_id, title, text,
    content='sections', content_rowid='id'
);

CREATE TABLE figures (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter     INTEGER NOT NULL,
    page        INTEGER,
    label       TEXT,
    caption     TEXT,
    image_path  TEXT
);

CREATE TABLE book_tables (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter     INTEGER NOT NULL,
    page        INTEGER,
    caption     TEXT,
    content_csv TEXT
);

CREATE TABLE sources (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path   TEXT UNIQUE NOT NULL,
    sha256      TEXT NOT NULL,
    indexed_at  TEXT NOT NULL
);
"""


# ---------------------------------------------------------------------------
# Hashing
# ---------------------------------------------------------------------------

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_converter() -> DocumentConverter:
    opts = PdfPipelineOptions()
    opts.do_ocr = False
    opts.do_table_structure = True
    opts.generate_picture_images = True
    return DocumentConverter(
        format_options={InputFormat.PDF: PdfFormatOption(pipeline_options=opts)}
    )


def load_curriculum(book_dir: Path):
    p = book_dir / "curriculum.yaml"
    if not p.exists():
        return None
    with open(p, encoding="utf-8") as f:
        return yaml.safe_load(f)


def page_range_from_chapter(ch: dict) -> tuple[int, int] | None:
    """Return (start, end) page numbers for a curriculum chapter."""
    pages = ch.get("pages")
    if pages and len(pages) == 2:
        return (int(pages[0]), int(pages[1]))
    # Fall back to scanning sections
    all_pages = []
    for sec in ch.get("sections", []):
        sp = sec.get("pages", [])
        if sp:
            all_pages.extend(sp)
    if all_pages:
        return (min(all_pages), max(all_pages))
    return None


def get_caption(item, doc) -> str:
    try:
        text = item.caption_text(doc)
        if text:
            return text.strip()
    except Exception:
        pass
    if hasattr(item, "captions") and item.captions:
        return str(item.captions[0])
    return ""


def table_to_csv(tbl) -> str:
    if not hasattr(tbl, "data") or not tbl.data:
        return ""
    grid = getattr(tbl.data, "grid", None)
    if not grid:
        return ""
    buf = io.StringIO()
    w = csv.writer(buf)
    for row in grid:
        w.writerow([cell.text for cell in row])
    return buf.getvalue()


def item_page(item, fallback: int) -> int:
    if item.prov:
        return item.prov[0].page_no
    return fallback


# ---------------------------------------------------------------------------
# Chapter processing
# ---------------------------------------------------------------------------

SECTION_ID_RE = re.compile(r"^(\d+\.\d+)\b")


def process_chapter(
    converter: DocumentConverter,
    pdf_path: Path,
    chapter_num: int,
    page_start: int,
    page_end: int,
    text_dir: Path,
    figures_dir: Path,
) -> tuple[list, list, list]:
    """
    Convert one chapter's pages with Docling.
    Returns (sections, figures, tables) as lists of dicts ready for DB insertion.
    """
    print(f"  Ch{chapter_num:02d}: pages {page_start}–{page_end} ...", end=" ", flush=True)

    result = converter.convert(str(pdf_path), page_range=(page_start, page_end))
    doc = result.document

    # --- Markdown export ---
    md = doc.export_to_markdown()
    (text_dir / f"ch{chapter_num:02d}.md").write_text(md, encoding="utf-8")

    # --- Sections (from text items) ---
    # Sort by page number; trust Docling for within-page order
    ordered = sorted(doc.texts, key=lambda x: item_page(x, page_start))

    sections: list[dict] = []
    cur: dict = {
        "chapter": chapter_num,
        "section_id": None,
        "title": f"Chapter {chapter_num}",
        "page_start": page_start,
        "page_end": page_end,
        "parts": [],
    }

    for item in ordered:
        label = str(item.label)
        text = item.text.strip() if item.text else ""
        page = item_page(item, page_start)

        if "section_header" in label and text and len(text) >= 3:
            # Flush current section
            if cur["parts"] or sections:
                cur["page_end"] = page - 1
                sections.append({**cur, "text": "\n\n".join(cur["parts"])})
            # Start new section
            sid_m = SECTION_ID_RE.match(text)
            cur = {
                "chapter": chapter_num,
                "section_id": sid_m.group(1) if sid_m else None,
                "title": text,
                "page_start": page,
                "page_end": page_end,
                "parts": [],
            }
        elif any(lbl in label for lbl in ("text", "list_item", "formula", "caption", "footnote")):
            if text:
                cur["parts"].append(text)

    # Flush last section
    if cur["parts"] or not sections:
        sections.append({**cur, "text": "\n\n".join(cur["parts"])})

    # Remove helper key
    for s in sections:
        s.pop("parts", None)

    # --- Figures ---
    figures: list[dict] = []
    for i, pic in enumerate(doc.pictures):
        page = item_page(pic, page_start)
        cap = get_caption(pic, doc)
        label = str(pic.label)
        image_path = None

        try:
            if pic.image is not None and hasattr(pic.image, "pil_image") and pic.image.pil_image is not None:
                fname = f"ch{chapter_num:02d}_fig{i+1:02d}_p{page}.png"
                out = figures_dir / fname
                pic.image.pil_image.save(str(out))
                image_path = f"figures/{fname}"
        except Exception as exc:
            print(f"\n    [warn] could not save figure {i+1}: {exc}", end=" ")

        figures.append({
            "chapter": chapter_num,
            "page": page,
            "label": label,
            "caption": cap,
            "image_path": image_path,
        })

    # --- Tables ---
    tables: list[dict] = []
    for tbl in doc.tables:
        page = item_page(tbl, page_start)
        cap = get_caption(tbl, doc)
        tables.append({
            "chapter": chapter_num,
            "page": page,
            "caption": cap,
            "content_csv": table_to_csv(tbl),
        })

    print(f"ok  ({len(sections)} sections, {len(figures)} figs, {len(tables)} tables)")
    return sections, figures, tables


# ---------------------------------------------------------------------------
# DB write
# ---------------------------------------------------------------------------

def build_db(db_path: Path, sections: list, figures: list, tables: list,
             pdf_path: str = "", pdf_hash: str = "") -> None:
    if db_path.exists():
        db_path.unlink()
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    c.executescript(SCHEMA)

    c.executemany(
        "INSERT INTO sections (chapter, section_id, title, page_start, page_end, text) "
        "VALUES (:chapter, :section_id, :title, :page_start, :page_end, :text)",
        sections,
    )
    conn.commit()

    c.execute("INSERT INTO sections_fts(sections_fts) VALUES('rebuild')")
    conn.commit()

    c.executemany(
        "INSERT INTO figures (chapter, page, label, caption, image_path) "
        "VALUES (:chapter, :page, :label, :caption, :image_path)",
        figures,
    )
    c.executemany(
        "INSERT INTO book_tables (chapter, page, caption, content_csv) "
        "VALUES (:chapter, :page, :caption, :content_csv)",
        tables,
    )

    if pdf_hash and pdf_path:
        now = datetime.now(timezone.utc).isoformat()
        c.execute(
            "INSERT INTO sources (file_path, sha256, indexed_at) VALUES (?, ?, ?) "
            "ON CONFLICT(file_path) DO UPDATE SET sha256=excluded.sha256, indexed_at=excluded.indexed_at",
            (pdf_path, pdf_hash, now),
        )

    conn.commit()
    conn.close()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    force = "--force" in sys.argv
    clean_argv = [a for a in sys.argv[1:] if a != "--force"]
    if len(clean_argv) != 2:
        print(__doc__)
        sys.exit(1)

    pdf_path = Path(clean_argv[0])
    book_dir = Path(clean_argv[1])

    if not pdf_path.exists():
        print(f"ERROR: PDF not found: {pdf_path}")
        sys.exit(1)

    book_dir.mkdir(parents=True, exist_ok=True)
    text_dir = book_dir / "text"
    figures_dir = book_dir / "figures"
    db_path = book_dir / "index.db"

    # --- Incremental hash check ---
    pdf_hash = sha256_file(pdf_path)
    if not force and db_path.exists():
        try:
            _conn = sqlite3.connect(db_path)
            row = _conn.execute(
                "SELECT sha256 FROM sources WHERE file_path = ?", (str(pdf_path),)
            ).fetchone()
            _conn.close()
            if row and row[0] == pdf_hash:
                print(f"Already indexed with same content hash.  Use --force to reindex.")
                return
        except Exception:
            pass  # sources table missing or DB corrupt — proceed with full reindex

    # Clean previous outputs — use onerror to handle Windows read-only files
    import stat
    def _rm_readonly(func, path, _):
        os.chmod(path, stat.S_IWRITE)
        func(path)

    for d in (text_dir, figures_dir):
        if d.exists():
            shutil.rmtree(d, onerror=_rm_readonly)
        d.mkdir()

    curriculum = load_curriculum(book_dir)

    print("Initialising Docling converter (downloads models on first run) ...")
    converter = make_converter()

    # Get actual PDF page count to skip out-of-range chapters
    import pypdfium2 as pdfium
    _pdf = pdfium.PdfDocument(str(pdf_path))
    total_pages = len(_pdf)
    _pdf.close()
    print(f"PDF page count: {total_pages}")

    all_sections: list[dict] = []
    all_figures: list[dict] = []
    all_tables: list[dict] = []

    if curriculum and "chapters" in curriculum:
        chapters = curriculum["chapters"]
        print(f"Using curriculum.yaml — {len(chapters)} chapters")
        for ch in chapters:
            rng = page_range_from_chapter(ch)
            if rng is None:
                print(f"  Ch{ch['number']:02d}: no page range — skipping")
                continue
            p_start, p_end = rng
            if p_start > total_pages:
                print(f"  Ch{ch['number']:02d}: pages {p_start}–{p_end} beyond PDF ({total_pages} pages) — skipping")
                continue
            p_end = min(p_end, total_pages)
            try:
                s, f, t = process_chapter(
                    converter, pdf_path, ch["number"], p_start, p_end,
                    text_dir, figures_dir,
                )
            except Exception as exc:
                print(f"\n  Ch{ch['number']:02d}: ERROR — {exc} — skipping")
                continue
            all_sections.extend(s)
            all_figures.extend(f)
            all_tables.extend(t)
    else:
        # No curriculum: convert entire PDF as a single block
        print("No curriculum.yaml — converting entire PDF as one unit ...")
        s, f, t = process_chapter(
            converter, pdf_path, 1, 1, 999999, text_dir, figures_dir,
        )
        all_sections.extend(s)
        all_figures.extend(f)
        all_tables.extend(t)

    print(f"\nWriting index.db ...")
    build_db(db_path, all_sections, all_figures, all_tables,
             pdf_path=str(pdf_path), pdf_hash=pdf_hash)

    print(f"\n{'='*50}")
    print(f"Sections : {len(all_sections)}")
    print(f"Figures  : {len(all_figures)}")
    print(f"Tables   : {len(all_tables)}")
    print(f"Index    : {db_path}")
    print(f"\nQuery with:")
    print(f'  python books/lookup.py "{book_dir}" "dot product"')
    print(f'  python books/lookup.py "{book_dir}" --chapter 1')
    print(f'  python books/lookup.py "{book_dir}" --figures')


if __name__ == "__main__":
    main()
