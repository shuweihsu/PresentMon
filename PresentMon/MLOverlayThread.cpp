#include "PresentMon.hpp"

static std::thread gThread;
static bool gQuit;

static void OverlayMLResults()
{
    // Initialize overlay panel
    MLInitializeOverlayPanel();
    
    auto const& args = GetCommandLineArgs();

    // Enter render loop
    while (args.mMLShowOverlay==1) {

        HWND hwnd = MLGetOverlayPanelHWND();

        const MARGINS m = { -1 };
        MSG message = { 0 };
        while (PeekMessage(&message, hwnd, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        
            MLUpdateOverlayPanel();
            MLRenderOverlayPanel();

        if (gQuit) {
            break;
        }
    }

    MLCleanupOverlayPanel();
}

void StartMLOverlayThread() {
    gThread = std::thread(OverlayMLResults);
}

void StopMLOverlayThread() {
    if (gThread.joinable()) {
        gQuit = true;
        gThread.join();
    }
}