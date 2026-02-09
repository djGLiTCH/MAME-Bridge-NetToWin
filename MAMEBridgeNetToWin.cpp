// license: BSD-3-Clause
// copyright-holders: Jacob Simpson

// MAME Bridge NetToWin
// Version 2.0.0
// Designed to bridge the gap between network and windows output in MAME and enable simultaneous output.
// MAME must be set to "output network". This tool will forward all state outputs from "network" to "windows" by simulating native "windows" output in MAME.
// Only compatible with MAME 64-bit builds running on Windows

// Compile with MSYS2 MINGW64:
// Step 1: windres bridge.rc -o bridge.o
// Step 2: g++ MAMEBridgeNetToWin.cpp bridge.o -o MAME-Bridge-NetToWin.exe -lws2_32 -mwindows -static

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
#define BRIDGE_WINDOW_CLASS "MAMEOutput"  // Impersonating MAME
#define GUI_WINDOW_CLASS "NetToWinGUI"    // Our Visible Log Window
#define WM_SHELLNOTIFY (WM_USER + 1)
#define WM_APPEND_LOG  (WM_USER + 2)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT     1002
#define ID_TRAY_SHOW     1003

// --- GLOBALS ---
HWND g_hwndGUI = NULL;      // Visible Log Window
HWND g_hwndBridge = NULL;   // Hidden Message Listener
HWND g_hLogCtrl = NULL;     // Text Area
NOTIFYICONDATA g_nid;
std::atomic<bool> g_running(true);
std::vector<HWND> g_clients;

// ID MAPPING
std::map<std::string, LPARAM> g_nameToID;
std::map<LPARAM, std::string> g_idToName;
LPARAM g_nextID = 1;

// WINDOWS MESSAGE IDs
UINT om_mame_start;
UINT om_mame_stop;
UINT om_mame_update_state;
UINT om_mame_register_client;
UINT om_mame_unregister_client;
UINT om_mame_get_id_string;

// --- LOGGING HELPER ---
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
        
        std::stringstream ss;
        ss << "[MAP] New Output: '" << name << "' -> ID " << newID;
        Log(ss.str());
        return newID;
    }
    return g_nameToID[name];
}

// --- HIDDEN BRIDGE WINDOW PROCEDURE ---
LRESULT CALLBACK BridgeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == om_mame_register_client) {
        HWND client = (HWND)wParam;
        g_clients.push_back(client);
        Log("[WIN] Client Registered!");
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
        std::string name = (id == 0) ? "MAME" : (g_idToName.count(id) ? g_idToName[id] : "");

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

// --- VISIBLE GUI WINDOW PROCEDURE ---
LRESULT CALLBACK GUIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hLogCtrl = CreateWindowEx(0, "EDIT", "", 
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
            0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_hLogCtrl, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), 0);
        break;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            ShowWindow(hwnd, SW_HIDE);
        } else {
            RECT rc;
            GetClientRect(hwnd, &rc);
            MoveWindow(g_hLogCtrl, 0, 0, rc.right, rc.bottom, TRUE);
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_SHELLNOTIFY:
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW, "Show Logs");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            if (cmd == ID_TRAY_EXIT) DestroyWindow(hwnd);
            if (cmd == ID_TRAY_SHOW) {
                ShowWindow(hwnd, SW_SHOW);
                ShowWindow(hwnd, SW_RESTORE);
            }
            DestroyMenu(hMenu);
        }
        else if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
        }
        break;

    case WM_APPEND_LOG: 
        {
            std::string* pStr = (std::string*)lParam;
            std::string finalMsg = *pStr + "\r\n";
            int len = GetWindowTextLength(g_hLogCtrl);
            SendMessage(g_hLogCtrl, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            SendMessage(g_hLogCtrl, EM_REPLACESEL, 0, (LPARAM)finalMsg.c_str());
            delete pStr;
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        g_running = false;
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
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
            PostMessage(HWND_BROADCAST, om_mame_start, (WPARAM)g_hwndBridge, 0);

            char buffer[4096];
            int n;
            while ((n = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[n] = '\0';
                char* line = strtok(buffer, "\r\n");
                while (line) {
                    std::string msg = line;
                    size_t eq = msg.find("=");
                    if (eq != std::string::npos) {
                        std::string name = msg.substr(0, eq);
                        while (!name.empty() && name.back() == ' ') name.pop_back();
                        int val = std::atoi(msg.substr(eq + 1).c_str());

                        LPARAM id = GetIDForName(name);
                        for (HWND client : g_clients)
                            PostMessage(client, om_mame_update_state, (WPARAM)id, (LPARAM)val);
                    }
                    line = strtok(NULL, "\r\n");
                }
            }
            Log("[NET] Disconnected from MAME.");
        } else {
            Sleep(2000);
        }
        closesocket(sock);
        WSACleanup();
    }
}

// --- MAIN ENTRY POINT ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    // 1. Register Bridge Class
    WNDCLASS wcB = { 0 };
    wcB.lpszClassName = BRIDGE_WINDOW_CLASS;
    wcB.lpfnWndProc = BridgeWndProc;
    wcB.hInstance = hInstance;
    RegisterClass(&wcB);

    // 2. Register GUI Class
    WNDCLASS wcG = { 0 };
    wcG.lpszClassName = GUI_WINDOW_CLASS;
    wcG.lpfnWndProc = GUIWndProc;
    wcG.hInstance = hInstance;
    // LOAD ICON FROM RESOURCE
    wcG.hIcon = LoadIcon(hInstance, "EXE_ICON");
    wcG.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wcG);

    // 3. Create Windows
    g_hwndBridge = CreateWindow(BRIDGE_WINDOW_CLASS, "Bridge", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    g_hwndGUI = CreateWindow(GUI_WINDOW_CLASS, "NetToWin Bridge Logs", WS_OVERLAPPEDWINDOW, 
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 400, NULL, NULL, hInstance, NULL);

    // 4. Setup Tray Icon
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = g_hwndGUI;
    g_nid.uID = ID_TRAY_APP_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_SHELLNOTIFY;
    g_nid.hIcon = wcG.hIcon;
    strcpy(g_nid.szTip, "NetToWin Bridge");
    Shell_NotifyIcon(NIM_ADD, &g_nid);

    // 5. Register MAME Messages
    om_mame_start = RegisterWindowMessage("MAMEOutputStart");
    om_mame_stop = RegisterWindowMessage("MAMEOutputStop");
    om_mame_update_state = RegisterWindowMessage("MAMEOutputUpdateState");
    om_mame_register_client = RegisterWindowMessage("MAMEOutputRegister");
    om_mame_unregister_client = RegisterWindowMessage("MAMEOutputUnregister");
    om_mame_get_id_string = RegisterWindowMessage("MAMEOutputGetIDString");

    // 6. Start Network
    std::thread netThread(NetworkThread);
    netThread.detach();

    // 7. Loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
