#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>
#include <cstdio>
#include <algorithm>
#include <thread>
#include <atomic>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")


struct Result {
    bool active = false;
    std::string app;
    bool isStartup = false;
};

std::string getTimestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buffer[64];
    sprintf_s(buffer, sizeof(buffer), "[%04d-%02d-%02d %02d:%02d:%02d]",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    return std::string(buffer);
}

std::string sanitizePath(const std::string& input)
{
    std::string result = input;
    std::replace(result.begin(), result.end(), '#', '\\');
    size_t nonPkgPos = result.find("NonPackaged\\");
    if (nonPkgPos != std::string::npos)
        result = result.substr(nonPkgPos + 12);
    return result;
}

std::string toLower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

std::string extractExeName(const std::string& path)
{
    size_t lastSlash = path.find_last_of("\\/");
    std::string name = (lastSlash != std::string::npos)
        ? path.substr(lastSlash + 1) : path;
    return toLower(name);
}

bool isAppInStartup(const std::string& appPath)
{
    std::string exeName = extractExeName(appPath);
    if (exeName.empty()) return false;

    struct StartupKey { HKEY hive; const char* path; };
    StartupKey keys[] = {
        { HKEY_CURRENT_USER,  "Software\\Microsoft\\Windows\\CurrentVersion\\Run" },
        { HKEY_CURRENT_USER,  "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce" },
        { HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Run" },
        { HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce" },
    };

    for (const auto& sk : keys)
    {
        HKEY hKey;
        if (RegOpenKeyExA(sk.hive, sk.path, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
            continue;

        char valueName[256];
        char valueData[1024];

        for (DWORD i = 0; ; ++i)
        {
            DWORD nameSize = sizeof(valueName);
            DWORD dataSize = sizeof(valueData);
            DWORD type = 0;

            LONG res = RegEnumValueA(hKey, i, valueName, &nameSize,
                nullptr, &type, (LPBYTE)valueData, &dataSize);

            if (res == ERROR_NO_MORE_ITEMS) break;
            if (res != ERROR_SUCCESS) continue;
            if (type != REG_SZ && type != REG_EXPAND_SZ) continue;

            std::string data = toLower(std::string(valueData));
            if (data.find(exeName) != std::string::npos)
            {
                RegCloseKey(hKey);
                return true;
            }
        }
        RegCloseKey(hKey);
    }
    return false;
}

Result checkDevice(const char* device)
{
    std::string base =
        "Software\\Microsoft\\Windows\\CurrentVersion\\"
        "CapabilityAccessManager\\ConsentStore\\" + std::string(device);

    Result result;
    std::function<void(HKEY, const std::string&)> scan;

    scan = [&](HKEY key, const std::string&)
        {
            char sub[512];
            DWORD size;

            for (DWORD i = 0;; i++)
            {
                size = sizeof(sub);
                if (RegEnumKeyExA(key, i, sub, &size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                    break;

                HKEY child;
                if (RegOpenKeyExA(key, sub, 0, KEY_READ, &child) != ERROR_SUCCESS)
                    continue;

                DWORD64 start = 0, stop = 0;
                DWORD s1 = sizeof(start), s2 = sizeof(stop);

                RegQueryValueExA(child, "LastUsedTimeStart", nullptr, nullptr, (LPBYTE)&start, &s1);
                RegQueryValueExA(child, "LastUsedTimeStop", nullptr, nullptr, (LPBYTE)&stop, &s2);

                if (start > 0 && stop == 0)
                {
                    result.active = true;
                    result.app = sanitizePath(sub);
                    result.isStartup = isAppInStartup(result.app);
                    RegCloseKey(child);
                    return;
                }

                scan(child, sub);
                RegCloseKey(child);
                if (result.active) return;
            }
        };

    HKEY root;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, base.c_str(), 0, KEY_READ, &root) == ERROR_SUCCESS)
    {
        scan(root, "");
        RegCloseKey(root);
    }
    return result;
}

#define WM_TRAYICON   (WM_USER + 1)
#define ID_TRAY_EXIT  1001
#define ID_TRAY_STATUS 1002

HWND              g_hwnd = nullptr;
NOTIFYICONDATAA   g_nid = {};
HICON             g_iconIdle = nullptr;
HICON             g_iconActive = nullptr;
HICON             g_iconWarn = nullptr;
std::atomic<bool> g_running{ true };

HICON CreateColorIcon(COLORREF color, bool warn = false)
{
    const int size = 16;
    HDC hdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, size, size);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
    RECT rc = { 0, 0, size, size };
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    HBRUSH br = CreateSolidBrush(color);
    SelectObject(mem, br);
    Ellipse(mem, 2, 2, size - 2, size - 2);
    DeleteObject(br);

    if (warn)
    {
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 40, 40));
        SelectObject(mem, pen);
        MoveToEx(mem, 8, 4, nullptr);
        LineTo(mem, 8, 10);
        MoveToEx(mem, 8, 12, nullptr);
        LineTo(mem, 8, 13);
        DeleteObject(pen);
    }

    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, hdc);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = bmp;
    ii.hbmMask = bmp;
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(bmp);
    return hIcon;
}

