#include "g_memory.h"
#include <string>
#include <sstream>
#include <cstring>

void ParseSignature(const char* sig, std::vector<int>& bytes) {
    std::stringstream ss(sig);
    std::string t;
    while (ss >> t) bytes.push_back((t == "??" || t == "?") ? -1 : std::stoul(t, nullptr, 16));
}

uintptr_t FindPattern(const char* mod, const char* sig) {
    HMODULE hMod = GetModuleHandleA(mod);
    if (!hMod) return 0;

    PIMAGE_DOS_HEADER dH = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS ntH = (PIMAGE_NT_HEADERS)((uint8_t*)hMod + dH->e_lfanew);
    DWORD imgSize = ntH->OptionalHeader.SizeOfImage;

    std::vector<int> pat;
    ParseSignature(sig, pat);
    uint8_t* scan = (uint8_t*)hMod;

    for (DWORD i = 0; i < imgSize - pat.size(); ++i) {
        bool found = true;
        for (size_t j = 0; j < pat.size(); ++j) {
            if (pat[j] != -1 && scan[i + j] != pat[j]) { found = false; break; }
        }
        if (found) return (uintptr_t)&scan[i];
    }
    return 0;
}

void* AllocateNearAddress(uintptr_t target, size_t size) {
    SYSTEM_INFO sI;
    GetSystemInfo(&sI);
    uint64_t pSize = sI.dwPageSize;

    uint64_t start = (target & ~(pSize - 1));
    uint64_t minA = max(start - 0x7FFFFF00, (uint64_t)sI.lpMinimumApplicationAddress);
    uint64_t maxA = min(start + 0x7FFFFF00, (uint64_t)sI.lpMaximumApplicationAddress);

    for (uint64_t a = start; a > minA; a -= pSize) {
        if (void* alloc = VirtualAlloc((void*)a, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) return alloc;
    }
    for (uint64_t a = start; a < maxA; a += pSize) {
        if (void* alloc = VirtualAlloc((void*)a, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) return alloc;
    }
    return nullptr;
}

uintptr_t FindString(const char* moduleName, const char* str) {
    HMODULE hMod = GetModuleHandleA(moduleName);
    if (!hMod || !str) return 0;

    PIMAGE_DOS_HEADER dH = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS ntH = (PIMAGE_NT_HEADERS)((uint8_t*)hMod + dH->e_lfanew);
    DWORD imgSize = ntH->OptionalHeader.SizeOfImage;

    size_t strLen = strlen(str) + 1;
    uint8_t* scan = (uint8_t*)hMod;

    for (DWORD i = 0; i < imgSize - strLen; ++i) {
        if (memcmp(&scan[i], str, strLen) == 0) {
            return (uintptr_t)&scan[i];
        }
    }
    return 0;
}

uintptr_t FindRipRef(const char* moduleName, uintptr_t targetAddr, uint8_t regOpcode) {
    HMODULE hMod = GetModuleHandleA(moduleName);
    if (!hMod || !targetAddr) return 0;

    PIMAGE_DOS_HEADER dH = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS ntH = (PIMAGE_NT_HEADERS)((uint8_t*)hMod + dH->e_lfanew);
    DWORD imgSize = ntH->OptionalHeader.SizeOfImage;

    uint8_t* scan = (uint8_t*)hMod;

    for (DWORD i = 0; i < imgSize - 7; ++i) {
        // lea reg, [rip+disp]
        if (scan[i] == 0x48 && scan[i + 1] == 0x8D && scan[i + 2] == regOpcode) {
            int32_t disp = *reinterpret_cast<int32_t*>(&scan[i + 3]);
            uintptr_t ripAfter = (uintptr_t)&scan[i + 7];
            if ((ripAfter + disp) == targetAddr) {
                return (uintptr_t)&scan[i];
            }
        }
    }
    return 0;
}

uintptr_t ResolveCallTarget(uintptr_t callInstructionAddr) {
    if (!callInstructionAddr || *reinterpret_cast<uint8_t*>(callInstructionAddr) != 0xE8) {
        return 0;
    }
    int32_t disp = *reinterpret_cast<int32_t*>(callInstructionAddr + 1);
    return (callInstructionAddr + 5) + disp;
}

uintptr_t FindNthCallForward(uintptr_t startAddr, size_t maxScanBytes, size_t targetCallIndex) {
    if (!startAddr) return 0;

    uint8_t* code = reinterpret_cast<uint8_t*>(startAddr);
    size_t callCount = 0;

    for (size_t i = 0; i < maxScanBytes; ++i) {
        if (code[i] == 0xE8) {
            ++callCount;
            if (callCount == targetCallIndex) {
                return reinterpret_cast<uintptr_t>(&code[i]);
            }
        }
    }
    return 0;
}