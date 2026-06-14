#pragma once

// shader_utils.h — shared helpers for renderer classes
//
// Provides three small utilities used by RaycastRenderer and WireframeRenderer
// (and any future renderer that loads compiled .cso shader blobs):
//
//   exe_dir()          — wide-string path of the directory containing the
//                        running executable, with trailing backslash.
//   to_utf8()          — narrow a std::wstring to UTF-8.
//   load_shader_blob() — read a compiled .cso file into a byte vector.
//
// All functions are inline so they can live in a header without ODR problems.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace grannys_house_trials::gfx {

// Returns the directory of the running .exe with a trailing backslash.
// Example: L"C:\\projects\\build\\Debug\\"
// Used to locate compiled shader .cso blobs at runtime, regardless of CWD.
inline std::wstring exe_dir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    std::wstring s{ path };
    const auto pos = s.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? s.substr(0, pos + 1) : L"";
}

// Converts a wide string to a UTF-8 narrow string.
// WideCharToMultiByte is the correct Windows API; wcstombs would use the
// system locale and fail on paths with non-ASCII characters.
inline std::string to_utf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    // Compute required buffer size first (cbMultiByte = 0 → return size only).
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
        s.data(), required, nullptr, nullptr);
    // WideCharToMultiByte includes the null terminator in the character count.
    if (!s.empty() && s.back() == '\0')
        s.pop_back();
    return s;
}

// Reads a compiled shader object (.cso) file into a byte vector.
// Throws std::runtime_error if the file cannot be opened.
// D3D12 PSO creation accepts a {pointer, size} pair — pass .data() / .size().
inline std::vector<uint8_t> load_shader_blob(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open shader blob: " + path);

    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<uint8_t> blob(size);
    file.read(reinterpret_cast<char*>(blob.data()),
              static_cast<std::streamsize>(size));
    return blob;
}

} // namespace grannys_house_trials::gfx