void ShowBalloon(const char* title, const char* text, DWORD icon = NIIF_INFO)
{
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = icon | NIIF_NOSOUND;
    strncpy_s(g_nid.szInfoTitle, title, _TRUNCATE);
    strncpy_s(g_nid.szInfo, text, _TRUNCATE);
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);

    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.szInfo[0] = 0;
    g_nid.szInfoTitle[0] = 0;
}

void UpdateTrayIcon(bool cam, bool mic, bool anyStartup)
{
    HICON icon = g_iconIdle;
    if (anyStartup)      icon = g_iconWarn;
    else if (cam || mic) icon = g_iconActive;

    g_nid.hIcon = icon;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;

    char tip[128];
    sprintf_s(tip, "Cam: %s  Mic: %s",
        cam ? "ON" : "off",
        mic ? "ON" : "off");
    strncpy_s(g_nid.szTip, tip, _TRUNCATE);
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

void ShowStatusPopup()
{
    Result cam = checkDevice("webcam");
    Result mic = checkDevice("microphone");

    char buf[2048];
    sprintf_s(buf,
        "%s\n"
        "=======================================\n"
        "Camera:        %s\n"
        "Camera App:    %s\n"
        "%s"
        "---------------------------------------\n"
        "Microphone:    %s\n"
        "Microphone App:%s\n"
        "%s"
        "=======================================",
        getTimestamp().c_str(),

        cam.active ? "TRUE (ACTIVE)" : "FALSE (idle)",
        cam.active ? cam.app.c_str() : "-",
        (cam.active && cam.isStartup)
        ? "  [!] FLAGGED: this app is registered to launch at startup\n"
        : "  Startup flag: no\n",

        mic.active ? "TRUE (ACTIVE)" : "FALSE (idle)",
        mic.active ? mic.app.c_str() : "-",
        (mic.active && mic.isStartup)
        ? "  [!] FLAGGED: this app is registered to launch at startup\n"
        : "  Startup flag: no\n"
    );

    MessageBoxA(g_hwnd, buf, "Camera / Microphone Status", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING, ID_TRAY_STATUS, "Show current status");
            AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(menu, MF_STRING, ID_TRAY_EXIT, "Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
        else if (lParam == WM_LBUTTONDBLCLK)
        {
            ShowStatusPopup();
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_STATUS)
            ShowStatusPopup();
        else if (LOWORD(wParam) == ID_TRAY_EXIT)
        {
            g_running = false;
            PostQuitMessage(0);
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        if (g_iconIdle)   DestroyIcon(g_iconIdle);
        if (g_iconActive) DestroyIcon(g_iconActive);
        if (g_iconWarn)   DestroyIcon(g_iconWarn);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void TrayThread()
{
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "CamMicTrayClass";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowA("CamMicTrayClass", "", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr);

    g_iconIdle = CreateColorIcon(RGB(80, 180, 80));       
    g_iconActive = CreateColorIcon(RGB(220, 60, 60));      
    g_iconWarn = CreateColorIcon(RGB(255, 160, 0), true); 

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_iconIdle;
    strcpy_s(g_nid.szTip, "Camera / Mic Monitor");
    Shell_NotifyIconA(NIM_ADD, &g_nid);

    MSG msg;
    while (g_running && GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}


int main()
{
    HWND console = GetConsoleWindow();
    if (console)
        ShowWindow(console, SW_HIDE);

    std::thread tray(TrayThread);

    bool lastCam = false;
    bool lastMic = false;

    while (g_running)
    {
        Result cam = checkDevice("webcam");
        Result mic = checkDevice("microphone");

        bool anyStartup = (cam.active && cam.isStartup) || (mic.active && mic.isStartup);
        UpdateTrayIcon(cam.active, mic.active, anyStartup);

        if (cam.active && !lastCam)
        {
            std::string msg = "App: " + cam.app;
            if (cam.isStartup)
            {
                msg += "\n⚠ This app is registered to launch at startup!";
                ShowBalloon("Camera ACTIVATED (Startup App)", msg.c_str(), NIIF_WARNING);
            }
            else
            {
                ShowBalloon("Camera ACTIVATED", msg.c_str(), NIIF_INFO);
            }
        }

        if (mic.active && !lastMic)
        {
            std::string msg = "App: " + mic.app;
            if (mic.isStartup)
            {
                msg += "\n⚠ This app is registered to launch at startup!";
                ShowBalloon("Microphone ACTIVATED (Startup App)", msg.c_str(), NIIF_WARNING);
            }
            else
            {
                ShowBalloon("Microphone ACTIVATED", msg.c_str(), NIIF_INFO);
            }
        }

        lastCam = cam.active;
        lastMic = mic.active;
            
        Sleep(5000);
    }

    if (g_hwnd)
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);

    tray.join();
    return 0;
}