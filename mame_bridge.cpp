// license: BSD-3-Clause
// copyright-holders: Jacob Simpson

// MAME Bridge
// Version 1.0.0
// Designed to bridge the gap between network and windows output in MAME and enable simultaneous output

// Compile with: g++ mame_bridge.cpp -o mame_bridge.exe -lws2_32 -static

#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

// --- CONFIGURATION ---
#define SERVER_PORT 8000
#define MAME_WINDOW_CLASS TEXT("MAMEOutput")

// --- GLOBALS ---
HWND g_hwndMAME = NULL;
HWND g_hwndBridge = NULL;
SOCKET g_clientSocket = INVALID_SOCKET;

// Message IDs
UINT om_mame_start;
UINT om_mame_stop;
UINT om_mame_update_state;
UINT om_mame_register_client;
UINT om_mame_unregister_client;
UINT om_mame_get_id_string;

// ID to Name Cache
std::map<LPARAM, std::string> g_idToName;

// --- NETWORK HELPER ---
void SendToNetwork(std::string msg) {
    if (g_clientSocket != INVALID_SOCKET) {
        send(g_clientSocket, msg.c_str(), msg.length(), 0);
    }
}

// --- NETWORK SERVER THREAD ---
void NetworkThread() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(SERVER_PORT);

    bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, 1);

    std::cout << "[NET] Listening on Port " << SERVER_PORT << "..." << std::endl;

    while (true) {
        SOCKET client = accept(listenSocket, NULL, NULL);
        if (client != INVALID_SOCKET) {
            std::cout << "[NET] Client Connected!" << std::endl;
            g_clientSocket = client;
            
            // Wait for disconnect
            char buffer[1024];
            while (recv(client, buffer, sizeof(buffer), 0) > 0) {
                // We could handle incoming "pause" commands here later if needed
            }
            
            std::cout << "[NET] Client Disconnected." << std::endl;
            closesocket(client);
            g_clientSocket = INVALID_SOCKET;
        }
    }
}

// --- WINDOWS MESSAGE HANDLER ---
LRESULT CALLBACK BridgeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == om_mame_update_state) {
        LPARAM id = wParam;
        LPARAM value = lParam;

        // Do we know this ID?
        if (g_idToName.find(id) == g_idToName.end()) {
            // No: Ask MAME for the name
            // We use the same specific COPYDATA logic MAME expects
            PostMessage(g_hwndMAME, om_mame_get_id_string, (WPARAM)g_hwndBridge, id);
        } else {
            // Yes: Relay to Network
            std::string outputName = g_idToName[id];
            std::string netMsg = outputName + " = " + std::to_string(value) + "\r";
            SendToNetwork(netMsg);
            
            // Optional: Print to console
            // std::cout << "[OUT] " << netMsg << "\n"; 
        }
        return 0;
    }
    else if (msg == WM_COPYDATA) {
        // MAME sent us a Name for an ID
        PCOPYDATASTRUCT pCopyData = (PCOPYDATASTRUCT)lParam;
        if (pCopyData->dwData == 1) { // COPYDATA_MESSAGE_ID_STRING
            struct copydata_id_string {
                uint32_t id;
                char string[1];
            } *pData = (copydata_id_string*)pCopyData->lpData;

            std::string name = pData->string;
            g_idToName[pData->id] = name;
            
            std::cout << "[MAP] ID " << pData->id << " -> " << name << std::endl;
        }
        return 0;
    }
    else if (msg == om_mame_start) {
        std::cout << "[WIN] MAME Started! Registering..." << std::endl;
        g_hwndMAME = (HWND)wParam;
        PostMessage(g_hwndMAME, om_mame_register_client, (WPARAM)g_hwndBridge, 12345); // 12345 = Our Client ID
        return 0;
    }
    else if (msg == om_mame_stop) {
        std::cout << "[WIN] MAME Stopped." << std::endl;
        g_hwndMAME = NULL;
        g_idToName.clear();
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// --- MAIN ---
int main() {
    // 1. Setup Network Thread
    std::thread netThread(NetworkThread);
    netThread.detach();

    // 2. Setup Windows Class
    WNDCLASS wc = { 0 };
    wc.lpszClassName = TEXT("MameBridge");
    wc.lpfnWndProc = BridgeWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    RegisterClass(&wc);

    // 3. Create Invisible Listener Window
    g_hwndBridge = CreateWindow(TEXT("MameBridge"), TEXT("Bridge"), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);

    // 4. Register MAME Messages
    om_mame_start = RegisterWindowMessage(TEXT("MAMEOutputStart"));
    om_mame_stop = RegisterWindowMessage(TEXT("MAMEOutputStop"));
    om_mame_update_state = RegisterWindowMessage(TEXT("MAMEOutputUpdateState"));
    om_mame_register_client = RegisterWindowMessage(TEXT("MAMEOutputRegister"));
    om_mame_unregister_client = RegisterWindowMessage(TEXT("MAMEOutputUnregister"));
    om_mame_get_id_string = RegisterWindowMessage(TEXT("MAMEOutputGetIDString"));

    std::cout << "Waiting for MAME..." << std::endl;

    // 5. Check if MAME is already running
    g_hwndMAME = FindWindow(MAME_WINDOW_CLASS, MAME_WINDOW_CLASS);
    if (g_hwndMAME) {
        std::cout << "[WIN] Found MAME! Registering..." << std::endl;
        PostMessage(g_hwndMAME, om_mame_register_client, (WPARAM)g_hwndBridge, 12345);
    }

    // 6. Message Loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
