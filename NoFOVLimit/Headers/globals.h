#pragma once
#include <windows.h>
#include <cstdint>

namespace Config {
    inline float MinFOV = 1.0f;
    inline float MaxFOV = 240.0f;

    inline const char* SigFovLimits = "F3 41 0F 10 ?? ?? ?? ?? ?? 45 33 ?? F3 45 0F 10 ?? ??";
    
    inline uintptr_t AddrFovHook = 0;
    inline uintptr_t AddrFovCodecave = 0;

    inline unsigned char OrigBytes[9] = { 0 }; 
    inline unsigned char PatchFovJmp[9] = { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90 };
}