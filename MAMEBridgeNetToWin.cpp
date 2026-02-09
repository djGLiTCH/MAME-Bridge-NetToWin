// license: BSD-3-Clause
// copyright-holders: Jacob Simpson

// MAME Bridge NetToWin
// Version 2.7.0
// Author: DJ GLiTCH
// Designed to bridge the gap between network and windows output in MAME.

// Compile with MSYS2 MINGW64:
// windres bridge.rc -o bridge.o
// g++ MAMEBridgeNetToWin.cpp bridge.o -o MAME-Bridge-NetToWin.exe -lws2_32 -mwindows -static

#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")

// --- CONFIGURATION ---
#define MAME_IP "127.0.0.1"
#define MAME_PORT 8000
#define BRIDGE_WINDOW_CLASS "MAMEOutput"
#define GUI_WINDOW_CLASS "NetToWinGUI"
#define WM_SHELLNOTIFY (WM_USER + 1)
#define WM_APPEND_LOG  (WM_USER + 2)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT     1002
#define ID_TRAY_SHOW     1003
#define ID_TRAY_ABOUT    1004
#define ID_TRAY_GITHUB   1005

#define TOOL_NAME "MAME Bridge NetToWin"
#define TOOL_VERSION "2.7.0"
#define TOOL_AUTHOR "DJ GLiTCH"
#define GITHUB_LINK "https://github.com/djGLiTCH/MAME-Bridge-NetToWin"

// --- GLOBALS ---
HWND g_hwndGUI = NULL;
HWND g_hwndBridge = NULL;
HWND g_hLogCtrl = NULL;
NOTIFYICONDATA g_nid;
std::atomic<bool> g_running(true);
std::vector<HWND> g_clients;

// ID MAPPING
std::map<std::string, LPARAM> g_nameToID;
std::map<LPARAM, std::string> g_idToName;
LPARAM g_nextID = 1;
std::string g_currentRomName = "___empty"; 

// MESSAGES
UINT om_mame_start;
UINT om_mame_stop;
UINT om_mame_update_state;
UINT om_mame_register_client;
UINT om_mame_unregister_client;
UINT om_mame_get_id_string;

// --- RESOURCE HELPER ---
std::string LoadDescriptionFromResource() {
    HRSRC hRes = FindResource(NULL, "DESCRIPTION_TEXT", RT_RCDATA);
    if (!hRes) return "Error: Description resource not found.";
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return "Error: Could not load description.";
    const char* data = (const char*)LockResource(hData);
    DWORD size = SizeofResource(NULL, hRes);
    return std::string(data, size);
}

// --- LOGGING ---
void Log(const std::string& msg) {
    if (g_hwndGUI) {
        std::string* pMsg = new std::string(msg);
        PostMessage(g_hwndGUI, WM_APPEND_LOG, 0, (LPARAM)pMsg);
    }
}

// --- ID MANAGER ---
LPARAM GetIDForName(const std::string& name) {
    if (g_nameToID.find(name) == g_nameToID.end()) {
        LPARAM newID = g_nextID++;
        g_nameToID[name] = newID;
        g_idToName[newID] = name;
        
        if (newID < 1000) { 
            std::stringstream ss;
            ss << "[MAP] New Output: '" << name << "' -> ID " << newID;
            Log(ss.str());
        }
        return newID;
    }
    return g_nameToID[name];
}

