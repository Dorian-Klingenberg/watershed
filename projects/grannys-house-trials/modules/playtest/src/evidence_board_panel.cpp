#include "grannys_house_trials/playtest/evidence_board_panel.h"

#include "grannys_house_trials/sim/evidence_type.h"

#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <string_view>

namespace grannys_house_trials::playtest
{
namespace
{
constexpr wchar_t evidence_board_panel_class_name[] = L"GrannysHouseTrialsEvidenceBoardPanel";

[[nodiscard]] std::wstring widen(std::string_view text)
{
    return std::wstring(text.begin(), text.end());
}
} // namespace

void EvidenceBoardPanel::create(HWND parent, HINSTANCE instance)
{
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &EvidenceBoardPanel::window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = evidence_board_panel_class_name;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&window_class);

    hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        evidence_board_panel_class_name,
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        0,
        parent,
        nullptr,
        instance,
        this);

    if (!hwnd_)
    {
        throw std::runtime_error("Could not create the evidence board panel.");
    }

    text_ = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance,
        nullptr);

    if (!text_)
    {
        throw std::runtime_error("Could not create the evidence board text view.");
    }

    font_ = CreateFontW(
        -16,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN,
        L"Consolas");

    if (font_)
    {
        SendMessage(text_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    update_text();
}

void EvidenceBoardPanel::layout(int x, int y, int width, int height) const
{
    if (!hwnd_ || !text_)
    {
        return;
    }

    MoveWindow(hwnd_, x, y, width, height, TRUE);
    const int padding = 6;
    const int inner_width = width > padding * 2 ? width - padding * 2 : 0;
    const int inner_height = height > padding * 2 ? height - padding * 2 : 0;
    MoveWindow(text_, padding, padding, inner_width, inner_height, TRUE);
}

void EvidenceBoardPanel::set_view(const EvidenceBoardView &view)
{
    view_ = view;
    update_text();
}

HWND EvidenceBoardPanel::hwnd() const noexcept
{
    return hwnd_;
}

LRESULT CALLBACK EvidenceBoardPanel::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    EvidenceBoardPanel *self = reinterpret_cast<EvidenceBoardPanel *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_NCCREATE:
    {
        const auto *create_struct = reinterpret_cast<CREATESTRUCTW *>(lparam);
        self = reinterpret_cast<EvidenceBoardPanel *>(create_struct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    case WM_SIZE:
        if (self && self->text_)
        {
            const int width = LOWORD(lparam);
            const int height = HIWORD(lparam);
            const int padding = 6;
            const int inner_width = width > padding * 2 ? width - padding * 2 : 0;
            const int inner_height = height > padding * 2 ? height - padding * 2 : 0;
            MoveWindow(
                self->text_,
                padding,
                padding,
                inner_width,
                inner_height,
                TRUE);
        }
        return 0;
    case WM_DESTROY:
        if (self && self->font_)
        {
            DeleteObject(self->font_);
            self->font_ = nullptr;
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

void EvidenceBoardPanel::update_text()
{
    if (!text_)
    {
        return;
    }

    SetWindowTextW(text_, format_view().c_str());
}

std::wstring EvidenceBoardPanel::format_view() const
{
    std::wostringstream text;
    text << L"Evidence Board\r\n";

    if (view_.stats.empty())
    {
        text << L"  - no evidence recorded yet\r\n";
    }
    else
    {
        for (const auto &stat : view_.stats)
        {
            text << L"  - " << widen(grannys_house_trials::sim::to_string(stat.type)) << L": " << stat.count << L"\r\n";
        }
    }

    text << L"\r\nRecent notes\r\n";
    if (view_.highlights.empty())
    {
        text << L"  - none yet\r\n";
    }
    else
    {
        const std::size_t start = view_.highlights.size() > 6 ? view_.highlights.size() - 6 : 0;
        for (std::size_t index = start; index < view_.highlights.size(); ++index)
        {
            text << L"  - " << widen(view_.highlights[index]) << L"\r\n";
        }
    }

    return text.str();
}
} // namespace grannys_house_trials::playtest
