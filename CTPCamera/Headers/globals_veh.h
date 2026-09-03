#pragma once
#include <windows.h>
#include <cstdint>
#include <atomic>

namespace Config {
    // sub_140650680 (vehicle camera master update)
    inline const char* const SigVehicleMasterHook = "40 53 48 83 EC 20 48 8B 01 48 8B ?? FF 50 68 48 8B ?? 41 B8 02 00 00 00";
    inline const char* const SigVehicleMasterHookFallback = "40 53 48 83 EC 20 48 8B 01 48 8B ?? FF 50 68";

    // Mech Weapon Aim / Mining Hook (lea rcx, [rdi+458h]; lea rbx, [rcx+4])
    inline const char* const SigMechAimHook = "48 8D 8F 58 04 00 00 48 8D 59 04";

    // Condition Location / Environment Zone (sub_7FF6A4241800)
    inline const char* const SigConditionLocation = "48 8B 0D ?? ?? ?? ?? 8B B9 ?? ?? ?? ?? 48 8D B1";
    inline const char* const SigConditionLocationFallback = "48 8B 0D ?? ?? ?? ?? 8B B9 04 FE 55 00";

    inline uintptr_t* AddrGameStateManager = nullptr;
    inline uint32_t OffsetEnvZone = 0x55FE04;

    inline uintptr_t AddrVehicleMasterHook = 0;
    inline unsigned char OrigVehicleMasterHook[6] = { 0 };
    inline unsigned char PatchedVehicleMasterHook[6] = { 0 };

    inline uintptr_t AddrMechAimHook = 0;
    inline unsigned char OrigMechAimHook[9] = { 0 };
    inline unsigned char PatchedMechAimHook[9] = { 0 };

    inline std::atomic<bool> bVehicleHooksApplied{ false };

    inline std::atomic<float> CustomShipsDist{ 20.0f };
    inline std::atomic<float> CustomShipsX{ 0.0f };
    inline std::atomic<float> CustomCorvetteDist{ 28.0f };
    inline std::atomic<float> CustomCorvetteX{ 0.0f };
    inline std::atomic<float> CustomMechDist{ 6.5f };
    inline std::atomic<float> CustomMechHeight{ -0.5f };
    inline std::atomic<float> CustomTruckDist{ 12.0f };
    inline std::atomic<float> CustomSubDist{ 15.0f };
    inline std::atomic<float> CustomExoGeneralDist{ 15.25f };
    inline std::atomic<float> CustomHoverDist{ 16.5f };
    inline std::atomic<float> CustomPilgrimDist{ 15.25f };
}