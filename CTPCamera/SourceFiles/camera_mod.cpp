#include "camera_mod.h"
#include "globals.h"
#include "g_memory.h"
#include "config_file.h"
#include <vector>
#include <thread>

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

static void ForceUpdateCamera() {
    if (!Config::GlobalCameraPtr) return;

    __try {
        float cWX = ScaleToGame(Config::CustomX.load(), -4.65f, 4.65f);
        float cHght = ScaleToGame(Config::CustomHeight.load(), -1.85f, 3.7f);
        float cDis = ScaleToGame(Config::CustomDist.load(), 1.4f, 25.0f);
        bool keepMomentum = Config::EnableCameraSmoothing.load();

        for (int i = 0; i < Config::CopiesXYZ; ++i) {
            uint32_t s = i * Config::StructStride;
            *(float*)(Config::GlobalCameraPtr + Config::OffsetX + s) = cWX;
            *(float*)(Config::GlobalCameraPtr + Config::OffsetHeight + s) = cHght;
            *(float*)(Config::GlobalCameraPtr + Config::OffsetDist + s) = cDis;
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

static void RebuildCameraShellcode() {
    if (!Config::AddrCodecave) return;
    std::vector<BYTE> sc;

    Add(sc, { 0x50, 0x51 });
    Add(sc, { 0x48, 0x8B, 0x4B, 0x08, 0x48, 0x85, 0xC9, 0x0F, 0x84 });
    size_t jz_patch = sc.size(); Push(sc, (uint32_t)0);
    Add(sc, { 0x48, 0x8B, 0x81, 0x80, 0x00, 0x00, 0x00, 0x48, 0x39, 0xD8, 0x0F, 0x85 });
    size_t jne_patch = sc.size(); Push(sc, (uint32_t)0);

    Add(sc, { 0x48, 0xB8 });
    Push(sc, (uintptr_t)&Config::GlobalCameraPtr);
    Add(sc, { 0x48, 0x89, 0x18 });

    float cWX = ScaleToGame(Config::CustomX.load(), -4.65f, 4.65f);
    float cHght = ScaleToGame(Config::CustomHeight.load(), -1.85f, 3.7f);
    float cDis = ScaleToGame(Config::CustomDist.load(), 1.4f, 25.0f);
    bool keepSmoothing = Config::EnableCameraSmoothing.load();

    for (int i = 0; i < Config::CopiesXYZ; ++i) {
        uint32_t s = i * Config::StructStride;
        Add(sc, { 0xB8 }); Push(sc, cWX); Add(sc, { 0x89, 0x83 }); Push(sc, Config::OffsetX + s);
        Add(sc, { 0xB8 }); Push(sc, cHght); Add(sc, { 0x89, 0x83 }); Push(sc, Config::OffsetHeight + s);
        Add(sc, { 0xB8 }); Push(sc, cDis); Add(sc, { 0x89, 0x83 }); Push(sc, Config::OffsetDist + s);
    }

    if (!keepSmoothing) {
        for (int i = 0; i < Config::CopiesSmoothing; ++i) {
            uint32_t s = i * Config::StructStride;
            float value1 = 0.0f
                ;uint32_t bits;memcpy(&bits, &value1, sizeof(bits));
            Add(sc, { 0xC7, 0x83 });
            Push(sc, Config::OffsetCameraSmoothing + s);
            Push(sc, bits);

            float value2 = 0.0f
                ;uint32_t bits1;memcpy(&bits1, &value2, sizeof(bits1));
            Add(sc, { 0xC7, 0x83 });
            Push(sc, Config::OffsetSprintCameraS + s);
            Push(sc, bits1);
        }
    }

    for (int i = 0; i < Config::CopiesCollision; ++i) {
        Add(sc, { 0xC7, 0x83 }); Push(sc, Config::OffsetCollision + (i * Config::StructStride)); Push(sc, (uint32_t)0);
    }

    Add(sc, { 0x59, 0x58, 0xE9 });
    size_t jmp_mod = sc.size(); Push(sc, (uint32_t)0);

    size_t do_orig = sc.size();
    Add(sc, { 0x59, 0x58 });
    for (int i = 0; i < 8; i++) sc.push_back(Config::OrigCamera[i]);
    sc.push_back(0xE9);
    size_t jmp_orig = sc.size(); Push(sc, (uint32_t)0);

    uint32_t relJz = (uint32_t)(do_orig - (jz_patch + 4)), relJne = (uint32_t)(do_orig - (jne_patch + 4));
    memcpy(&sc[jz_patch], &relJz, 4);
    memcpy(&sc[jne_patch], &relJne, 4);

    uintptr_t retAddr = Config::AddrCameraHook + 8;
    int32_t rJmpMod = (int32_t)(retAddr - (Config::AddrCodecave + jmp_mod + 4));
    int32_t rJmpOrig = (int32_t)(retAddr - (Config::AddrCodecave + jmp_orig + 4));
    memcpy(&sc[jmp_mod], &rJmpMod, 4);
    memcpy(&sc[jmp_orig], &rJmpOrig, 4);

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
        memcpy((void*)Config::AddrCollision2, Config::bModActive ? Config::PatchXorpsXmm1 : Config::OrigCollision2, 8);
        VirtualProtect((void*)Config::AddrCollision2, 8, oP, &oP);
    }

    if (Config::bModActive) ForceUpdateCamera();
    UpdateSmoothingPatch();
}

void ApplyHooks() {
    Config::AddrCameraHook = FindPattern("NMS.exe", Config::SigCamera);
    Config::AddrCollision1 = FindPattern("NMS.exe", Config::SigCollisionRead1);
    Config::AddrCollision2 = FindPattern("NMS.exe", Config::SigCollisionRead2);
    Config::AddrCameraSmoothing2 = FindPattern("NMS.exe", Config::SigCameraSmoothing2);

    if (!Config::AddrCameraHook) {
        return;
    }

    Config::AddrCodecave = (uintptr_t)AllocateNearAddress(Config::AddrCameraHook, 0x1000);

    if (!Config::AddrCodecave) {
        return;
    }

    RebuildCameraShellcode();
    int32_t jmpToCodecave = (int32_t)(Config::AddrCodecave - Config::AddrCameraHook - 5);
    memcpy(&Config::PatchCameraJmp[1], &jmpToCodecave, 4);

    ToggleMod(); ToggleMod();
}

void KeyPollLoop() {
    static bool last[256] = { false };
    while (Config::bInitialized.load()) {
        if (HasConfigChanged()) {
            LoadConfig();
            RebuildCameraShellcode();
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
                ForceUpdateCamera();
            }
            if (save) SaveConfig();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}