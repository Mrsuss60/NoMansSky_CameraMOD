#include "camera_mod_veh.h"
#include "globals_veh.h"
#include "globals.h"
#include "g_memory.h"
#include <windows.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <iostream>

namespace {

    struct CodeEmitter {
        std::vector<uint8_t> buf;
        std::unordered_map<std::string, size_t> labels;
        std::vector<std::pair<size_t, std::string>> fixups;
        std::vector<size_t> externalFixups;

        void Bytes(std::initializer_list<uint8_t> b) { buf.insert(buf.end(), b.begin(), b.end()); }
        void Imm32(uint32_t v) { uint8_t b[4]; memcpy(b, &v, 4); buf.insert(buf.end(), b, b + 4); }
        void Imm64(uint64_t v) { uint8_t b[8]; memcpy(b, &v, 8); buf.insert(buf.end(), b, b + 8); }

        void Mark(const std::string& name) { labels[name] = buf.size(); }

        void Jcc32(std::initializer_list<uint8_t> opcode, const std::string& target) {
            Bytes(opcode);
            fixups.emplace_back(buf.size(), target);
            Imm32(0);
        }
        void Jmp32(const std::string& target) { Jcc32({ 0xE9 }, target); }

        void JmpExternal() {
            Bytes({ 0xE9 });
            externalFixups.push_back(buf.size());
            Imm32(0);
        }

        void Resolve() {
            for (auto& fx : fixups) {
                size_t pos = fx.first;
                uint32_t target = (uint32_t)labels.at(fx.second);
                uint32_t rel = target - (uint32_t)(pos + 4);
                memcpy(&buf[pos], &rel, 4);
            }
        }
    };

    void PatchExternalJumps(CodeEmitter& e, uintptr_t base, uintptr_t returnAddr) {
        for (size_t pos : e.externalFixups) {
            uint32_t rel = (uint32_t)(returnAddr - (base + pos + 4));
            memcpy(&e.buf[pos], &rel, 4);
        }
    }

    typedef __int64(__fastcall* tSub140650680)(uintptr_t camera, void* v200);
    static tSub140650680 s_origSub140650680 = nullptr;

    static void* s_pVehTrampoline = nullptr;
    static void* s_pVehRelayStub = nullptr;

