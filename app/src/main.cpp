// LatencyForge エントリポイント。イベント駆動ループ: 入力/アニメーションが無い間はスリープし CPU/GPU を使わない。
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "dx11.hpp"
#include "lf/util.hpp"
#include "ui.hpp"

static_assert(IMGUI_VERSION_NUM >= 19200, "Dear ImGui 1.92+ (dynamic fonts) is required");

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr int kMinWidth = 920;   // 論理ピクセル
constexpr int kMinHeight = 600;

constexpr UINT_PTR kPollTimerId = 1;

struct App {
    HWND hwnd = nullptr;
    lfapp::Dx11 dx;
    lfapp::Ui ui;
    int framesLeft = 3;
    bool minimized = false;
    bool mouseInside = false;
    bool active = true;
    int timerMs = 0;  // Affinity 用ポーリングタイマー (0 = 停止)

    // 必要なときだけタイマーを動かす (無効/ゲーム未登録なら止める = アイドル時の負荷ゼロ)。
    void syncTimer() {
        const int wanted = ui.pollIntervalMs();
        if (wanted == timerMs) return;
        if (timerMs) KillTimer(hwnd, kPollTimerId);
        if (wanted) SetTimer(hwnd, kPollTimerId, static_cast<UINT>(wanted), nullptr);
        timerMs = wanted;
    }

    void render() {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ui.frame();
        ImGui::Render();
        const ImVec4 bg = ui.palette().bg;
        const float clear[4] = {bg.x, bg.y, bg.z, 1.0f};
        dx.beginFrame(clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        dx.present();
    }
};

App* g_app = nullptr;

float dpiScaleFor(HWND hwnd) { return static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f; }

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return TRUE;
    App* app = g_app;
    switch (msg) {
        case WM_SIZE:
            if (app && app->dx.device()) {
                app->minimized = (wp == SIZE_MINIMIZED);
                if (!app->minimized) {
                    app->dx.resize(LOWORD(lp), HIWORD(lp));
                    app->framesLeft = 3;
                    app->render();  // サイズ変更中のモーダルループでも追従して描画
                }
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            const float s = dpiScaleFor(hwnd);
            mmi->ptMinTrackSize.x = static_cast<LONG>(kMinWidth * s);
            mmi->ptMinTrackSize.y = static_cast<LONG>(kMinHeight * s);
            return 0;
        }
        case WM_DPICHANGED: {
            if (app) app->ui.setDpi(static_cast<float>(LOWORD(wp)) / 96.0f);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (app && !app->mouseInside) {
                app->mouseInside = true;
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&t);
            }
            break;
        case WM_MOUSELEAVE:
            if (app) app->mouseInside = false;
            return 0;
        case WM_ACTIVATE:
            if (app) app->active = LOWORD(wp) != WA_INACTIVE;
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wp == kPollTimerId && app) {
                if (app->ui.pollTimer()) app->framesLeft = std::max(app->framesLeft, 3);  // 状態が変わったときだけ再描画
                app->syncTimer();
            }
            return 0;
        case WM_CLOSE:
            // ウィンドウを閉じる前に、変更したアフィニティ/優先度を必ず元に戻す (ログオフ/シャットダウンは WM_ENDSESSION)。
            if (app) app->ui.shutdown();
            break;
        case WM_ENDSESSION:
            if (app && wp) app->ui.shutdown();
            break;
        case WM_DESTROY:
            if (app) app->ui.shutdown();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool dirWritable(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto probe = dir / ".write_test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// ポータブル運用: exe 隣の config/。書き込めなければ %APPDATA%\LatencyForge。
std::filesystem::path resolveConfigDir(const std::filesystem::path& exeDir) {
    auto local = exeDir / "config";
    if (dirWritable(local)) return local;
    PWSTR roaming = nullptr;
    std::filesystem::path out = local;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
        out = std::filesystem::path(roaming) / "LatencyForge";
        CoTaskMemFree(roaming);
        std::error_code ec;
        std::filesystem::create_directories(out, ec);
    }
    return out;
}

lfapp::Page parsePage(const std::wstring& s) {
    const int n = _wtoi(s.c_str());
    return (n >= 0 && n < static_cast<int>(lfapp::Page::Count)) ? static_cast<lfapp::Page>(n) : lfapp::Page::Home;
}

}  // namespace

// 二重起動防止: 名前付きミューテックス。2 つ目のインスタンスは既存ウィンドウを前面に出して終了する。
// (state.json を 2 つのプロセスが同時に書くと、バックアップが壊れうるため。)
struct SingleInstance {
    HANDLE mutex = nullptr;
    bool alreadyRunning = false;
    explicit SingleInstance(bool demo) {
        mutex = CreateMutexW(nullptr, FALSE, demo ? L"Local\\LatencyForge.SingleInstance.Demo" : L"Local\\LatencyForge.SingleInstance");
        alreadyRunning = mutex && GetLastError() == ERROR_ALREADY_EXISTS;
    }
    ~SingleInstance() {
        if (mutex) CloseHandle(mutex);
    }
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
};

