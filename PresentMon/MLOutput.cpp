#include "PresentMon.hpp"
#include "MLOutput.hpp"

using namespace winrt::Windows::AI::MachineLearning;
using namespace std;

constexpr auto ML_INPUT_AVG_WINDOW_SIZE = 2; // Default is 2. Might be changed through cmd parameters. 
constexpr auto ML_INPUT_SIZE = 256;          // Should be the same as the FPS_HISTORY_SIZE in the model training script.
constexpr auto ML_FRAME_CAP = 500;           // Should be the same as the FPS_MAX value in the model training script.
constexpr auto ML_FRAME_TIME_CAP = 200;      // Should be the same as the FRAME_TIME_MAX value in the model training script.

class OverlayPanel;

// Moving Averager
class MovingAverage {
public:
    MovingAverage() : mSum(0), mWidth(ML_INPUT_AVG_WINDOW_SIZE) {}

    void setWindowSize(int size) {
        mWidth = size;
    }

    float next(float value) {
        if (mQ.size() >= mWidth) {
            mSum -= mQ.front();
            mQ.pop();
        }
        mQ.push(value);
        mSum += value;
        return mSum / mQ.size();
    }

private:
    queue<float> mQ;
    float mSum;
    int mWidth;
};


vector<float> gMLInputData = {};
vector<int> gMLExperience = {};
MovingAverage gMovingAverageProcessor;
LearningModel gMLModel = nullptr;
LearningModelSession gMLSession = nullptr;
LearningModelBinding gMLBinding = nullptr;
winrt::Windows::Foundation::Collections::IVectorView<float> gMLResult = nullptr;
OverlayPanel* gOverlayPanel = nullptr;
std::wstring gTargeteProcessWindowTitle;
std::wstring modelName = L"MLSD.onnx";



// Find process utility
unsigned long GetProcessId(std::wstring processName)
{
    PROCESSENTRY32 processInfo;
    processInfo.dwSize = sizeof(processInfo);

    HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (processesSnapshot == INVALID_HANDLE_VALUE)
        return 0;

    Process32First(processesSnapshot, &processInfo);
    if (!processName.compare(processInfo.szExeFile))
    {
        CloseHandle(processesSnapshot);
        return processInfo.th32ProcessID;
    }

    while (Process32Next(processesSnapshot, &processInfo))
    {
        if (!processName.compare(processInfo.szExeFile))
        {
            CloseHandle(processesSnapshot);
            return processInfo.th32ProcessID;
        }
    }

    CloseHandle(processesSnapshot);
    return 0;
}

struct param_enum
{
    unsigned long pid = 0;
    HWND hWnd = nullptr;
};

BOOL CALLBACK enum_windows_callback(HWND curhWnd, LPARAM lParam)
{
    param_enum& target = *(param_enum*)lParam;
    unsigned long curPid = 0;
    GetWindowThreadProcessId(curhWnd, &curPid);
    if (target.pid == curPid)
    {
        target.hWnd = curhWnd;
        return FALSE;   // stop enumerating 
    }

    return TRUE;  // not found yet. keep enumerating
}

wstring GetWindowTitleViaPid(unsigned long process_id)
{
    wchar_t title[255] = L"\0";
    param_enum param = { process_id, nullptr };
    
    EnumWindows(enum_windows_callback, (LPARAM)&param);
    GetWindowTextW(param.hWnd, (LPTSTR)(&title), 255);
    return wstring(title);
}




// Overlay Panel
class OverlayPanel {

private:
    Microsoft::WRL::ComPtr<ID3D11Device> mDxDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> mDxContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> mD2DRenderTarget;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mD2DRedBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mD2DGreenBrush;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> mDWriteTextFormat;

    HWND mOverlayWindowHWND;

    std::wstring mTargetWindowTitle;
    std::wstring mText1;
    std::wstring mText2;
    int mTargetWindowWidth;
    int mTargetWindowHeight;
    int mReadyToRender;


public:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        MARGINS margin = { -1 };
        switch (message)
        {
        //case WM_PAINT:
        case WM_ACTIVATE:
            DwmExtendFrameIntoClientArea(hWnd, &margin);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            break;
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }

public:
    OverlayPanel() :
        mTargetWindowWidth(0),
        mTargetWindowHeight(0),
        mReadyToRender(false),
        mOverlayWindowHWND(0)
    {
    }

    ~OverlayPanel() {
        dxResourceCleanUp();
    }

    bool readyToRender() {
        return mReadyToRender;
    }

