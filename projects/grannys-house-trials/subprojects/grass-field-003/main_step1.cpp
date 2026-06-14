// grass-field-003 — Step 1: Bare Win32 Window

#define WIN32_LEAN_AND_MEAN

#define NOMINMAX

#include <Windows.h>

#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

constexpr wchar_t k_window_class_name[] = L"GrassField003Window";
constexpr wchar_t k_window_title[]      = L"Grass Field 003 — Step 1";

// ─────────────────────────────────────────────────────────────────────────────
// Window procedure
// ─────────────────────────────────────────────────────────────────────────────
//
LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        return 0;

    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────
//
struct Application
{
    HWND hwnd = nullptr;

    void create_window()
    {
        WNDCLASSEXW wnd_class  = {};
        wnd_class.cbSize        = sizeof(WNDCLASSEXW);
        wnd_class.style         = CS_HREDRAW | CS_VREDRAW;
        wnd_class.lpfnWndProc   = window_proc;
        wnd_class.hInstance     = GetModuleHandleW(nullptr);
        wnd_class.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wnd_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        wnd_class.lpszClassName = k_window_class_name;

        if (!RegisterClassExW(&wnd_class))
            throw std::runtime_error("RegisterClassExW failed.");

        hwnd = CreateWindowExW(
            0,
            k_window_class_name,
            k_window_title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            1280, 720,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr
        );

        if (!hwnd)
            throw std::runtime_error("CreateWindowExW failed.");

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }

    void run()
    {
        MSG  msg     = {};
        bool running = true;

        while (running)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    running = false;
                    break;
                }

                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (!running)
                break;

        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────
//
int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                    PWSTR /*pCmdLine*/, int /*nCmdShow*/)
{
    try
    {
        Application app;
        app.create_window();
        app.run();
        return 0;
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Grass Field 003 — Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
