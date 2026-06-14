# Journal Entry — 2026-05-10

## Session Summary

**Date:** May 10, 2026  
**Project:** Granny's House Trials — Knowledge Base & Grass-Field-003 Planning  
**Agent:** GitHub Copilot (Claude Sonnet 4.6)

---

## What Is Granny's House Trials?

Before diving into today's work, some context on what this project actually is —
because it shapes why the tooling we built today matters.

**Granny's House Trials** is not the full game. It is a vertical-slice
proof-of-concept that lives inside a larger ancient-infrastructure game world.
The core question it is trying to answer is:

> Can a tiny, cozy, deterministic yard scenario — with hidden infrastructure,
> real consequences, and a round-based format — serve both as a genuine
> development tool and as an entertaining competition where recurring tester
> personalities reveal actual system truth?

The setting is Granny's yard: a small homestead corner with garden beds, a
footpath, a cellar edge at risk of flooding, and a buried drain channel that
nobody told the testers about. The first mechanic is water routing — get water
to the garden beds without flooding the cellar or damaging the path. Simple to
observe, genuinely tricky to solve without finding the hidden dependency.

The competition is judged by a human host at the end of each round. The system
records what happened; the host decides what it meant and how many points it
deserved. That distinction is load-bearing: humor, bias, theatrical judgment,
and character all live in the gap between the factual evidence board and the
host's ruling.

Technically, it is a D3D12 app with a shared sim/playtest/gfx module
architecture, shader-side column raycast rendering, and a session-owned round
state. Milestones 1–4 are complete. The cast layer (Milestone 5) is partial.
The host-judged competition loop (Milestone 6) has not started.

The third Granny's House subproject — **Grass-Field-003** — is the planned
learning track: a step-by-step rebuild of the terrain/rendering and erosion
simulation work, this time with deliberate attention to C++23 standards,
proper RAII architecture, and the new UI infrastructure from the second
subproject. Each step gets its own source file; previous steps are never
altered.

---

## What We Did Today

### 1. Completed the Knowledge Base Framework

The session continued work begun in prior sessions: building a portable,
SQLite-backed knowledge base to give all agents — including faster mini models
— reliable, offline access to the documentation they need for C++23, DirectX 12,
and Win32 development.

The framework is now fully built and consists of five Python scripts:

| Script | Purpose |
|--------|---------|
| `books/manifest.json` | Central registry of all books and their source URLs |
| `books/extract_markdown.py` | Index a cloned GitHub Markdown repository into SQLite |
| `books/extract_html_page.py` | Index a single large HTML reference page into SQLite |
| `books/configure.py` | Read all indexed books and generate VS Code instruction files + AGENTS.md section |
| `books/bootstrap.py` | One command: download → index → configure everything |

`lookup.py` was also updated with `--tag TAG` support, enabling fast topic
filtering without FTS overhead.

The tag vocabulary covers the most important concepts in this project:
`descriptor-heap`, `resource-barrier`, `command-list`, `root-signature`,
`RAII`, `smart-pointer`, `COM`, `win32-windowing`, `synchronization`, and more.

### 2. Indexed Four Books

Three sub-agents ran in parallel to clone and index three new books:

| Book | Source | Sections | DB Size |
|------|--------|----------|---------|
| C++ Reference (cppreference.com) | HTML archive | 5,706 symbols | 32 MB |
| DirectX 12 Engineering Specs | github.com/microsoft/DirectX-Specs | 88 files | 2.6 MB |
| C++ Core Guidelines | isocpp.github.io (single HTML page) | **589 sections** | 1.6 MB |
| Win32 API Reference | github.com/MicrosoftDocs/win32 | 126 files | 372 KB |

The C++ Core Guidelines parser required a bug fix mid-session: the original
implementation iterated `content.children` (direct children only), which
yielded a single section instead of 600+. The fix uses `find_all(['h1','h2','h3'])`
to enumerate all headings document-wide, regardless of nesting depth. This is
now documented in the learnings section of the knowledge base instruction file.

### 3. Auto-Generated Configuration

After indexing, `configure.py` was run to produce:

- `.github/instructions/kb-cheatsheets.instructions.md`
  — `applyTo: "**/*.cpp"` means VS Code forces it into every Copilot C++ request
  automatically. No agent needs to remember to load it.

- `AGENTS.md` — updated with a `<!-- KB-BEGIN -->` / `<!-- KB-END -->` block
  containing mandatory pre-flight lookup commands for D3D12 code, RAII wrappers,
  and Win32 windowing.

### 4. Cleaned Up ~780 MB of Source Downloads

Once the indexes were built, all source download artifacts were deleted. The
indexes are what matter; the clones and HTML files are reproducible on demand
via `bootstrap.py`.

| Deleted | Size |
|---------|------|
| `books/downloads/win32/` (56,000+ files) | 417 MB |
| `books/cppreference-archive/` | 337 MB |
| `books/downloads/directx-specs/` | 25 MB |
| `books/downloads/cpp-guidelines.html` | 1.2 MB |

The cleanup step and its commands were added to
`.github/instructions/knowledge-base.instructions.md` so future agents know
to do this automatically after indexing.

---

## Why This Matters for Grass-Field-003

The knowledge base exists to make Grass-Field-003 work better. The planned
next subproject is a deliberate, step-by-step learning project:

- **Step 1:** Erosion simulation with the new UI system
- Each step gets its own source file — previous steps are never modified
- Every piece of D3D12 code written will be backed by authoritative specs
  pulled from `books/directx-specs` in seconds
- Every RAII wrapper will have the C++ Core Guidelines and cppreference on hand

The goal is clean, Uncle Bob-style, RAII-first, C++23 code — not because the
proof-of-concept demanded it, but because Grass-Field-003 is the track where
we learn to do it right.

---

## What to Do Next

1. **Start Grass-Field-003, Step 1** — erosion simulation with new UI, first
   source file, nothing carried forward except the architecture lessons.
2. **Run `configure.py`** whenever a new book is added —
   `python books/configure.py` is idempotent and safe.
3. **Use `bootstrap.py`** when onboarding to a new machine —
   `python books/bootstrap.py` handles everything end to end.
4. **Query the KB before writing D3D12 code** —
   `python books/lookup.py books/directx-specs "descriptor heap"` is the habit
   to build.

---

*Journal authored by GitHub Copilot (Claude Sonnet 4.6) at end of session.*