    void dxResourceCleanUp() {
        mDxDevice = nullptr;
        mDxContext = nullptr;
        mSwapChain = nullptr;
        mD2DRenderTarget = nullptr;
        mD2DRedBrush = nullptr;
        mD2DGreenBrush = nullptr;
        mDWriteTextFormat = nullptr;
    }

    bool bindTargetWindowTitle(std::wstring title) {
        HWND hwnd = findTargetWindow(title);
        if (hwnd != NULL) {
            RECT rc = { 0 };
            GetWindowRect(hwnd, &rc);
            mTargetWindowWidth = rc.right - rc.left;
            mTargetWindowHeight = rc.bottom - rc.top;
            mTargetWindowTitle = title;

            return true;
        }

        return false;
    }

    HWND createWindow() {
        WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = OverlayPanel::WindowProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = sizeof(LONG_PTR);
        wcex.hInstance = GetModuleHandle(0);
        wcex.hbrBackground = NULL;
        wcex.lpszMenuName = NULL;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)COLOR_WINDOW;
        wcex.lpszClassName = L"OverlayPanel";
        ATOM winClassName = RegisterClassEx(&wcex);

        mOverlayWindowHWND = CreateWindowExW(
            0,
            (LPCWSTR)winClassName,
            L"",
            WS_EX_TOPMOST | WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            mTargetWindowWidth,
            mTargetWindowHeight,
            NULL,
            NULL,
            GetModuleHandle(0),
            NULL);

        SetWindowLong(mOverlayWindowHWND, GWL_EXSTYLE, (int)GetWindowLong(mOverlayWindowHWND, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT);
        SetLayeredWindowAttributes(mOverlayWindowHWND, RGB(0, 0, 0), 0, ULW_COLORKEY);
        SetLayeredWindowAttributes(mOverlayWindowHWND, 0, 255, LWA_ALPHA);
        ShowWindow(mOverlayWindowHWND, SW_SHOWNORMAL);

        return mOverlayWindowHWND;
    }

    bool initD3D11(HWND hWnd) {
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;

        DXGI_SWAP_CHAIN_DESC swapChainDesc = { };
        swapChainDesc.BufferCount = 2;
        swapChainDesc.BufferDesc.Width = 0;
        swapChainDesc.BufferDesc.Height = 0;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
        swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = hWnd;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.Windowed = true;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            NULL,
            D3D_DRIVER_TYPE_HARDWARE,
            NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            &level,
            1,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &mSwapChain,
            &mDxDevice,
            NULL,
            &mDxContext);

        if (FAILED(hr)) {
            return false;
        }

        // Set up the D3D render target view to the back buffer
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBufferView;
        if (FAILED(mSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
        if (FAILED(mDxDevice->CreateRenderTargetView(backBuffer.Get(), NULL, &backBufferView))) return false;;
        mDxContext->OMSetRenderTargets(1, &backBufferView, NULL);

        // Create the D2D factory
        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        D2D1_FACTORY_OPTIONS options;
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options, &factory))) return false;

        // Set up the D2D render target using the back buffer
        Microsoft::WRL::ComPtr<IDXGISurface> dxgiBackbuffer;
        if (FAILED(mSwapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackbuffer)))) return false;

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        if (FAILED(factory->CreateDxgiSurfaceRenderTarget(dxgiBackbuffer.Get(), props, &mD2DRenderTarget))) return false;

        // Create the DWrite factory
        Microsoft::WRL::ComPtr<IDWriteFactory1> writeFactory;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1), (IUnknown**)(&writeFactory)))) return false;

        // Create the DRwite text format
        if (FAILED(writeFactory->CreateTextFormat(
            L"Consolas",
            NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            20,
            L"",
            &mDWriteTextFormat)))
        {
            return false;
        }

        // Create brushes
        if (FAILED(mD2DRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::GreenYellow), &mD2DGreenBrush))) return false;
        if (FAILED(mD2DRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::OrangeRed), &mD2DRedBrush))) return false;

        mReadyToRender = true;

        return true;
    }

    void syncToTargetWindowsPosition() {
        RECT rc;
        if (!GetWindowRect(findTargetWindow(mTargetWindowTitle), &rc)) {
            ::ShowWindow(mOverlayWindowHWND, SW_HIDE);
        }
        else {
            ::ShowWindow(mOverlayWindowHWND, SW_SHOW);
            ::SetWindowPos(mOverlayWindowHWND, HWND_TOPMOST, rc.left, rc.top, 0, 0, SWP_NOSIZE);
        }
    }

    void updateText(std::wstring s1, std::wstring s2) {
        mText1 = s1;
        mText2 = s2;
    }

    void render() {
        const int shiftFromLeft = 80;
        const int shiftFromBtn1 = 64;
        const int shiftFromBtn2 = 32;

        if (!mReadyToRender) return;

        mD2DRenderTarget->BeginDraw();
        mD2DRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black, 0));
        mD2DRenderTarget->DrawText(
            mText1.c_str(), 
            static_cast<UINT32>(mText1.size()), 
            mDWriteTextFormat.Get(), 
            D2D1::RectF(shiftFromLeft, static_cast<FLOAT>(mTargetWindowHeight)- shiftFromBtn1, static_cast<FLOAT>(mTargetWindowWidth), static_cast<FLOAT>(mTargetWindowHeight)),
            mD2DGreenBrush.Get()
        );
        mD2DRenderTarget->DrawText(
            mText2.c_str(),
            static_cast<UINT32>(mText2.size()),
            mDWriteTextFormat.Get(),
            D2D1::RectF(shiftFromLeft, static_cast<FLOAT>(mTargetWindowHeight)- shiftFromBtn2, static_cast<FLOAT>(mTargetWindowWidth), static_cast<FLOAT>(mTargetWindowHeight)),
            mD2DRedBrush.Get()
        );
        mD2DRenderTarget->EndDraw();
        mSwapChain->Present(0, 0);
    }

    HWND findTargetWindow(std::wstring title) {
        HWND hwnd = FindWindow(NULL, title.c_str());
        return hwnd;
    }

    HWND getOverlaypanelHWND() {
        return mOverlayWindowHWND;
    }
};





