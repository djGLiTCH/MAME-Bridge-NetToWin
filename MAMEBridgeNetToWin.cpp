// license: BSD-3-Clause
// copyright-holders: Jacob Simpson

// ==================================================================================
//                               MAME BRIDGE NET-TO-WIN
// ==================================================================================
// Version: 3.6.0
// Author:  DJ GLiTCH
// GitHub:  https://github.com/djGLiTCH/MAME-Bridge-NetToWin
//
// DESCRIPTION:
// This tool acts as a "Translation Bridge" for MAME.
// 1. It listens to MAME's TCP Network Output (default 127.0.0.1:8000).
// 2. It translates those network commands into native Windows Output Messages.
// 3. It broadcasts those messages to other apps (like LEDBlinky) pretending to be MAME.
//
// WHY IS THIS NEEDED?
// MAME can usually only output to Network OR Windows, not both simultaneously.
// This tool allows you to set MAME to "Network", while still allowing "Windows"
// clients (like LEDBlinky) to work by simulating the Windows output signals.
// ==================================================================================

// Compile with MSYS2 MINGW64:
// Step 1: windres bridge.rc -o bridge.o
// Step 2: g++ MAMEBridgeNetToWin.cpp bridge.o -o MAME-Bridge-NetToWin.exe -lws2_32 -mwindows -static

#define _WIN32_WINNT 0x0600 // Target Windows Vista or newer
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
#include <algorithm>
#include <cctype>

// Link against required Windows libraries for Sockets and GUI controls
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")

// --- CONFIGURATION ---
#define MAME_IP "127.0.0.1"
#define MAME_PORT 8000
#define BRIDGE_WINDOW_CLASS "MAMEOutput"  // CRITICAL: LEDBlinky looks for this specific class name!
#define GUI_WINDOW_CLASS "NetToWinGUI"    // Class name for our visible log window
#define WM_SHELLNOTIFY (WM_USER + 1)      // Custom message for Tray Icon events
#define WM_APPEND_LOG  (WM_USER + 2)      // Custom message for thread-safe logging

// Tray Icon Menu IDs
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT     1002
#define ID_TRAY_SHOW     1003
#define ID_TRAY_ABOUT    1004
#define ID_TRAY_GITHUB   1005
#define ID_TRAY_AUTOSTART 1006

// Info Strings
#define TOOL_NAME "MAME Bridge NetToWin"
#define TOOL_VERSION "3.6.0"
#define TOOL_AUTHOR "DJ GLiTCH"
#define GITHUB_LINK "https://github.com/djGLiTCH/MAME-Bridge-NetToWin"
#define REG_RUN_PATH "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define REG_APP_NAME "MAMEBridgeNetToWin"

// --- GLOBALS ---
HWND g_hwndGUI = NULL;      // Handle to the visible Log Window
HWND g_hwndBridge = NULL;   // Handle to the hidden "Bridge" Window (Impersonates MAME)
HWND g_hLogCtrl = NULL;     // Handle to the text box inside the log window
NOTIFYICONDATA g_nid;       // Struct for the System Tray Icon
std::atomic<bool> g_running(true); // Flag to control the Network Thread loop
std::vector<HWND> g_clients;       // List of connected clients (e.g. LEDBlinky)

// --- ID MAPPING ---
// MAME uses integer IDs for outputs (e.g., ID 10 = "lamp0").
// Since we don't know MAME's internal IDs, we generate our own on the fly.
std::map<std::string, LPARAM> g_nameToID; // Maps "lamp0" -> 1
std::map<LPARAM, std::string> g_idToName; // Maps 1 -> "lamp0"
LPARAM g_nextID = 1;
std::string g_currentRomName = "___empty"; // Stores current game name (e.g., "pacman")

// --- WINDOWS MESSAGE IDS ---
// These are special unique IDs registered at runtime.
// They match the exact strings used by MAME's native output system.
UINT om_mame_start;
UINT om_mame_stop;
UINT om_mame_update_state;
UINT om_mame_register_client;
UINT om_mame_unregister_client;
UINT om_mame_get_id_string;

// ==================================================================================
//                                  HELPER FUNCTIONS
// ==================================================================================

// Loads the "description.txt" file embedded in the .exe resource
std::string LoadDescriptionFromResource() {
    HRSRC hRes = FindResource(NULL, "DESCRIPTION_TEXT", RT_RCDATA);
    if (!hRes) return "Error: Description resource not found.";
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return "Error: Could not load description.";
    const char* data = (const char*)LockResource(hData);
    DWORD size = SizeofResource(NULL, hRes);
    return std::string(data, size);
}

