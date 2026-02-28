#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace Config {
    inline std::atomic<float> CustomX = 6.39f;
    inline std::atomic<float> CustomHeight = -33.0f;
    inline std::atomic<float> CustomDist = -49.0f;
    inline float Step = 0.18f;
    inline std::atomic<bool> EnableCameraSmoothing = false;

    inline int ToggleKey = VK_NUMPAD7;
    inline int IncDistKey = VK_ADD;
    inline int DecDistKey = VK_SUBTRACT;
    inline int IncWidthKey = VK_RIGHT;
    inline int DecWidthKey = VK_LEFT;
    inline int IncHeightKey = VK_UP;
    inline int DecHeightKey = VK_DOWN;

    inline uint32_t StructStride = 0x108;
    inline int CopiesXYZ = 2;
    inline int CopiesSmoothing = 2;
    inline int CopiesCollision = 2;

    inline uint32_t OffsetDist = 0x498;
    inline uint32_t OffsetX = 0x4F4;
    inline uint32_t OffsetHeight = 0x4F8;
    inline uint32_t OffsetCameraSmoothing = 0x210;
    inline uint32_t OffsetSprintCameraS = 0x494;
    inline uint32_t OffsetCollision = 0x4A0;

    inline const char* SigCamera = "F3 0F 11 83 98 04 00 00 F3 0F 10 83 ?? 06";
    inline const char* SigCollisionRead1 = "F3 0F 59 83 A0 04 00 00";
    inline const char* SigCollisionRead2 = "F3 0F 59 8B A0 04 00 00";
    inline const char* SigCameraSmoothing2 = "F3 0F 11 9E 10 02 00 00";

    inline std::atomic<bool> bModActive = true;
    inline std::atomic<bool> bInitialized = false;
    inline uintptr_t GlobalCameraPtr = 0;
    inline uintptr_t AddrCameraHook = 0;
    inline uintptr_t AddrCodecave = 0;
    inline uintptr_t AddrCollision1 = 0;
    inline uintptr_t AddrCollision2 = 0;
    inline uintptr_t AddrCameraSmoothing2 = 0;

    inline unsigned char OrigCamera[8] = { 0xF3, 0x0F, 0x11, 0x83, 0x98, 0x04, 0x00, 0x00 };
    inline unsigned char OrigCollision1[8] = { 0xF3, 0x0F, 0x59, 0x83, 0xA0, 0x04, 0x00, 0x00 };
    inline unsigned char OrigCollision2[8] = { 0xF3, 0x0F, 0x59, 0x8B, 0xA0, 0x04, 0x00, 0x00 };

    inline unsigned char PatchXorpsXmm0[8] = { 0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90 };
    inline unsigned char PatchXorpsXmm1[8] = { 0x0F, 0x57, 0xC9, 0x90, 0x90, 0x90, 0x90, 0x90 };
    inline unsigned char PatchCameraJmp[8] = { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90 };

    inline unsigned char OrigCameraSmoothing2[8] = { 0xF3, 0x0F, 0x11, 0x9E, 0x10, 0x02, 0x00, 0x00 };
    inline unsigned char PatchNop8[8] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
}