// ----------------------------------------------------------------------------
// SplashWindow — a small always-shown Win32 progress window.
//
// Runs its own UI thread + message loop so it stays responsive while the
// orchestrator works on a background thread. The public methods are thread-safe
// (they marshal to the UI thread via PostMessage).
// ----------------------------------------------------------------------------
#pragma once

#include <string>

namespace qgc {

class SplashWindow {
public:
    SplashWindow();
    ~SplashWindow();

    // Creates and shows the window on a dedicated UI thread.
    void show(const std::wstring &title);
    void close();

    // Status line under the title.
    void setStatus(const std::wstring &text);

    // Determinate progress 0..100.
    void setProgress(int percent);
    // Indeterminate (marquee) animation while doing non-measurable work.
    void setMarquee(bool on);

    // Shows a blocking, actionable error dialog (modal). Used on fatal failure.
    static void showError(const std::wstring &title, const std::wstring &message);

private:
    struct Impl;
    Impl *m_impl;
};

} // namespace qgc
