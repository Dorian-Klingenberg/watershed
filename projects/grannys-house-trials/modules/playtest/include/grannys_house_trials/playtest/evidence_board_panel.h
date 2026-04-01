#pragma once

#include "grannys_house_trials/playtest/evidence_board_view.h"

#include <windows.h>

#include <string>

namespace grannys_house_trials::playtest
{
class EvidenceBoardPanel
{
public:
    EvidenceBoardPanel() = default;

    void create(HWND parent, HINSTANCE instance);
    void layout(int x, int y, int width, int height) const;
    void set_view(const EvidenceBoardView &view);
    [[nodiscard]] HWND hwnd() const noexcept;

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void update_text();
    [[nodiscard]] std::wstring format_view() const;

    HWND hwnd_ = nullptr;
    HWND text_ = nullptr;
    HFONT font_ = nullptr;
    EvidenceBoardView view_{};
};
} // namespace grannys_house_trials::playtest