// Checks if the app is set to run at Windows Startup
bool IsAutostartEnabled() {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[MAX_PATH];
        DWORD size = sizeof(buffer);
        DWORD type = REG_SZ;
        LONG result = RegQueryValueEx(hKey, REG_APP_NAME, NULL, &type, (LPBYTE)buffer, &size);
        RegCloseKey(hKey);
        return (result == ERROR_SUCCESS);
    }
    return false;
}

// Toggles the registry key for Autostart
void ToggleAutostart() {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        if (IsAutostartEnabled()) {
            RegDeleteValue(hKey, REG_APP_NAME); // Remove
        } else {
            char exePath[MAX_PATH];
            GetModuleFileName(NULL, exePath, MAX_PATH);
            RegSetValueEx(hKey, REG_APP_NAME, 0, REG_SZ, (LPBYTE)exePath, strlen(exePath) + 1); // Add
        }
        RegCloseKey(hKey);
    }
}

// Thread-safe logging helper. Sends text to the GUI thread to display.
void Log(const std::string& msg) {
    if (g_hwndGUI) {
        std::string* pMsg = new std::string(msg);
        PostMessage(g_hwndGUI, WM_APPEND_LOG, 0, (LPARAM)pMsg);
    }
}

// Manages unique IDs for output names.
// If "lamp0" is seen for the first time, it gets a new ID (e.g. 1).
// If "lamp0" is seen again, it returns the existing ID (1).
LPARAM GetIDForName(const std::string& name) {
    if (g_nameToID.find(name) == g_nameToID.end()) {
        LPARAM newID = g_nextID++;
        g_nameToID[name] = newID;
        g_idToName[newID] = name;
        
        // Only log new items (ID < 1000 prevents startup spam if IDs reset)
        if (newID < 1000) { 
            std::stringstream ss;
            ss << "[MAP] New Output: '" << name << "' -> ID " << newID;
            Log(ss.str());
        }
        return newID;
    }
    return g_nameToID[name];
}

