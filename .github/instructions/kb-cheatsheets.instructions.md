---
description: "Knowledge base query guide — automatically injected for all C++ files."
applyTo: "**/*.cpp"
---

# Knowledge Base — C++ / DirectX 12 / Win32 Cheatsheet

> **Before writing any D3D12, RAII, or Win32 code, query the local knowledge base.**
> Run commands from the repository root (`D:\Repos\watershed`).

## Available Books

- **C++ Reference (cppreference.com)** → `projects/grannys-house-trials/books/cppreference`
- **DirectX 12 Engineering Specs (microsoft/DirectX-Specs)** → `projects/grannys-house-trials/books/directx-specs`
- **C++ Core Guidelines (Stroustrup & Sutter)** → `projects/grannys-house-trials/books/cpp-guidelines`
- **Win32 API Reference (MicrosoftDocs/win32)** → `projects/grannys-house-trials/books/win32-api`

## Quick Reference — Lookup Commands

```powershell
# --- D3D12 patterns ---
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "descriptor heap"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs --tag descriptor-heap
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "command list recording"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "resource barrier"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "root signature"

# --- RAII / Modern C++ patterns ---
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cpp-guidelines "RAII"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cpp-guidelines --tag RAII
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cpp-guidelines "ownership lifetime"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cpp-guidelines "unique_ptr"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cppreference --symbol unique_ptr
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cppreference --symbol span
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cppreference --since 23

# --- Win32 API ---
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/win32-api "message loop"
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/win32-api --tag win32-windowing
python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/win32-api "COM IUnknown"
```

## Usage Notes

- Add `| Select-Object -First 10` to truncate long output.
- `--tag TAG` filters by auto-tagged topic (fast, no FTS).
- FTS supports: `'descriptor heap'`, `'RAII OR ownership'`, `'unique_ptr -shared_ptr'`

## D3D12 Patterns to Query Before Coding

| Task | Command |
|------|---------|
| Creating descriptor heap | `python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "descriptor heap creation"` |
| Resource barriers | `python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "resource barrier transition"` |
| Command list | `python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "command list"` |
| Root signature | `python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "root signature"` |
| Swap chain | `python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/directx-specs "swap chain"` |

## RAII Patterns to Query Before Coding

| Task | Command |
|------|---------|
| RAII wrapper design | `python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cpp-guidelines "RAII"` |
| Ownership transfer | `python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cpp-guidelines "ownership"` |
| Destructor rules | `python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cpp-guidelines "destructor"` |
| unique_ptr API | `python projects/grannys-house-trials/books/lookup.py projects/grannys-house-trials/books/cppreference --symbol unique_ptr` |
