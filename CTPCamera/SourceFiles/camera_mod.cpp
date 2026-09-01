#include "camera_mod.h"
#include "globals.h"
#include "globals_veh.h"
#include "camera_mod_veh.h"
#include "g_memory.h"
#include "config_file.h"
#include <vector>
#include <thread>
#include <cstring>
#include <iostream>

template <typename T>
static void Push(std::vector<BYTE>& sc, T value) {
    BYTE* ptr = (BYTE*)&value;
    sc.insert(sc.end(), ptr, ptr + sizeof(T));
}

static void Add(std::vector<BYTE>& sc, std::initializer_list<BYTE> bytes) {
    sc.insert(sc.end(), bytes.begin(), bytes.end());
}

static float ScaleToGame(float userVal, float min, float max) {
    return min + ((userVal + 50.0f) / 100.0f) * (max - min);
}

static bool IsCorvetteInteriorActive() {
    if (!Config::EnableCorvetteInteriorGuard.load()) return false;
    if (!Config::GlobalCameraPtr) return false;

    __try {
        return memcmp((void*)(Config::GlobalCameraPtr + Config::OffsetCharacterRigTag), "CHARBIGG", 8) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static std::atomic<int> g_transitionState{ 0 };
static int g_transitionStep = 0;
static float g_transStartX[4] = {};
static float g_transStartH[4] = {};
static float g_transStartD[4] = {};

static float BlendSmooth(float start, float target, float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float eased = t * t * (3.0f - 2.0f * t);
    return start + (target - start) * eased;
}

static bool HandleCameraHook(uintptr_t camera) {
    if (!camera) return false;
    Config::GlobalCameraPtr = camera;

    char currentTag[17] = { 0 };
    __try {
        memcpy(currentTag, (void*)(camera + Config::OffsetCharacterRigTag), 16);
        currentTag[16] = '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    // Vehicle/ship camera handled by camera_mod_veh.cpp
    if (memcmp(currentTag, "CHAR", 4) != 0) {
        return false;
    }

    __try {
        if (Config::EnableCorvetteInteriorGuard.load()) {
            const bool nowCorvette = memcmp(currentTag, "CHARBIGG", 8) == 0;

            if (nowCorvette) {
                g_transitionState.store(1, std::memory_order_release);
                g_transitionStep = 0;
                return false;
            }

            if (g_transitionState.load(std::memory_order_acquire) == 1) {
                for (int i = 0; i < Config::CopiesXYZ && i < 4; ++i) {
                    const uint32_t s = i * Config::StructStride;
                    g_transStartX[i] = *(float*)(camera + Config::OffsetX + s);
                    g_transStartH[i] = *(float*)(camera + Config::OffsetHeight + s);
                    g_transStartD[i] = *(float*)(camera + Config::OffsetDist + s);
                }
                g_transitionStep = 0;
                g_transitionState.store(2, std::memory_order_release);
            }
        }

        if (g_transitionState.load(std::memory_order_acquire) == 2) {
            int steps = Config::CorvetteExitTransitionSteps;
            if (steps < 1) steps = 1;

            ++g_transitionStep;
            float t = (float)g_transitionStep / (float)steps;
            if (t > 1.0f) t = 1.0f;

            const float targetX = ScaleToGame(Config::CustomX.load(std::memory_order_relaxed), -4.65f, 4.65f);
            const float targetH = ScaleToGame(Config::CustomHeight.load(std::memory_order_relaxed), -1.85f, 3.7f);
            const float targetD = ScaleToGame(Config::CustomDist.load(std::memory_order_relaxed), 1.4f, 25.0f);

            for (int i = 0; i < Config::CopiesXYZ && i < 4; ++i) {
                const uint32_t s = i * Config::StructStride;
                *(float*)(camera + Config::OffsetX + s) = BlendSmooth(g_transStartX[i], targetX, t);
                *(float*)(camera + Config::OffsetHeight + s) = BlendSmooth(g_transStartH[i], targetH, t);
                *(float*)(camera + Config::OffsetDist + s) = BlendSmooth(g_transStartD[i], targetD, t);
            }

            if (t >= 1.0f) {
                g_transitionState.store(0, std::memory_order_release);
            }

            return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Config::GlobalCameraPtr = 0;
        g_transitionState.store(0, std::memory_order_release);
        return false;
    }

    const float cWX = ScaleToGame(Config::CustomX.load(std::memory_order_relaxed), -4.65f, 4.65f);
    const float cHght = ScaleToGame(Config::CustomHeight.load(std::memory_order_relaxed), -1.85f, 3.7f);
    const float cDis = ScaleToGame(Config::CustomDist.load(std::memory_order_relaxed), 1.4f, 25.0f);

    for (int i = 0; i < Config::CopiesXYZ; ++i) {
        const uint32_t s = i * Config::StructStride;
        *(float*)(camera + Config::OffsetX + s) = cWX;
        *(float*)(camera + Config::OffsetHeight + s) = cHght;
        *(float*)(camera + Config::OffsetDist + s) = cDis;
    }

    if (!Config::EnableCameraSmoothing.load(std::memory_order_relaxed)) {
        for (int i = 0; i < Config::CopiesSmoothing; ++i) {
            const uint32_t s = i * Config::StructStride;
            *(uint32_t*)(camera + Config::OffsetCameraSmoothing + s) = 0;
            *(uint32_t*)(camera + Config::OffsetSprintCameraS + s) = 0;
        }
    }

    for (int i = 0; i < Config::CopiesCollision; ++i) {
        *(uint32_t*)(camera + Config::OffsetCollision + (i * Config::StructStride)) = 0;
    }

    return true;
}

static void ForceUpdateCamera() {
    if (!Config::GlobalCameraPtr) return;
    if (g_transitionState.load(std::memory_order_acquire) != 0) return;

    __try {
        char currentTag[17] = { 0 };
        memcpy(currentTag, (void*)(Config::GlobalCameraPtr + Config::OffsetCharacterRigTag), 16);
        currentTag[16] = '\0';

        bool keepMomentum = Config::EnableCameraSmoothing.load();

        if (memcmp(currentTag, "CHAR", 4) != 0) {
            float targetVehDist = -1.0f;
            float targetVehHeight = -999.0f;

            if (memcmp(currentTag, "MECH", 4) == 0) {
                targetVehDist = Config::CustomMechDist.load();
                targetVehHeight = Config::CustomMechHeight.load();
            }
            else if (memcmp(currentTag, "TRUC", 4) == 0) targetVehDist = Config::CustomTruckDist.load();
            else if (memcmp(currentTag, "SUBM", 4) == 0 || memcmp(currentTag, "SUB_", 4) == 0) targetVehDist = Config::CustomSubDist.load();
            else if (memcmp(currentTag, "BUGG", 4) == 0) targetVehDist = Config::CustomExoGeneralDist.load();
            else if (memcmp(currentTag, "HOVE", 4) == 0 || memcmp(currentTag, "VEHI", 4) == 0 || memcmp(currentTag, "BIKE", 4) == 0) targetVehDist = Config::CustomHoverDist.load();
            else if (memcmp(currentTag, "CORV", 4) == 0) targetVehDist = Config::CustomCorvetteDist.load();
            else if (memcmp(currentTag, "SPAC", 4) == 0 || memcmp(currentTag, "DROP", 4) == 0 ||
                memcmp(currentTag, "SCIE", 4) == 0 || memcmp(currentTag, "SHUT", 4) == 0 ||
                memcmp(currentTag, "FLAT", 4) == 0 || memcmp(currentTag, "SAIL", 4) == 0 ||
                memcmp(currentTag, "ROYA", 4) == 0 || memcmp(currentTag, "ROBO", 4) == 0 ||
                memcmp(currentTag, "ALIE", 4) == 0) {
                targetVehDist = Config::CustomShipsDist.load();
            }

            if (targetVehDist > 0.0f) {
                for (int i = 0; i < Config::CopiesXYZ; ++i) {
                    uint32_t s = i * Config::StructStride;
                    *(float*)(Config::GlobalCameraPtr + Config::OffsetDist + s) = targetVehDist;
                }
            }

            if (targetVehHeight != -999.0f) {
                for (int i = 0; i < Config::CopiesXYZ; ++i) {
                    uint32_t s = i * Config::StructStride;
                    *(float*)(Config::GlobalCameraPtr + Config::OffsetHeight + s) = targetVehHeight;
                }
            }
        }
        else {
            float cWX = ScaleToGame(Config::CustomX.load(), -4.65f, 4.65f);
            float cHght = ScaleToGame(Config::CustomHeight.load(), -1.85f, 3.7f);
            float cDis = ScaleToGame(Config::CustomDist.load(), 1.4f, 25.0f);

            for (int i = 0; i < Config::CopiesXYZ; ++i) {
                uint32_t s = i * Config::StructStride;
                *(float*)(Config::GlobalCameraPtr + Config::OffsetX + s) = cWX;
                *(float*)(Config::GlobalCameraPtr + Config::OffsetHeight + s) = cHght;
                *(float*)(Config::GlobalCameraPtr + Config::OffsetDist + s) = cDis;
            }
        }

        if (!keepMomentum) {
            for (int i = 0; i < Config::CopiesSmoothing; ++i) {
                uint32_t s = i * Config::StructStride;
                *(uint32_t*)(Config::GlobalCameraPtr + Config::OffsetCameraSmoothing + s) = 0;
                *(uint32_t*)(Config::GlobalCameraPtr + Config::OffsetSprintCameraS + s) = 0;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Config::GlobalCameraPtr = 0;
    }
}

static void UpdateSmoothingPatch() {
    if (!Config::AddrCameraSmoothing2) return;
    DWORD oP;
    VirtualProtect((void*)Config::AddrCameraSmoothing2, 8, PAGE_EXECUTE_READWRITE, &oP);
    if (Config::bModActive && !Config::EnableCameraSmoothing.load()) {
        memcpy((void*)Config::AddrCameraSmoothing2, Config::PatchNop8, 8);
    }
    else {
        memcpy((void*)Config::AddrCameraSmoothing2, Config::OrigCameraSmoothing2, 8);
    }
    VirtualProtect((void*)Config::AddrCameraSmoothing2, 8, oP, &oP);
}

static void EmitMovAbsRax(std::vector<BYTE>& sc, uintptr_t address) {
    Add(sc, { 0x48, 0xB8 });
    Push(sc, address);
}

void RebuildCameraShellcode() {
    if (!Config::AddrCodecave) return;

    std::vector<BYTE> sc;

    Add(sc, {
        0x50, 0x51, 0x52,
        0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53
        });

    Add(sc, { 0x49, 0x89, 0xE3 });
    Add(sc, { 0x48, 0x83, 0xE4, 0xF0 });
    Add(sc, { 0x48, 0x83, 0xEC, 0x80 });

    Add(sc, { 0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20 });
    Add(sc, { 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30 });
    Add(sc, { 0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40 });
    Add(sc, { 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50 });
    Add(sc, { 0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60 });
    Add(sc, { 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70 });
    Add(sc, { 0x4C, 0x89, 0x5C, 0x24, 0x78 });

    Add(sc, { 0x48, 0x85, 0xDB });                      // test rbx, rbx
    Add(sc, { 0x0F, 0x84 });                             // jz invalidLabel
    size_t jz_orig = sc.size(); Push(sc, (uint32_t)0);

    Add(sc, { 0x48, 0x89, 0xD9 });                      // mov rcx, rbx

    EmitMovAbsRax(sc, (uintptr_t)&HandleCameraHook);
    Add(sc, { 0xFF, 0xD0 });                            // call rax

    Add(sc, { 0x4C, 0x8B, 0x5C, 0x24, 0x78 });

    Add(sc, { 0x84, 0xC0, 0x0F, 0x85 });                // test al, al ; jnz handledLabel
    size_t jne_handled = sc.size(); Push(sc, (uint32_t)0);

    size_t do_orig = sc.size();

    Add(sc, { 0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70 });

    Add(sc, { 0x4C, 0x8B, 0x5C, 0x24, 0x78 });
    Add(sc, { 0x4C, 0x89, 0xDC });

    Add(sc, { 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58 });
    Add(sc, { 0x5A, 0x59, 0x58 });

    for (int i = 0; i < 8; ++i) sc.push_back(Config::OrigCamera[i]);
    Add(sc, { 0xE9 });
    size_t jmp_orig = sc.size(); Push(sc, (uint32_t)0);

    size_t handledLabel = sc.size();

    Add(sc, { 0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70 });

    Add(sc, { 0x4C, 0x8B, 0x5C, 0x24, 0x78 });
    Add(sc, { 0x4C, 0x89, 0xDC });

    Add(sc, { 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58 });
    Add(sc, { 0x5A, 0x59, 0x58 });

    Add(sc, { 0xE9 });
    size_t jmp_handled = sc.size(); Push(sc, (uint32_t)0);

    size_t invalidLabel = sc.size();
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60 });
    Add(sc, { 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70 });
    Add(sc, { 0x4C, 0x8B, 0x5C, 0x24, 0x78 });
    Add(sc, { 0x4C, 0x89, 0xDC });
    Add(sc, { 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58 });
    Add(sc, { 0x5A, 0x59, 0x58 });
    Add(sc, { 0xE9 });
    size_t jmp_invalid = sc.size(); Push(sc, (uint32_t)0);

    uintptr_t codeBase = Config::AddrCodecave;

    auto patchRel32 = [&](size_t at, size_t target) {
        int64_t rel = (int64_t)(codeBase + target) - (int64_t)(codeBase + at + 4);
        int32_t r = (int32_t)rel;
        memcpy(&sc[at], &r, sizeof(r));
        };

    patchRel32(jz_orig, invalidLabel);
    patchRel32(jne_handled, handledLabel);

    uintptr_t retAddr = Config::AddrCameraHook + 8;
    auto patchJmp = [&](size_t at) {
        int64_t rel = (int64_t)retAddr - (int64_t)(codeBase + at + 4);
        int32_t r = (int32_t)rel;
        memcpy(&sc[at], &r, sizeof(r));
        };

    patchJmp(jmp_orig);
    patchJmp(jmp_handled);
    patchJmp(jmp_invalid);

    DWORD oP;
    VirtualProtect((void*)Config::AddrCodecave, sc.size(), PAGE_EXECUTE_READWRITE, &oP);
    memcpy((void*)Config::AddrCodecave, sc.data(), sc.size());
    VirtualProtect((void*)Config::AddrCodecave, sc.size(), oP, &oP);

    UpdateSmoothingPatch();
}

static void ToggleMod() {
    Config::bModActive = !Config::bModActive;
    DWORD oP;

    if (Config::AddrCameraHook) {
        VirtualProtect((void*)Config::AddrCameraHook, 8, PAGE_EXECUTE_READWRITE, &oP);
        memcpy((void*)Config::AddrCameraHook, Config::bModActive ? Config::PatchCameraJmp : Config::OrigCamera, 8);
        VirtualProtect((void*)Config::AddrCameraHook, 8, oP, &oP);
    }
    if (Config::AddrCollision1) {
        VirtualProtect((void*)Config::AddrCollision1, 8, PAGE_EXECUTE_READWRITE, &oP);
        memcpy((void*)Config::AddrCollision1, Config::bModActive ? Config::PatchXorpsXmm0 : Config::OrigCollision1, 8);
        VirtualProtect((void*)Config::AddrCollision1, 8, oP, &oP);
    }
    if (Config::AddrCollision2) {
        VirtualProtect((void*)Config::AddrCollision2, 8, PAGE_EXECUTE_READWRITE, &oP);
        memcpy((void*)Config::AddrCollision2, Config::bModActive ? Config::PatchXorpsXmm2 : Config::OrigCollision2, 8);
        VirtualProtect((void*)Config::AddrCollision2, 8, oP, &oP);
    }

    if (Config::bModActive) ForceUpdateCamera();

    UpdateSmoothingPatch();
    VehicleCamera::SetEnabled(Config::bModActive);
}

void ApplyHooks() {
    std::cout << "camera: scanning for on-foot hook signature\n";

    uintptr_t hookAddr = 0;

    // movss [rbx+498h], xmm1
    hookAddr = FindPattern("NMS.exe", Config::SigCamera);
    if (hookAddr) {
        std::cout << "camera: signature match @ 0x" << std::hex << hookAddr << std::dec << "\n";
    }

    // Fallback 8-byte signature
    if (!hookAddr) {
        hookAddr = FindPattern("NMS.exe", Config::SigCameraShort);
        if (hookAddr) {
            std::cout << "camera: signature match @ 0x" << std::hex << hookAddr << std::dec << "\n";
        }
    }

    if (!hookAddr) {
        std::cout << "camera: signature not found\n";
        return;
    }

    // Verify opcode: movss [rbx+...], xmm1
    if (*reinterpret_cast<uint8_t*>(hookAddr) != 0xF3 ||
        *reinterpret_cast<uint8_t*>(hookAddr + 1) != 0x0F ||
        *reinterpret_cast<uint8_t*>(hookAddr + 2) != 0x11 ||
        *reinterpret_cast<uint8_t*>(hookAddr + 3) != 0x8B) {
        std::cout << "camera: pre-flight check failed @ 0x" << std::hex << hookAddr << std::dec << "\n";
        return;
    }

    // Read displacement from instruction
    int32_t liveOffset = *reinterpret_cast<int32_t*>(hookAddr + 4);
    if (liveOffset == 0x498 || (liveOffset >= 0x480 && liveOffset <= 0x4C0)) {
        Config::OffsetDist = static_cast<uint32_t>(liveOffset);
        Config::OffsetCollision = Config::OffsetDist + 0x08;
        Config::OffsetX = Config::OffsetDist + 0x5C;
        Config::OffsetHeight = Config::OffsetDist + 0x60;
        Config::OffsetCharacterRigTag = 0x478;
        std::cout << "camera: offsets -> distance 0x" << std::hex << Config::OffsetDist
            << ", collision 0x" << Config::OffsetCollision
            << ", x 0x" << Config::OffsetX
            << ", height 0x" << Config::OffsetHeight
            << ", tag 0x" << Config::OffsetCharacterRigTag << std::dec << "\n";
    }
    else {
        Config::OffsetDist = 0x498;
        Config::OffsetCollision = 0x4A0;
        Config::OffsetX = 0x4F4;
        Config::OffsetHeight = 0x4F8;
        Config::OffsetCharacterRigTag = 0x478;
        std::cout << "camera: offsets -> distance 0x498, collision 0x4a0, x 0x4f4, height 0x4f8, tag 0x478\n";
    }

    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION pFunc = RtlLookupFunctionEntry(static_cast<DWORD64>(hookAddr), &imageBase, nullptr);
    if (pFunc) {
        uintptr_t funcEntry = static_cast<uintptr_t>(imageBase + pFunc->BeginAddress);
        uintptr_t funcEnd = static_cast<uintptr_t>(imageBase + pFunc->EndAddress);
        std::cout << "camera: function bounds 0x" << std::hex << funcEntry
            << "-0x" << funcEnd << " (" << std::dec << (funcEnd - funcEntry) << " bytes)\n";
    }

    Config::AddrCameraHook = hookAddr;
    memcpy(Config::OrigCamera, reinterpret_cast<void*>(hookAddr), 8);

    Config::AddrCollision1 = FindPattern("NMS.exe", Config::SigCollisionRead1);
    Config::AddrCollision2 = FindPattern("NMS.exe", Config::SigCollisionRead2);
    Config::AddrCameraSmoothing2 = FindPattern("NMS.exe", Config::SigCameraSmoothing2);

    if (Config::AddrCollision1) {
        std::cout << "camera: collision read1 @ 0x" << std::hex << Config::AddrCollision1 << std::dec << "\n";
    }
    if (Config::AddrCollision2) {
        std::cout << "camera: collision read2 @ 0x" << std::hex << Config::AddrCollision2 << std::dec << "\n";
    }
    if (Config::AddrCameraSmoothing2) {
        std::cout << "camera: smoothing2 signature @ 0x" << std::hex << Config::AddrCameraSmoothing2 << std::dec << "\n";
    }

    Config::AddrCodecave = (uintptr_t)AllocateNearAddress(Config::AddrCameraHook, 0x1000);
    if (!Config::AddrCodecave) {
        std::cout << "camera: failed to allocate codecave memory\n";
        return;
    }

    int64_t fullJmp = static_cast<int64_t>(Config::AddrCodecave) - (Config::AddrCameraHook + 5);
    if (fullJmp < INT32_MIN || fullJmp > INT32_MAX) {
        std::cout << "camera: codecave displacement out of range\n";
        return;
    }

    RebuildCameraShellcode();
    int32_t jmpToCodecave = static_cast<int32_t>(fullJmp);
    memcpy(&Config::PatchCameraJmp[1], &jmpToCodecave, 4);

    ToggleMod(); ToggleMod();

    std::cout << "camera: hook live @ 0x" << std::hex << Config::AddrCameraHook
        << " -> codecave 0x" << Config::AddrCodecave << std::dec << "\n";
}

void KeyPollLoop() {
    static bool last[256] = { false };
    int configCheckCounter = 0;
    while (Config::bInitialized.load()) {
        if (++configCheckCounter >= 100) { // ~1s poll interval
            configCheckCounter = 0;
            if (HasConfigChanged()) {
                LoadConfig();
                RebuildCameraShellcode();
            }
        }

        bool rebuild = false;
        bool save = false;

        auto HandleKey = [&](int key, std::atomic<float>& val, float mod) {
            bool current = (GetAsyncKeyState(key) & 0x8000) != 0;
            if (current) {
                float n = val.load() + mod;
                if (n > 50.0f) n = 50.0f;
                if (n < -50.0f) n = -50.0f;
                val.store(n);
                rebuild = true;
            }
            else if (last[key]) {
                save = true;
            }
            last[key] = current;
            };

        if (Config::EnableHotkeys.load()) {
            bool toggle = (GetAsyncKeyState(Config::ToggleKey) & 0x8000) != 0;
            if (toggle && !last[Config::ToggleKey]) ToggleMod();
            last[Config::ToggleKey] = toggle;

            if (Config::bModActive.load()) {
                HandleKey(Config::IncDistKey, Config::CustomDist, Config::Step);
                HandleKey(Config::DecDistKey, Config::CustomDist, -Config::Step);
                HandleKey(Config::IncWidthKey, Config::CustomX, Config::Step);
                HandleKey(Config::DecWidthKey, Config::CustomX, -Config::Step);
                HandleKey(Config::IncHeightKey, Config::CustomHeight, Config::Step);
                HandleKey(Config::DecHeightKey, Config::CustomHeight, -Config::Step);

                if (rebuild) {
                    RebuildCameraShellcode();
                    if (g_transitionState.load(std::memory_order_acquire) == 0) {
                        ForceUpdateCamera();
                    }
                }
                if (save) SaveConfig();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}