// --- BRIDGE WINDOW PROC (Hidden) ---
LRESULT CALLBACK BridgeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == om_mame_register_client) {
        HWND client = (HWND)wParam;
        g_clients.push_back(client);
        Log("[WIN] Client Registered!");
        // FIX: Removed PostMessage(om_mame_start) here to prevent infinite loop.
        // The client already knows we exist, so we just accept the registration silently.
        return 1;
    }
    else if (msg == om_mame_unregister_client) {
        HWND client = (HWND)wParam;
        for (auto it = g_clients.begin(); it != g_clients.end(); ++it) {
            if (*it == client) {
                g_clients.erase(it);
                break;
            }
        }
        Log("[WIN] Client Unregistered");
        return 1;
    }
    else if (msg == om_mame_get_id_string) {
        LPARAM id = (LPARAM)wParam;
        std::string name = "";

        // ID 0 is System Name
        if (id == 0) name = g_currentRomName;
        else if (g_idToName.count(id)) name = g_idToName[id];

        struct copydata_id_string { uint32_t id; char string[1]; };
        int dataLen = sizeof(copydata_id_string) + name.length() + 1;
        std::vector<uint8_t> buffer(dataLen);
        copydata_id_string* pData = (copydata_id_string*)buffer.data();
        pData->id = (uint32_t)id;
        strcpy(pData->string, name.c_str());

        COPYDATASTRUCT copyData = { 1, (DWORD)dataLen, pData };
        SendMessage((HWND)wParam, WM_COPYDATA, (WPARAM)g_hwndBridge, (LPARAM)&copyData);
        return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// --- GUI WINDOW PROC ---
LRESULT CALLBACK GUIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hLogCtrl = CreateWindowEx(0, "EDIT", "", 
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
            0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_hLogCtrl, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), 0);
        Log(std::string(TOOL_NAME) + " - Version " + std::string(TOOL_VERSION));
        break;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) ShowWindow(hwnd, SW_HIDE);
        else {
            RECT rc; GetClientRect(hwnd, &rc);
            MoveWindow(g_hLogCtrl, 0, 0, rc.right, rc.bottom, TRUE);
        }
        break;

    case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;

    case WM_SHELLNOTIFY:
        if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW, "Show Logs");
            AppendMenu(hMenu, MF_STRING, ID_TRAY_ABOUT, "About");
            AppendMenu(hMenu, MF_STRING, ID_TRAY_GITHUB, "GitHub");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            if (cmd == ID_TRAY_EXIT) DestroyWindow(hwnd);
            if (cmd == ID_TRAY_SHOW) { ShowWindow(hwnd, SW_SHOW); ShowWindow(hwnd, SW_RESTORE); }
            if (cmd == ID_TRAY_GITHUB) ShellExecute(0, 0, GITHUB_LINK, 0, 0, SW_SHOW);
            if (cmd == ID_TRAY_ABOUT) {
                std::string desc = LoadDescriptionFromResource();
                std::string msg = std::string(TOOL_NAME) + "\nVersion: " + std::string(TOOL_VERSION) + 
                                  "\nAuthor: " + std::string(TOOL_AUTHOR) + "\n\n" + desc + 
                                  "\n\nGitHub: " + std::string(GITHUB_LINK);
                MessageBox(hwnd, msg.c_str(), "About", MB_ICONINFORMATION | MB_OK);
            }
            DestroyMenu(hMenu);
        }
        else if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hwnd, SW_SHOW); ShowWindow(hwnd, SW_RESTORE); }
        break;

    case WM_APPEND_LOG: {
        std::string* pStr = (std::string*)lParam;
        std::string finalMsg = *pStr + "\r\n";
        int len = GetWindowTextLength(g_hLogCtrl);
        SendMessage(g_hLogCtrl, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(g_hLogCtrl, EM_REPLACESEL, 0, (LPARAM)finalMsg.c_str());
        delete pStr;
        } break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        g_running = false;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// --- NETWORK PARSER ---
void ProcessLine(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) return;

    size_t eqPos = line.find("=");
    if (eqPos != std::string::npos) {
        std::string name = line.substr(0, eqPos);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        std::string valStr = line.substr(eqPos + 1);
        while (!valStr.empty() && valStr.front() == ' ') valStr.erase(0, 1);
        
        // CAPTURE MAME START
        if (name == "mame_start") {
            g_currentRomName = valStr;
            // Update ID 0 mapping
            g_idToName[0] = g_currentRomName;
            Log("[SYS] MAME Started. ROM: " + g_currentRomName);
            // Broadcast START again now that we have the real name
            PostMessage(HWND_BROADCAST, om_mame_start, (WPARAM)g_hwndBridge, 0);
            return;
        }

        int val = std::atoi(valStr.c_str());
        LPARAM id = GetIDForName(name);
        for (HWND client : g_clients) PostMessage(client, om_mame_update_state, (WPARAM)id, (LPARAM)val);
    }
}