constexpr const wchar_t* kWindowClass = L"LatencyForgeWnd";
constexpr const wchar_t* kTitle = L"LatencyForge";
constexpr const wchar_t* kTitleDemo = L"LatencyForge (DEMO)";

void bringExistingWindowToFront(bool demo) {
    if (HWND w = FindWindowW(kWindowClass, demo ? kTitleDemo : kTitle)) {
        if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
        SetForegroundWindow(w);
    }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // 開発/スクリーンショット用: --page N (0=Home .. 8=Settings)
    // --demo はメモリ上のレジストリで動作し、実機のレジストリには一切触れない。
    //   --skip-wizard / --show ... / --sim <サンプル名> (例: intel-hybrid, amd-dual-ccd, arm64) は --demo のときだけ有効。
    lfapp::Page startPage = lfapp::Page::Home;
    bool demo = false, skipWizard = false;
    std::string show, sim;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring a = argv[i];
            if (a == L"--demo") demo = true;
            if (a == L"--skip-wizard") skipWizard = true;
            if (i + 1 < argc && a == L"--page") startPage = parsePage(argv[i + 1]);
            if (i + 1 < argc && a == L"--sim") sim = lf::narrow(argv[i + 1]);
            if (i + 1 < argc && a == L"--show") {
                show = lf::narrow(argv[i + 1]);
            }
        }
        LocalFree(argv);
    }

    SingleInstance instance(demo);
    if (instance.alreadyRunning) {
        bringExistingWindowToFront(demo);
        return 0;
    }

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    App app;
    g_app = &app;

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    const float sysScale = static_cast<float>(GetDpiForSystem()) / 96.0f;
    RECT wr{0, 0, static_cast<LONG>(1180 * sysScale), static_cast<LONG>(760 * sysScale)};
    AdjustWindowRectExForDpi(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
    app.hwnd = CreateWindowExW(0, wc.lpszClassName, demo ? kTitleDemo : kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, hInst, nullptr);
    if (!app.hwnd) return 1;

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(app.hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    const COLORREF cap = RGB(0x12, 0x15, 0x1B);
    DwmSetWindowAttribute(app.hwnd, 35 /*DWMWA_CAPTION_COLOR (Win11)*/, &cap, sizeof cap);

    std::string err;
    if (!app.dx.init(app.hwnd, err)) {
        const std::wstring msg = L"Direct3D 11 を初期化できません / Cannot initialize Direct3D 11.\n\n" +
                                 std::wstring(err.begin(), err.end());
        MessageBoxW(app.hwnd, msg.c_str(), L"LatencyForge", MB_ICONERROR | MB_OK);
        return 2;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    const float dpi = dpiScaleFor(app.hwnd);
    lfapp::UiInit init;
    init.exeDir = exeDir;
    init.configDir = resolveConfigDir(exeDir);
    init.startPage = startPage;
    init.warp = app.dx.isWarp();
    init.demo = demo;
    init.skipWizard = demo && skipWizard;
    init.show = demo ? show : std::string();
    init.sim = demo ? sim : std::string();  // システム構成のサンプル (--demo のときだけ有効)
    app.ui.init(init, dpi);
    ImGui_ImplWin32_Init(app.hwnd);
    ImGui_ImplDX11_Init(app.dx.device(), app.dx.context());

    ShowWindow(app.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(app.hwnd);

    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            // ポーリングのタイマー (WM_TIMER) だけでは再描画しない。状態が変わったときだけ wndProc が要求する。
            if (msg.message != WM_TIMER) app.framesLeft = std::max(app.framesLeft, 3);
        }
        app.syncTimer();
        if (app.minimized) {
            WaitMessage();
            continue;
        }
        if (app.framesLeft > 0 || app.ui.animating()) {
            app.render();
            if (app.framesLeft > 0) --app.framesLeft;
        } else {
            // ツールチップ等の時間依存処理のため、ウィンドウ上にマウスがある間だけ低頻度で起床。
            const DWORD timeout = (app.mouseInside && app.active) ? 120 : INFINITE;
            if (MsgWaitForMultipleObjects(0, nullptr, FALSE, timeout, QS_ALLINPUT) == WAIT_TIMEOUT) app.framesLeft = 1;
        }
    }
done:
    app.ui.shutdown();  // 念のため (冪等): どの終了経路でも、変更したプロセスを元に戻す
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyWindow(app.hwnd);
    g_app = nullptr;
    return 0;
}