string GetModulePath()
{
    string val;
    char modulePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, modulePath, ARRAYSIZE(modulePath));
    char drive[_MAX_DRIVE];
    char dir[_MAX_DIR];
    char filename[_MAX_FNAME];
    char ext[_MAX_EXT];
    _splitpath_s(modulePath, drive, _MAX_DRIVE, dir, _MAX_DIR, filename, _MAX_FNAME, ext, _MAX_EXT);

    val = drive;
    val += dir;
    return val;
}

wstring GetModelPath()
{
    wostringstream woss;
    woss << GetModulePath().c_str();
    woss << modelName.c_str();
    return woss.str();
}

bool MLInitialize() {
    winrt::init_apartment();

    // Get model path
    auto modelPath = GetModelPath();

    // load the model
    gMLModel = LearningModel::LoadFromFilePath(modelPath);

    // now create a session and binding
    gMLSession = LearningModelSession(gMLModel, LearningModelDevice(LearningModelDeviceKind::Default));
    gMLBinding = LearningModelBinding(gMLSession);

    // set average window size for input
    auto const& args = GetCommandLineArgs();
    gMovingAverageProcessor.setWindowSize(args.mMLMovingAverageSize);

    return true;
}

void MLCleanup() {
    gMLResult = nullptr;
    gMLBinding = nullptr;
    gMLModel = nullptr;
    gMLSession = nullptr;
}

bool MLInitializeOverlayPanel() {
    // create overlay panel
    auto const& args = GetCommandLineArgs();
    gOverlayPanel = new OverlayPanel();

    std::string processName = (args.mTargetProcessNames.size() > 0) ? args.mTargetProcessNames[0] : "";
    std::wstring processNameW;
    processNameW.assign(processName.begin(), processName.end());

    gTargeteProcessWindowTitle = GetWindowTitleViaPid(GetProcessId(processNameW));
    gOverlayPanel->bindTargetWindowTitle(gTargeteProcessWindowTitle);
    HWND hWnd = gOverlayPanel->createWindow();
    gOverlayPanel->initD3D11(hWnd);
    gOverlayPanel->render();

    return true;
}

void MLCleanupOverlayPanel() {
    if (gOverlayPanel)
        gOverlayPanel->dxResourceCleanUp();
    gOverlayPanel = nullptr;
}



STUTTER_EXPERIENCE MLGetStutterExperiencePrediction() {
    if (!gMLResult) {
        return STUTTER_EXPERIENCE::NOT_READY;
    }

    return (gMLResult.GetAt(1) > gMLResult.GetAt(0)) ? STUTTER_EXPERIENCE::GOOD : STUTTER_EXPERIENCE::BAD;
}

