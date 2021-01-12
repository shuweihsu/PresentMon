#pragma once

/*
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 1 // The C++ Standard doesn't provide equivalent non-deprecated functionality yet.

#if (_MSC_VER >= 1915)
#define no_init_all deprecated
#endif
*/

#include <vcruntime.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.AI.MachineLearning.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <wrl/client.h>
#include <d3d11_2.h>
#include <d2d1.h>
#include <dwrite_1.h>
#include <dwmapi.h> 
#include <tlhelp32.h>
#include <string>
#include <codecvt>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <queue>
#include <codecvt>

// forward declaration
struct ProcessInfo;
struct SwapChainData;
struct PresentEvent;

enum STUTTER_EXPERIENCE : int { 
    NO_PREDICT = -2,
    NOT_READY = -1, 
    BAD = 0, 
    GOOD = 1,
	
};

bool MLInitialize();
void MLCleanup();
void MLUpdateConsole();

bool MLInitializeOverlayPanel();
void MLCleanupOverlayPanel();
HWND MLGetOverlayPanelHWND();
void MLRenderOverlayPanel();
void MLUpdateOverlayPanel();
STUTTER_EXPERIENCE MLGetStutterExperiencePrediction();
void MLUpdateInputAndPredict(ProcessInfo* processInfo, SwapChainData const& chain, PresentEvent const& p);

