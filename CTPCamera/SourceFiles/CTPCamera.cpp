#include <windows.h>
#include <iostream>
#include <cstdio>
#include <string>
#include "config_file.h"
#include "camera_mod.h"
#include "camera_ui.h"
#include "globals.h"
#include "logger.h"
#include "camera_mod_veh.h"

static bool IsMicrosoftStoreVersion() {
    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exePath, MAX_PATH)) return false;
    std::string dir = exePath;
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos + 1);

    const char* msDlls[] = {
        "PartyXboxLive.dll",
        "PlayFabCore.GDK.dll",
        "PlayFabMultiplayerGDK.dll",
        "PlayFabServices.GDK.dll"
    };

    for (const char* dll : msDlls) {
        std::string fullPath = dir + dll;
        DWORD attr = GetFileAttributesA(fullPath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            return true;
        }
        if (GetModuleHandleA(dll) != nullptr) {
            return true;
        }
    }
    return false;
}

static void CreateDebugConsole() {
    if (!Config::EnableConsole) return;

    if (AllocConsole()) {
        FILE* fp;
        freopen_s(&fp, "NUL", "w", stdout);
        freopen_s(&fp, "NUL", "w", stderr);
        freopen_s(&fp, "NUL", "r", stdin);

        std::ios::sync_with_stdio(true);
        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();

        SetConsoleTitleA("NMS CTPCamera Debug Console");
    }
}

static unsigned int __stdcall ModThread(void*) {
    CreateDebugConsole();
    Logger::Initialize();

    if (IsMicrosoftStoreVersion()) {
        std::cout << "init: microsoft store version detected, not supported for now\n";
        return 0;
    }

    std::cout << "init: loading configuration\n";
    LoadConfig();

    std::cout << "hooks: patching camera bytecode\n";
    ApplyHooks();

    if (!VehicleCamera::ApplyHooks()) {
        std::cout << "vehicle: hooks failed to apply\n";
    }

    std::cout << "ui: hooking camera menu\n";
    if (!CameraUI::Initialize()) {
        std::cout << "ui: failed to hook camera menu\n";
    }

    Config::bInitialized.store(true);
    std::cout << "main: entering key poll loop\n\n";
    KeyPollLoop();

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)ModThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        Config::bInitialized.store(false);
        Logger::Shutdown();
    }
    return TRUE;
}