// --- NETWORK THREAD ---
void NetworkThread() {
    Log("[SYS] Network Thread Started. Waiting for MAME...");
    
    while (g_running) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server = { AF_INET, htons(MAME_PORT) };
        server.sin_addr.s_addr = inet_addr(MAME_IP);

        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == 0) {
            Log("[NET] Connected to MAME!");

            // 1. INITIALIZE DEFAULTS
            g_currentRomName = "___empty"; // Use placeholder initially
            g_idToName[0] = "___empty";    // Ensure ID 0 map matches

            // 2. FORCE START BROADCAST
            PostMessage(HWND_BROADCAST, om_mame_start, (WPARAM)g_hwndBridge, 0);
            Log("[SYS] Sent Force Start Signal (___empty).");

            // 3. SEND HANDSHAKE
            const char* wakeUp = "\r\n";
            send(sock, wakeUp, 2, 0);

            char buffer[4096];
            std::string netBuffer = "";
            int n;
            
            while ((n = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
                netBuffer.append(buffer, n);
                size_t pos = 0;
                while ((pos = netBuffer.find('\n')) != std::string::npos) {
                    std::string line = netBuffer.substr(0, pos);
                    ProcessLine(line);
                    netBuffer.erase(0, pos + 1);
                }
            }
            Log("[NET] Disconnected from MAME.");
            
            PostMessage(HWND_BROADCAST, om_mame_stop, (WPARAM)g_hwndBridge, 0);
            Log("[SYS] Sent STOP Signal.");
            
            g_currentRomName = "___empty";
            g_nameToID.clear();
            g_idToName.clear();
            g_nextID = 1;

        } else {
            Sleep(2000);
        }
        closesocket(sock);
        WSACleanup();
    }
}

// --- MAIN ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutex(NULL, TRUE, "Global\\MAMEBridgeNetToWin_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL, "MAME Bridge NetToWin is already running.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASS wcB = { 0 }; wcB.lpszClassName = BRIDGE_WINDOW_CLASS; wcB.lpfnWndProc = BridgeWndProc; wcB.hInstance = hInstance; RegisterClass(&wcB);
    WNDCLASS wcG = { 0 }; wcG.lpszClassName = GUI_WINDOW_CLASS; wcG.lpfnWndProc = GUIWndProc; wcG.hInstance = hInstance; wcG.hIcon = LoadIcon(hInstance, "EXE_ICON"); wcG.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClass(&wcG);

    g_hwndBridge = CreateWindow(BRIDGE_WINDOW_CLASS, "Bridge", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    g_hwndGUI = CreateWindow(GUI_WINDOW_CLASS, TOOL_NAME, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 600, 400, NULL, NULL, hInstance, NULL);

    g_nid.cbSize = sizeof(NOTIFYICONDATA); g_nid.hWnd = g_hwndGUI; g_nid.uID = ID_TRAY_APP_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; g_nid.uCallbackMessage = WM_SHELLNOTIFY;
    g_nid.hIcon = wcG.hIcon; strcpy(g_nid.szTip, TOOL_NAME); Shell_NotifyIcon(NIM_ADD, &g_nid);

    om_mame_start = RegisterWindowMessage("MAMEOutputStart");
    om_mame_stop = RegisterWindowMessage("MAMEOutputStop");
    om_mame_update_state = RegisterWindowMessage("MAMEOutputUpdateState");
    om_mame_register_client = RegisterWindowMessage("MAMEOutputRegister");
    om_mame_unregister_client = RegisterWindowMessage("MAMEOutputUnregister");
    om_mame_get_id_string = RegisterWindowMessage("MAMEOutputGetIDString");

    std::thread netThread(NetworkThread);
    netThread.detach();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    
    ReleaseMutex(hMutex); CloseHandle(hMutex);
    return 0;
}