// ==================================================================================
//                            BRIDGE WINDOW PROCEDURE (HIDDEN)
// ==================================================================================
// This hidden window listens for messages from clients (LEDBlinky).
// It mimics the behavior of the official MAME Output Window.
LRESULT CALLBACK BridgeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    
    // Client wants to register (e.g. LEDBlinky starting up)
    if (msg == om_mame_register_client) {
        HWND client = (HWND)wParam;
        g_clients.push_back(client);
        Log("[WIN] Client Registered!");
        
        // NOTE: We do NOT send "mame_start" here anymore.
        // Sending "start" immediately after "register" causes LEDBlinky to 
        // register again, creating an infinite loop.
        return 1;
    }
    
    // Client is closing
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
    
    // Client asks: "What is the name for ID X?"
    else if (msg == om_mame_get_id_string) {
        LPARAM id = (LPARAM)lParam; // The ID they are asking about
        std::string name = "";

        // ID 0 is RESERVED for the Game Name (e.g. "pacman")
        if (id == 0) name = g_currentRomName;
        // Any other ID is looked up in our map
        else if (g_idToName.count(id)) name = g_idToName[id];

        // We must reply using a WM_COPYDATA message structure.
        // This is exactly how MAME native output works.
        struct copydata_id_string { uint32_t id; char string[1]; };
        int dataLen = sizeof(copydata_id_string) + name.length() + 1;
        std::vector<uint8_t> buffer(dataLen);
        copydata_id_string* pData = (copydata_id_string*)buffer.data();
        pData->id = (uint32_t)id;
        strcpy(pData->string, name.c_str());

        COPYDATASTRUCT copyData = { 1, (DWORD)dataLen, pData };
        // Reply to the client window (stored in wParam)
        SendMessage((HWND)wParam, WM_COPYDATA, (WPARAM)g_hwndBridge, (LPARAM)&copyData);
        return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ==================================================================================
//                              GUI WINDOW PROCEDURE (VISIBLE)
// ==================================================================================
// Handles the Visible Log Window, System Tray Icon, and Right-Click Menu.
LRESULT CALLBACK GUIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        // Create the text box for logs
        g_hLogCtrl = CreateWindowEx(0, "EDIT", "", 
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 
            0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_hLogCtrl, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), 0);
        Log(std::string(TOOL_NAME) + " - Version " + std::string(TOOL_VERSION));
        break;

    case WM_SIZE:
        // Handle minimize to tray
        if (wParam == SIZE_MINIMIZED) ShowWindow(hwnd, SW_HIDE);
        else {
            RECT rc; GetClientRect(hwnd, &rc);
            MoveWindow(g_hLogCtrl, 0, 0, rc.right, rc.bottom, TRUE);
        }
        break;

    case WM_CLOSE: 
        // Don't close app on X, just hide to tray
        ShowWindow(hwnd, SW_HIDE); 
        return 0;

    case WM_SHELLNOTIFY:
        // Handle Tray Icon Clicks
        if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            
            // Determine if Autostart is checked
            UINT flags = MF_STRING;
            if (IsAutostartEnabled()) flags |= MF_CHECKED;
            
            // Build Menu
            AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW, "Show Logs");
            AppendMenu(hMenu, flags, ID_TRAY_AUTOSTART, "Autostart");
            AppendMenu(hMenu, MF_STRING, ID_TRAY_ABOUT, "About");
            AppendMenu(hMenu, MF_STRING, ID_TRAY_GITHUB, "GitHub");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");
            
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            
            // Handle Selection
            if (cmd == ID_TRAY_EXIT) DestroyWindow(hwnd);
            if (cmd == ID_TRAY_SHOW) { ShowWindow(hwnd, SW_SHOW); ShowWindow(hwnd, SW_RESTORE); }
            if (cmd == ID_TRAY_GITHUB) ShellExecute(0, 0, GITHUB_LINK, 0, 0, SW_SHOW);
            if (cmd == ID_TRAY_AUTOSTART) ToggleAutostart();
            
            if (cmd == ID_TRAY_ABOUT) {
                std::string desc = LoadDescriptionFromResource();
                std::string msg = std::string(TOOL_NAME) + "\nVersion: " + std::string(TOOL_VERSION) + 
                                  "\nAuthor: " + std::string(TOOL_AUTHOR) + "\n\n" + desc + 
                                  "\n\nGitHub: " + std::string(GITHUB_LINK);
                MessageBox(hwnd, msg.c_str(), "About", MB_ICONINFORMATION | MB_OK);
            }
            DestroyMenu(hMenu);
        }
        else if (lParam == WM_LBUTTONDBLCLK) { 
            ShowWindow(hwnd, SW_SHOW); 
            ShowWindow(hwnd, SW_RESTORE); 
        }
        break;

    case WM_APPEND_LOG: {
        // Safe way to update UI from the Network Thread
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

// ==================================================================================
//                              NETWORK PACKET PARSER
// ==================================================================================

// Helper: Remove invisible chars, quotes, and whitespace artifacts
std::string CleanString(std::string input) {
    std::string output = "";
    for (char c : input) {
        if (isalnum(c) || c == '_' || c == '.') {
            output += c;
        }
    }
    return output;
}

// Parses a single line from MAME (e.g., "mame_start = pacman" or "lamp0 = 1")
void ProcessLine(std::string line) {
    // Debug: Log Raw Line (Optional)
    if (line.length() > 0) Log("RAW: " + line);

    // Basic Trim
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) return;

    size_t eqPos = line.find("=");
    if (eqPos != std::string::npos) {
        std::string rawName = line.substr(0, eqPos);
        std::string rawVal = line.substr(eqPos + 1);

        // CLEANING: Strip quotes and garbage characters
        std::string name = CleanString(rawName);
        std::string valStr = CleanString(rawVal);
        
        // LOGIC: Check Command Type

        // 1. GAME START
        if (name == "mame_start") {
            g_currentRomName = valStr;
            g_idToName[0] = g_currentRomName;
            Log("[SYS] MAME Started. ROM: " + g_currentRomName);
            // Broadcast START so clients know the game name changed
            PostMessage(HWND_BROADCAST, om_mame_start, (WPARAM)g_hwndBridge, 0);
            return;
        }

        // 2. MAME STOP (Ignore this command data)
        // MAME sends "mame_stop = 1" on exit. We don't map this to an ID.
        // We handle the stop event via socket disconnect instead.
        if (name == "mame_stop") return;

        // 3. GAME OUTPUT (e.g. lamp0, led1)
        int val = std::atoi(valStr.c_str());
        LPARAM id = GetIDForName(name);
        
        // Forward state change to all connected clients (LEDBlinky)
        for (HWND client : g_clients) {
            PostMessage(client, om_mame_update_state, (WPARAM)id, (LPARAM)val);
        }
    }
}

