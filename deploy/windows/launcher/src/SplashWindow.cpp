#include "SplashWindow.h"

#include <windows.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace qgc {

namespace {
constexpr UINT WM_SPLASH_STATUS   = WM_APP + 1; // lParam = new std::wstring*
constexpr UINT WM_SPLASH_PROGRESS = WM_APP + 2; // wParam = percent (0..100)
constexpr UINT WM_SPLASH_MARQUEE  = WM_APP + 3; // wParam = on/off
constexpr UINT WM_SPLASH_CLOSE    = WM_APP + 4;

constexpr int kWidth = 460;
constexpr int kHeight = 170;
} // namespace

struct SplashWindow::Impl {
    HANDLE thread = nullptr;
    DWORD  threadId = 0;
    HWND   hwnd = nullptr;
    HWND   labelTitle = nullptr;
    HWND   labelStatus = nullptr;
    HWND   progress = nullptr;
    HANDLE readyEvent = nullptr;
    std::wstring title;

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        Impl *self = reinterpret_cast<Impl *>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
        case WM_SPLASH_STATUS: {
            auto *text = reinterpret_cast<std::wstring *>(lParam);
            if (self && text) {
                ::SetWindowTextW(self->labelStatus, text->c_str());
            }
            delete text;
            return 0;
        }
        case WM_SPLASH_PROGRESS:
            if (self) {
                ::SendMessageW(self->progress, PBM_SETMARQUEE, FALSE, 0);
                LONG_PTR style = ::GetWindowLongPtrW(self->progress, GWL_STYLE);
                ::SetWindowLongPtrW(self->progress, GWL_STYLE, style & ~PBS_MARQUEE);
                ::SendMessageW(self->progress, PBM_SETRANGE32, 0, 100);
                ::SendMessageW(self->progress, PBM_SETPOS, static_cast<WPARAM>(wParam), 0);
            }
            return 0;
        case WM_SPLASH_MARQUEE:
            if (self) {
                LONG_PTR style = ::GetWindowLongPtrW(self->progress, GWL_STYLE);
                if (wParam) {
                    ::SetWindowLongPtrW(self->progress, GWL_STYLE, style | PBS_MARQUEE);
                    ::SendMessageW(self->progress, PBM_SETMARQUEE, TRUE, 30);
                } else {
                    ::SendMessageW(self->progress, PBM_SETMARQUEE, FALSE, 0);
                    ::SetWindowLongPtrW(self->progress, GWL_STYLE, style & ~PBS_MARQUEE);
                }
            }
            return 0;
        case WM_SPLASH_CLOSE:
            ::DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    void createWindow()
    {
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
        ::InitCommonControlsEx(&icc);

        HINSTANCE inst = ::GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Impl::wndProc;
        wc.hInstance = inst;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"QGCLauncherSplash";
        wc.hIcon = ::LoadIconW(inst, MAKEINTRESOURCEW(1)); // IDI_ICON1 from the .rc
        ::RegisterClassExW(&wc);

        const int sx = ::GetSystemMetrics(SM_CXSCREEN);
        const int sy = ::GetSystemMetrics(SM_CYSCREEN);
        hwnd = ::CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, title.c_str(),
                                 WS_CAPTION | WS_SYSMENU,
                                 (sx - kWidth) / 2, (sy - kHeight) / 2, kWidth, kHeight,
                                 nullptr, nullptr, inst, nullptr);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        labelTitle = ::CreateWindowExW(0, L"STATIC", L"QGroundControl",
                                       WS_CHILD | WS_VISIBLE, 20, 18, kWidth - 40, 24,
                                       hwnd, nullptr, inst, nullptr);
        labelStatus = ::CreateWindowExW(0, L"STATIC", L"Starting…",
                                        WS_CHILD | WS_VISIBLE, 20, 50, kWidth - 40, 24,
                                        hwnd, nullptr, inst, nullptr);
        progress = ::CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                     WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
                                     20, 86, kWidth - 40, 22, hwnd, nullptr, inst, nullptr);
        ::SendMessageW(progress, PBM_SETMARQUEE, TRUE, 30);

        // Use a slightly larger font for the title.
        HFONT font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
        ::SendMessageW(labelTitle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ::SendMessageW(labelStatus, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        ::ShowWindow(hwnd, SW_SHOW);
        ::UpdateWindow(hwnd);
    }

    static DWORD WINAPI threadMain(LPVOID param)
    {
        Impl *self = reinterpret_cast<Impl *>(param);
        self->createWindow();
        ::SetEvent(self->readyEvent);

        MSG msg;
        while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        return 0;
    }
};

SplashWindow::SplashWindow() : m_impl(new Impl) {}

SplashWindow::~SplashWindow()
{
    close();
    if (m_impl->readyEvent) {
        ::CloseHandle(m_impl->readyEvent);
    }
    if (m_impl->thread) {
        ::CloseHandle(m_impl->thread);
    }
    delete m_impl;
}

void SplashWindow::show(const std::wstring &title)
{
    if (m_impl->thread) {
        return;
    }
    m_impl->title = title;
    m_impl->readyEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_impl->thread = ::CreateThread(nullptr, 0, &Impl::threadMain, m_impl, 0, &m_impl->threadId);
    if (m_impl->readyEvent) {
        ::WaitForSingleObject(m_impl->readyEvent, 5000);
    }
}

void SplashWindow::close()
{
    if (m_impl->hwnd) {
        ::PostMessageW(m_impl->hwnd, WM_SPLASH_CLOSE, 0, 0);
    }
    if (m_impl->thread) {
        ::WaitForSingleObject(m_impl->thread, 3000);
    }
    m_impl->hwnd = nullptr;
}

void SplashWindow::setStatus(const std::wstring &text)
{
    if (m_impl->hwnd) {
        ::PostMessageW(m_impl->hwnd, WM_SPLASH_STATUS, 0, reinterpret_cast<LPARAM>(new std::wstring(text)));
    }
}

void SplashWindow::setProgress(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (m_impl->hwnd) {
        ::PostMessageW(m_impl->hwnd, WM_SPLASH_PROGRESS, static_cast<WPARAM>(percent), 0);
    }
}

void SplashWindow::setMarquee(bool on)
{
    if (m_impl->hwnd) {
        ::PostMessageW(m_impl->hwnd, WM_SPLASH_MARQUEE, on ? 1 : 0, 0);
    }
}

void SplashWindow::showError(const std::wstring &title, const std::wstring &message)
{
    ::MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

} // namespace qgc