    static bool IsInsideHangarOrStation() {
        if (!Config::AddrGameStateManager) return false;
        __try {
            uintptr_t pStateMgr = *Config::AddrGameStateManager;
            if (!pStateMgr) return false;
            int envZone = *reinterpret_cast<int*>(pStateMgr + Config::OffsetEnvZone);
            // 2: Space Station, 10: Freighter, 14: Space Anomaly (Nexus), 15: Atlas Station
            return (envZone == 2 || envZone == 10 || envZone == 14 || envZone == 15);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    __int64 __fastcall Detour_MasterCameraUpdate(uintptr_t camera, void* v200) {
        __int64 result = 0;
        __try {
            // sub_140650680 fills template into v200
            result = s_origSub140650680(camera, v200);

            if (!camera || !Config::bModActive.load(std::memory_order_relaxed)) {
                return result;
            }

            char currentTag[17] = { 0 };
            static char s_lastTag[17] = { 0 };

            memcpy(currentTag, (void*)(camera + Config::OffsetCharacterRigTag), 16);
            currentTag[16] = '\0';

            if (memcmp(s_lastTag, currentTag, 16) != 0) {
                float d0 = *(float*)(camera + Config::OffsetDist);
                float d1 = *(float*)(camera + Config::OffsetDist + Config::StructStride);
                float d2 = *(float*)(camera + Config::OffsetDist + 2 * Config::StructStride);

                float h0 = *(float*)(camera + Config::OffsetHeight);
                float h1 = *(float*)(camera + Config::OffsetHeight + Config::StructStride);
                float h2 = *(float*)(camera + Config::OffsetHeight + 2 * Config::StructStride);

                std::cout << "camera: template changed -> [" << (currentTag[0] ? currentTag : "NONE") << "]"
                    << " | slot0: dist " << d0 << ", height " << h0 << "\n";

                memcpy(s_lastTag, currentTag, 17);
            }

            // On-foot camera handled by camera_mod.cpp
            if (memcmp(currentTag, "CHAR", 4) == 0) {
                return result;
            }

            bool isShipOrCorvette = (memcmp(currentTag, "CORV", 4) == 0 ||
                memcmp(currentTag, "SPAC", 4) == 0 || memcmp(currentTag, "DROP", 4) == 0 ||
                memcmp(currentTag, "SCIE", 4) == 0 || memcmp(currentTag, "SHUT", 4) == 0 ||
                memcmp(currentTag, "FLAT", 4) == 0 || memcmp(currentTag, "SAIL", 4) == 0 ||
                memcmp(currentTag, "ROYA", 4) == 0 || memcmp(currentTag, "ROBO", 4) == 0 ||
                memcmp(currentTag, "ALIE", 4) == 0);

            if (isShipOrCorvette && IsInsideHangarOrStation()) {
                return result;
            }

            float targetVehDist = -1.0f;
            float targetVehHeight = -999.0f;

            if (memcmp(currentTag, "MECH", 4) == 0) {
                targetVehDist = Config::CustomMechDist.load(std::memory_order_relaxed);
                targetVehHeight = Config::CustomMechHeight.load(std::memory_order_relaxed);
            }
            else {
                // Restore default template height for non-mech vehicles
                if (v200) {
                    targetVehHeight = *(float*)((uintptr_t)v200 + 0x80);
                }
            }

            if (memcmp(currentTag, "TRUC", 4) == 0) {
                targetVehDist = Config::CustomTruckDist.load(std::memory_order_relaxed);
            }
            else if (memcmp(currentTag, "SUBM", 4) == 0 || memcmp(currentTag, "SUB_", 4) == 0) {
                targetVehDist = Config::CustomSubDist.load(std::memory_order_relaxed);
            }
            else if (memcmp(currentTag, "BUGG", 4) == 0) {
                targetVehDist = Config::CustomExoGeneralDist.load(std::memory_order_relaxed);
            }
            else if (memcmp(currentTag, "HOVE", 4) == 0) {
                targetVehDist = Config::CustomHoverDist.load(std::memory_order_relaxed);
            }
            else if (memcmp(currentTag, "VEHI", 4) == 0 || memcmp(currentTag, "BIKE", 4) == 0) {
                // Distinguish Pilgrim (15.25) and Nomad (16.5)
                float templateDist = 0.0f;
                if (v200) {
                    templateDist = *(float*)((uintptr_t)v200 + 0x20);
                }
                if (templateDist <= 0.0f) {
                    templateDist = *(float*)(camera + Config::OffsetDist);
                }

                if (std::fabs(templateDist - 16.5f) < 0.6f) {
                    targetVehDist = Config::CustomHoverDist.load(std::memory_order_relaxed);
                }
                else {
                    targetVehDist = Config::CustomPilgrimDist.load(std::memory_order_relaxed);
                }
            }
            else if (memcmp(currentTag, "CORV", 4) == 0) {
                targetVehDist = Config::CustomCorvetteDist.load(std::memory_order_relaxed);
            }
            else if (memcmp(currentTag, "SPAC", 4) == 0 || memcmp(currentTag, "DROP", 4) == 0 ||
                memcmp(currentTag, "SCIE", 4) == 0 || memcmp(currentTag, "SHUT", 4) == 0 ||
                memcmp(currentTag, "FLAT", 4) == 0 || memcmp(currentTag, "SAIL", 4) == 0 ||
                memcmp(currentTag, "ROYA", 4) == 0 || memcmp(currentTag, "ROBO", 4) == 0 ||
                memcmp(currentTag, "ALIE", 4) == 0) {
                targetVehDist = Config::CustomShipsDist.load(std::memory_order_relaxed);
            }

            if (targetVehDist > 0.0f) {
                if (v200) {
                    *(float*)((uintptr_t)v200 + 0x20) = targetVehDist;
                }

                for (int i = 0; i < Config::CopiesXYZ; ++i) {
                    const uint32_t s = i * Config::StructStride;
                    *(float*)(camera + Config::OffsetDist + s) = targetVehDist;
                }
            }

            if (targetVehHeight != -999.0f) {
                if (v200 && memcmp(currentTag, "MECH", 4) == 0) {
                    *(float*)((uintptr_t)v200 + 0x80) = targetVehHeight;
                }

                for (int i = 0; i < Config::CopiesXYZ; ++i) {
                    const uint32_t s = i * Config::StructStride;
                    *(float*)(camera + Config::OffsetHeight + s) = targetVehHeight;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return result;
        }

        return result;
    }

    CodeEmitter BuildMechAimCodecave(uintptr_t targetFuncAddr) {
        CodeEmitter e;

        // Check for MECH tag
        e.Bytes({ 0x81, 0xBF });
        uint32_t tagOff = Config::OffsetCharacterRigTag;
        e.Bytes({ (uint8_t)(tagOff & 0xFF), (uint8_t)((tagOff >> 8) & 0xFF), (uint8_t)((tagOff >> 16) & 0xFF), (uint8_t)((tagOff >> 24) & 0xFF) });
        e.Bytes({ 'M', 'E', 'C', 'H' });
        e.Jcc32({ 0x0F, 0x84 }, "is_mech");

        e.Bytes({ 0x48, 0xB8 }); // mov rax, targetFuncAddr
        e.Imm64((uint64_t)targetFuncAddr);
        e.Bytes({ 0xFF, 0xD0 }); // call rax
        e.Bytes({ 0xF3, 0x0F, 0x11, 0x03 }); // movss [rbx], xmm0
        e.JmpExternal();

        e.Mark("is_mech");
        e.Bytes({ 0xC7, 0x87, 0x58, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }); // mov dword ptr [rdi+458h], 0
        e.Bytes({ 0xC7, 0x03, 0x00, 0x00, 0x00, 0x00 });                         // mov dword ptr [rbx], 0
        e.JmpExternal();

        e.Resolve();
        return e;
    }

    void SetHookEnabled(bool enable) {
        if (!Config::AddrVehicleMasterHook) return;
        DWORD oldProtect;
        VirtualProtect((void*)Config::AddrVehicleMasterHook, 6, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy((void*)Config::AddrVehicleMasterHook,
            enable ? Config::PatchedVehicleMasterHook : Config::OrigVehicleMasterHook, 6);
        VirtualProtect((void*)Config::AddrVehicleMasterHook, 6, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), (void*)Config::AddrVehicleMasterHook, 6);
    }

} 

namespace VehicleCamera {

    bool ApplyHooks() {
        std::cout << "vehicle: scanning for master hook signature\n";
        uintptr_t addr = FindPattern("NMS.exe", Config::SigVehicleMasterHook);
        if (!addr) {
            std::cout << "vehicle: primary signature not found, attempting fallback\n";
            addr = FindPattern("NMS.exe", Config::SigVehicleMasterHookFallback);
        }
        if (!addr) {
            std::cout << "vehicle: signature not found\n";
            return false;
        }

        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION pFunc = RtlLookupFunctionEntry(static_cast<DWORD64>(addr), &imageBase, nullptr);
        if (pFunc) {
            uintptr_t funcEntry = static_cast<uintptr_t>(imageBase + pFunc->BeginAddress);
            uintptr_t funcEnd = static_cast<uintptr_t>(imageBase + pFunc->EndAddress);
            std::cout << "vehicle: function bounds 0x" << std::hex << funcEntry
                << "-0x" << funcEnd << " (" << std::dec << (funcEnd - funcEntry) << " bytes)\n";
        }

        // Check opcode: push rbx (0x40 0x53)
        if (*reinterpret_cast<uint8_t*>(addr) != 0x40 || *reinterpret_cast<uint8_t*>(addr + 1) != 0x53) {
            std::cout << "vehicle: pre-flight check failed @ 0x" << std::hex << addr << std::dec << "\n";
            return false;
        }

        Config::AddrVehicleMasterHook = addr;
        std::cout << "vehicle: master hook @ 0x" << std::hex << addr << std::dec << "\n";

        memcpy(Config::OrigVehicleMasterHook, (void*)addr, 6);

        // Trampoline
        s_pVehTrampoline = AllocateNearAddress(addr, 0x40);
        if (!s_pVehTrampoline) {
            std::cout << "vehicle: failed to allocate trampoline memory\n";
            return false;
        }

        uint8_t trampBytes[20] = {
            0x40, 0x53,                         // push rbx
            0x48, 0x83, 0xEC, 0x20,             // sub rsp, 20h
            0xFF, 0x25, 0x00, 0x00, 0x00, 0x00  // jmp qword ptr [rip+0]
        };
        uintptr_t continueAddr = addr + 6;
        memcpy(&trampBytes[12], &continueAddr, sizeof(uintptr_t));

        DWORD oldProtect;
        VirtualProtect(s_pVehTrampoline, sizeof(trampBytes), PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(s_pVehTrampoline, trampBytes, sizeof(trampBytes));
        VirtualProtect(s_pVehTrampoline, sizeof(trampBytes), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), s_pVehTrampoline, sizeof(trampBytes));

        s_origSub140650680 = reinterpret_cast<tSub140650680>(s_pVehTrampoline);

        // Relay stub
        s_pVehRelayStub = AllocateNearAddress(addr, 0x40);
        if (!s_pVehRelayStub) {
            std::cout << "vehicle: failed to allocate relay stub memory\n";
            return false;
        }

        int64_t fullDist = reinterpret_cast<uintptr_t>(s_pVehRelayStub) - (addr + 5);
        if (fullDist < INT32_MIN || fullDist > INT32_MAX) {
            std::cout << "vehicle: relay stub displacement out of range\n";
            return false;
        }

        uint8_t relayBytes[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        uintptr_t detourAddr = reinterpret_cast<uintptr_t>(&Detour_MasterCameraUpdate);
        memcpy(&relayBytes[6], &detourAddr, sizeof(uintptr_t));

        VirtualProtect(s_pVehRelayStub, sizeof(relayBytes), PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(s_pVehRelayStub, relayBytes, sizeof(relayBytes));
        VirtualProtect(s_pVehRelayStub, sizeof(relayBytes), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), s_pVehRelayStub, sizeof(relayBytes));

        Config::PatchedVehicleMasterHook[0] = 0xE9;
        int32_t relJmp = static_cast<int32_t>(fullDist);
        memcpy(&Config::PatchedVehicleMasterHook[1], &relJmp, 4);
        Config::PatchedVehicleMasterHook[5] = 0x90;

        // Mech aim / mining height lock
        uintptr_t aimPatternAddr = FindPattern("NMS.exe", Config::SigMechAimHook);
        if (aimPatternAddr) {
            uintptr_t hookSite = aimPatternAddr + 0x1F;
            Config::AddrMechAimHook = hookSite;

            memcpy(Config::OrigMechAimHook, (void*)hookSite, 9);

            int32_t callRelOffset = *(int32_t*)(hookSite + 1);
            uintptr_t targetFuncAddr = (hookSite + 5) + callRelOffset;

            CodeEmitter aimEmitter = BuildMechAimCodecave(targetFuncAddr);
            void* aimCave = AllocateNearAddress(hookSite, aimEmitter.buf.size());
            if (aimCave) {
                PatchExternalJumps(aimEmitter, (uintptr_t)aimCave, hookSite + 9);

                VirtualProtect(aimCave, aimEmitter.buf.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
                memcpy(aimCave, aimEmitter.buf.data(), aimEmitter.buf.size());
                VirtualProtect(aimCave, aimEmitter.buf.size(), oldProtect, &oldProtect);
                FlushInstructionCache(GetCurrentProcess(), aimCave, aimEmitter.buf.size());

                Config::PatchedMechAimHook[0] = 0xE9;
                int32_t relAim = static_cast<int32_t>((uintptr_t)aimCave - (hookSite + 5));
                memcpy(&Config::PatchedMechAimHook[1], &relAim, 4);
                Config::PatchedMechAimHook[5] = 0x90;
                Config::PatchedMechAimHook[6] = 0x90;
                Config::PatchedMechAimHook[7] = 0x90;
                Config::PatchedMechAimHook[8] = 0x90;

                VirtualProtect((void*)hookSite, 9, PAGE_EXECUTE_READWRITE, &oldProtect);
                memcpy((void*)hookSite, Config::PatchedMechAimHook, 9);
                VirtualProtect((void*)hookSite, 9, oldProtect, &oldProtect);
                FlushInstructionCache(GetCurrentProcess(), (void*)hookSite, 9);

                std::cout << "vehicle: mech aim hook live @ 0x" << std::hex << hookSite << std::dec << "\n";
            }
        }

        // Environment detector (Space Station / Freighter / Anomaly)
        uintptr_t envSig = FindPattern("NMS.exe", Config::SigConditionLocation);
        if (!envSig) {
            envSig = FindPattern("NMS.exe", Config::SigConditionLocationFallback);
        }
        if (envSig) {
            int32_t disp = *reinterpret_cast<int32_t*>(envSig + 3);
            Config::AddrGameStateManager = reinterpret_cast<uintptr_t*>(envSig + 7 + disp);
            Config::OffsetEnvZone = *reinterpret_cast<uint32_t*>(envSig + 9);
            std::cout << "vehicle: environment detector live @ 0x" << std::hex << (uintptr_t)Config::AddrGameStateManager << std::dec << "\n";
        }

        Config::bVehicleHooksApplied.store(true);
        SetHookEnabled(true);

        std::cout << "vehicle: master hook live -> relay 0x" << std::hex << (uintptr_t)s_pVehRelayStub
            << " -> trampoline 0x" << (uintptr_t)s_pVehTrampoline << std::dec << "\n";

        return true;
    }

    void RebuildShellcode() {
        if (!Config::bVehicleHooksApplied.load()) {
            ApplyHooks();
        }
    }

    void SetEnabled(bool enable) {
        if (!Config::bVehicleHooksApplied.load()) return;
        std::cout << "vehicle: hooks toggled -> " << (enable ? "enabled" : "disabled") << "\n";
        SetHookEnabled(enable);

        if (Config::AddrMechAimHook) {
            DWORD oldProtect;
            VirtualProtect((void*)Config::AddrMechAimHook, 9, PAGE_EXECUTE_READWRITE, &oldProtect);
            memcpy((void*)Config::AddrMechAimHook,
                enable ? Config::PatchedMechAimHook : Config::OrigMechAimHook, 9);
            VirtualProtect((void*)Config::AddrMechAimHook, 9, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), (void*)Config::AddrMechAimHook, 9);
        }
    }

} 