// ==================================================================================
//                                  NETWORK THREAD
// ==================================================================================
// This runs in the background, connecting to MAME via TCP and reading data.
void NetworkThread() {
    Log("[SYS] Network Thread Started. Waiting for MAME...");
    
    while (g_running) {
        // Initialize Winsock
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server = { AF_INET, htons(MAME_PORT) };
        server.sin_addr.s_addr = inet_addr(MAME_IP);

        // Attempt Connection
        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == 0) {
            Log("[NET] Connected to MAME!");

            // 1. RESET STATE
            // Reset to defaults so clients are clean
            g_currentRomName = "___empty"; 
            g_idToName[0] = "___empty";    

            // 2. FORCE START
            // Tell Windows Clients we are live immediately (fixes LEDBlinky attach issues)
            PostMessage(HWND_BROADCAST, om_mame_start, (WPARAM)g_hwndBridge, 0);
            Log("[SYS] Sent Force Start Signal (___empty).");

            // 3. WAKE UP MAME
            // Send a newline to MAME to ensure it sends the initial state
            const char* wakeUp = "\r\n";
            send(sock, wakeUp, 2, 0);

            // 4. READ LOOP
            char buffer[4096];
            std::string netBuffer = ""; // Persistent buffer for fragmented packets
            int n;
            
            while ((n = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
                netBuffer.append(buffer, n);
                size_t pos = 0;
                
                // CRITICAL: MAME uses '\r' (Carriage Return) as a line terminator, NOT '\n'.
                // We must split on '\r' to correctly process messages.
                while ((pos = netBuffer.find('\r')) != std::string::npos) {
                    std::string line = netBuffer.substr(0, pos);
                    ProcessLine(line);
                    netBuffer.erase(0, pos + 1);
                }
            }
            
            // 5. DISCONNECT & CLEANUP
            Log("[NET] Disconnected from MAME.");
            
            // Send STOP to clients so they turn off lights
            PostMessage(HWND_BROADCAST, om_mame_stop, (WPARAM)g_hwndBridge, 0);
            
            // Clear ID maps for next run
            g_currentRomName = "___empty";
            g_nameToID.clear();
            g_idToName.clear();
            g_nextID = 1;

        } else {
            // If connection fails, wait 2 seconds and retry
            Sleep(2000);
        }
        
        // Clean up socket
        closesocket(sock);
        WSACleanup();
    }
}

// ==================================================================================
//                                MAIN ENTRY POINT
// ==================================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    
    // 0. SINGLE INSTANCE CHECK
    // Ensure only one copy of this tool runs at a time using a named Mutex.
    HANDLE hMutex = CreateMutex(NULL, TRUE, "Global\\MAMEBridgeNetToWin_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL, "MAME Bridge NetToWin is already running.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 1. REGISTER WINDOW CLASSES
    WNDCLASS wcB = { 0 }; wcB.lpszClassName = BRIDGE_WINDOW_CLASS; wcB.lpfnWndProc = BridgeWndProc; wcB.hInstance = hInstance; RegisterClass(&wcB);
    WNDCLASS wcG = { 0 }; wcG.lpszClassName = GUI_WINDOW_CLASS; wcG.lpfnWndProc = GUIWndProc; wcG.hInstance = hInstance; wcG.hIcon = LoadIcon(hInstance, "EXE_ICON"); wcG.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClass(&wcG);

    // 2. CREATE WINDOWS
    g_hwndBridge = CreateWindow(BRIDGE_WINDOW_CLASS, "Bridge", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    g_hwndGUI = CreateWindow(GUI_WINDOW_CLASS, TOOL_NAME, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 600, 400, NULL, NULL, hInstance, NULL);

    // 3. SETUP TRAY ICON
    g_nid.cbSize = sizeof(NOTIFYICONDATA); g_nid.hWnd = g_hwndGUI; g_nid.uID = ID_TRAY_APP_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; g_nid.uCallbackMessage = WM_SHELLNOTIFY;
    g_nid.hIcon = wcG.hIcon; strcpy(g_nid.szTip, TOOL_NAME); Shell_NotifyIcon(NIM_ADD, &g_nid);

    // 4. REGISTER MAME MESSAGES
    // These strings MUST match what LEDBlinky/MameHooker expect.
    om_mame_start = RegisterWindowMessage("MAMEOutputStart");
    om_mame_stop = RegisterWindowMessage("MAMEOutputStop");
    om_mame_update_state = RegisterWindowMessage("MAMEOutputUpdateState");
    om_mame_register_client = RegisterWindowMessage("MAMEOutputRegister");
    om_mame_unregister_client = RegisterWindowMessage("MAMEOutputUnregister");
    om_mame_get_id_string = RegisterWindowMessage("MAMEOutputGetIDString");

    // 5. START NETWORK THREAD
    std::thread netThread(NetworkThread);
    netThread.detach();

    // 6. MESSAGE LOOP (Keeps the app alive)
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    
    // Cleanup
    ReleaseMutex(hMutex); CloseHandle(hMutex);
    return 0;
}