void MLUpdateInputAndPredict(ProcessInfo* processInfo, SwapChainData const& chain, PresentEvent const& p) {
    (void)processInfo;

    auto const& args = GetCommandLineArgs();

    // Don't output dropped frames (if requested).
    auto presented = p.FinalState == PresentResult::Presented;
    if (args.mExcludeDropped && !presented) {
        return;
    }

    // Look up the last present event in the swapchain's history.  We need at
    // least two presents to compute frame statistics.
    if (chain.mPresentHistoryCount == 0) {
        return;
    }

    auto lastPresented = chain.mPresentHistory[(chain.mNextPresentIndex - 1) % SwapChainData::PRESENT_HISTORY_MAX_COUNT].get();
    float frametimeMS =  1000 * static_cast<float>(QpcDeltaToSeconds(p.QpcTime - lastPresented->QpcTime));

    // Cap frame time to ML_FRAME_TIME_CAP and then normalize it to [0,1]
    float frametimeMSNorm =  max(0.0f, min(1.0f, frametimeMS / ML_FRAME_TIME_CAP));

    gMLInputData.push_back(gMovingAverageProcessor.next(frametimeMSNorm));

    if (gMLInputData.size() == ML_INPUT_SIZE) {
        // Bind input
        std::vector<int64_t> inputShape({ 1, ML_INPUT_SIZE, 1 });
        gMLBinding.Bind(L"conv1d_1_input", TensorFloat::CreateFromIterable(inputShape, gMLInputData));

        // Run the model
        auto optionalCorrelationId = L"";
        auto results = gMLSession.Evaluate(gMLBinding, optionalCorrelationId);

        // Get the prediction result
        auto resultTensor = results.Outputs().Lookup(L"output").as<TensorFloat>();
        gMLResult = resultTensor.GetAsVectorView();

        // Store prediction result
        gMLExperience.push_back(MLGetStutterExperiencePrediction());

        // Make some room for the following inputs. We will do the prediction once the input is full again. 
        gMLInputData.erase(gMLInputData.begin(), gMLInputData.begin() + 20);
    }
    else {
        gMLExperience.push_back(STUTTER_EXPERIENCE::NO_PREDICT);
    }

	// Remove old entries in experience queue
    if (gMLExperience.size() == ML_INPUT_SIZE) {
        gMLExperience.erase(gMLExperience.begin());
    }
}

void MLUpdateConsole() {
    // get the latest 100 prediction 
    const int maxPredictionHistory = 100;
    string buffer(maxPredictionHistory, ' ');
    vector<int>::reverse_iterator ritr = gMLExperience.rbegin();

    for (int col = maxPredictionHistory -1; col >= 0 && ritr != gMLExperience.rend(); --col) {
        switch (*ritr++) {
            case STUTTER_EXPERIENCE::NOT_READY :
                buffer[col] = '.';
                break;
            case STUTTER_EXPERIENCE::GOOD :
                buffer[col] = 'O';
                break;
            case STUTTER_EXPERIENCE::BAD :
                buffer[col] = 'X';
                break;
            default:
                buffer[col] = ' ';
                break;
        }
    }

    int goodExpCnt = 0;
    int badExpCnt = 0;
    for (auto& i : gMLExperience) {
        if (i == STUTTER_EXPERIENCE::GOOD) goodExpCnt++;
        else if (i == STUTTER_EXPERIENCE::BAD) badExpCnt++;
    }

    ConsolePrintLn("%s", string(100, '-').c_str());
    ConsolePrintLn("%s", buffer.data());
    ConsolePrintLn("%s", string(100, '-').c_str());
    ConsolePrintLn("good: %s", string(goodExpCnt, '=').c_str());
    ConsolePrintLn(" bad: %s", string(badExpCnt, '=').c_str());
}


HWND MLGetOverlayPanelHWND() {
    if (!gOverlayPanel || !gOverlayPanel->readyToRender())
        return NULL;

    return gOverlayPanel->getOverlaypanelHWND();
}


void MLRenderOverlayPanel() {
    if (!gOverlayPanel || !gOverlayPanel->readyToRender()) {
        return;
    }

    gOverlayPanel->syncToTargetWindowsPosition();
    gOverlayPanel->render();
}


void MLUpdateOverlayPanel() {
    int goodExpCnt = 0;
    int badExpCnt = 0;
    for (auto& i : gMLExperience) {
        if (i == STUTTER_EXPERIENCE::GOOD) goodExpCnt++;
        else if (i == STUTTER_EXPERIENCE::BAD) badExpCnt++;
    }

    wstring s1, s2;
    s1 += L"good:" + wstring(goodExpCnt, L'=');
    s2 += L" bad:" + wstring(badExpCnt, L'=');
    
    gOverlayPanel->updateText(s1, s2);
}