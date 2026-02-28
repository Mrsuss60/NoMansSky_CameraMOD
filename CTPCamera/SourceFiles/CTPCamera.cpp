#include <windows.h>
#include <thread>
#include "config_file.h"
#include "camera_mod.h"
#include "globals.h"

static unsigned int __stdcall ModThread(void*) {
    LoadConfig();
    ApplyHooks();
    Config::bInitialized.store(true);
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
    }
    return TRUE;
}