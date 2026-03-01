#include "globals.h"
#include "g_memory.h"
#include <vector>

void ApplyFovHooks();

template <typename T>
void Push(std::vector<BYTE>& sc, T value) {
    BYTE* ptr = (BYTE*)&value;
    sc.insert(sc.end(), ptr, ptr + sizeof(T));
}

void Add(std::vector<BYTE>& sc, std::initializer_list<BYTE> bytes) {
    sc.insert(sc.end(), bytes.begin(), bytes.end());
}

void BuildFovShellcode() {
    if (!Config::AddrFovCodecave) return;

    std::vector<BYTE> sc;

    Add(sc, { 0x50 });

    Add(sc, { 0xB8 }); Push(sc, Config::MaxFOV);
    Add(sc, { 0x66, 0x44, 0x0F, 0x6E, 0xC0 });

    Add(sc, { 0xB8 }); Push(sc, Config::MinFOV);
    Add(sc, { 0x66, 0x0F, 0x6E, 0xF8 });

    Add(sc, { 0x58 });

    for (int i = 0; i < 9; i++) {
        sc.push_back(Config::OrigBytes[i]);
    }

    Add(sc, { 0xE9 });
    size_t jmp_back = sc.size(); Push(sc, (uint32_t)0);

    uintptr_t retAddr = Config::AddrFovHook + 9;
    int32_t rJmpBack = (int32_t)(retAddr - (Config::AddrFovCodecave + jmp_back + 4));
    memcpy(&sc[jmp_back], &rJmpBack, 4);

    DWORD oP;
    VirtualProtect((void*)Config::AddrFovCodecave, sc.size(), PAGE_EXECUTE_READWRITE, &oP);
    memcpy((void*)Config::AddrFovCodecave, sc.data(), sc.size());
    VirtualProtect((void*)Config::AddrFovCodecave, sc.size(), oP, &oP);
}

void ApplyFovHooks() {
    Config::AddrFovHook = FindPattern("NMS.exe", Config::SigFovLimits);

    if (!Config::AddrFovHook) {
        return;
    }

    memcpy(Config::OrigBytes, (void*)Config::AddrFovHook, 9);

    Config::AddrFovCodecave = (uintptr_t)AllocateNearAddress(Config::AddrFovHook, 0x1000);
    if (!Config::AddrFovCodecave) return;

    BuildFovShellcode();

    int32_t jmpToCodecave = (int32_t)(Config::AddrFovCodecave - Config::AddrFovHook - 5);
    memcpy(&Config::PatchFovJmp[1], &jmpToCodecave, 4);

    DWORD oP;
    VirtualProtect((void*)Config::AddrFovHook, 9, PAGE_EXECUTE_READWRITE, &oP);
    memcpy((void*)Config::AddrFovHook, Config::PatchFovJmp, 9);
    VirtualProtect((void*)Config::AddrFovHook, 9, oP, &oP);
}

unsigned int __stdcall ModThread(void*) {
    ApplyFovHooks();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)ModThread, nullptr, 0, nullptr);
    }
    return TRUE;
}