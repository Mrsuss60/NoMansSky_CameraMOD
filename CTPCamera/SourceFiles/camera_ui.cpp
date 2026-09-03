#include "camera_ui.h"
#include "globals.h"
#include "globals_veh.h"
#include "config_file.h"
#include "camera_mod.h"
#include "camera_mod_veh.h"
#include "g_memory.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace CameraUI {

    static uintptr_t s_CallSiteEndCyclic = 0;
    static void* s_pRelayTrampoline = nullptr;

    static tAddSectionHeader fnAddSectionHeader = nullptr;
    static tAddToggleOption  fnAddToggleOption = nullptr;
    static tAddIntSlider     fnAddIntSlider = nullptr;
    static tEndCyclicOption  fnEndCyclicOption = nullptr;

    // Range mapping [-50.0, +50.0] <-> [0, 100]
    static int ToSliderRange(float val) {
        int clamped = static_cast<int>(std::round(val + 50.0f));
        return (std::max)(0, (std::min)(100, clamped));
    }

    static float FromSliderRange(int val) {
        return static_cast<float>(val - 50);
    }

    // Step mapping [0.01, 1.00] <-> [1, 100]
    static int ToStepSlider(float val) {
        int clamped = static_cast<int>(std::round(val * 100.0f));
        return (std::max)(1, (std::min)(100, clamped));
    }

    static float FromStepSlider(int val) {
        return static_cast<float>(val) / 100.0f;
    }

    // Dynamic mapping: minSlider = minVal, maxSlider = maxVal
    static int ToVehicleSlider(float val, float minVal, float maxVal = 250.0f, int minSlider = 1, int maxSlider = 100) {
        if (val <= minVal) return minSlider;
        if (val >= maxVal) return maxSlider;
        float ratio = (val - minVal) / (maxVal - minVal);
        int clamped = static_cast<int>(std::round(static_cast<float>(minSlider) + ratio * static_cast<float>(maxSlider - minSlider)));
        return (std::max)(minSlider, (std::min)(maxSlider, clamped));
    }

    static float FromVehicleSlider(int val, float minVal, float maxVal = 250.0f, int minSlider = 1, int maxSlider = 100) {
        if (val <= minSlider) return minVal;
        if (val >= maxSlider) return maxVal;
        float ratio = static_cast<float>(val - minSlider) / static_cast<float>(maxSlider - minSlider);
        return minVal + ratio * (maxVal - minVal);
    }

    static int ToShipsXSlider(float val, float maxOffset = 25.0f) {
        if (std::abs(val) < 0.001f) return 50;
        if (val > 0.0f) {
            int tick = static_cast<int>(std::round(50.0f + (val / maxOffset) * 50.0f));
            return (std::max)(50, (std::min)(100, tick));
        }
        else {
            int tick = static_cast<int>(std::round(50.0f + (val / maxOffset) * 49.0f));
            return (std::max)(1, (std::min)(50, tick));
        }
    }

    static float FromShipsXSlider(int sliderVal, float maxOffset = 25.0f) {
        if (sliderVal == 50) return 0.0f;
        if (sliderVal > 50) {
            return (static_cast<float>(sliderVal - 50) / 50.0f) * maxOffset;
        }
        else {
            return (static_cast<float>(sliderVal - 50) / 49.0f) * maxOffset;
        }
    }

    static void UpdateModPatches(bool bEnable) {
        DWORD oP;
        if (Config::AddrCameraHook) {
            VirtualProtect((void*)Config::AddrCameraHook, 8, PAGE_EXECUTE_READWRITE, &oP);
            memcpy((void*)Config::AddrCameraHook, bEnable ? Config::PatchCameraJmp : Config::OrigCamera, 8);
            VirtualProtect((void*)Config::AddrCameraHook, 8, oP, &oP);
        }
        if (Config::AddrCollision1) {
            VirtualProtect((void*)Config::AddrCollision1, 8, PAGE_EXECUTE_READWRITE, &oP);
            memcpy((void*)Config::AddrCollision1, bEnable ? Config::PatchXorpsXmm0 : Config::OrigCollision1, 8);
            VirtualProtect((void*)Config::AddrCollision1, 8, oP, &oP);
        }
        if (Config::AddrCollision2) {
            VirtualProtect((void*)Config::AddrCollision2, 8, PAGE_EXECUTE_READWRITE, &oP);
            memcpy((void*)Config::AddrCollision2, bEnable ? Config::PatchXorpsXmm2 : Config::OrigCollision2, 8);
            VirtualProtect((void*)Config::AddrCollision2, 8, oP, &oP);
        }

        VehicleCamera::SetEnabled(bEnable);
    }

    void RenderCustomCameraOptions(void* pUIContext) {
        if (!pUIContext) return;

        bool bChanged = false;

        __try {
            fnAddSectionHeader(pUIContext, "NMS CUSTOMIZABLE THIRD-PERSON CAMERA", 2);

            const char* toggleLabels[2] = { "UI_ENABLED", "UI_DISABLED" };

            char curModActive = Config::bModActive.load() ? 1 : 0;
            char newModActive = fnAddToggleOption(
                pUIContext,
                "CUSTOM TPCAMERA",
                "Enable/Disable CUSTOM THIRD-PERSON CAMERA",
                curModActive,
                1,
                toggleLabels
            );

            if (newModActive != curModActive) {
                std::cout << "ui: Master Mod Toggle changed -> " << (int)newModActive << "\n";
                Config::bModActive.store(newModActive != 0);
                UpdateModPatches(newModActive != 0);
                bChanged = true;
            }

            char curHotkeys = Config::EnableHotkeys.load() ? 1 : 0;
            char newHotkeys = fnAddToggleOption(
                pUIContext,
                "ENABLE HOTKEYS",
                "Enable/Disable IN-GAME HOTKEYS",
                curHotkeys,
                1,
                toggleLabels
            );

            if (newHotkeys != curHotkeys) {
                std::cout << "ui: Enable Hotkeys changed -> " << (int)newHotkeys << "\n";
                Config::EnableHotkeys.store(newHotkeys != 0);
                bChanged = true;
            }

            SliderExtraParams coordParams{};
            coordParams.stepConfig = 0x100000001LL;
            coordParams.suffix = "";
            coordParams.reserved = 0;

            int curDistUI = ToSliderRange(Config::CustomDist.load());
            int newDistUI = fnAddIntSlider(
                pUIContext,
                "Camera Distance",
                "Change The Third-Person Camera Distance From The Player",
                curDistUI,
                ToSliderRange(13.86f),
                0,
                100,
                &coordParams
            );
            if (newDistUI != curDistUI) {
                float newDistVal = FromSliderRange(newDistUI);
                Config::CustomDist.store(newDistVal);
                bChanged = true;
            }

            int curWidthUI = ToSliderRange(Config::CustomX.load());
            int newWidthUI = fnAddIntSlider(
                pUIContext,
                "CAMERA X-OFFSET",
                "Shift The Third-Person Camera Left Or Right From The Player",
                curWidthUI,
                ToSliderRange(22.54f),
                0,
                100,
                &coordParams
            );
            if (newWidthUI != curWidthUI) {
                float newWidthVal = FromSliderRange(newWidthUI);
                Config::CustomX.store(newWidthVal);
                bChanged = true;
            }

            int curHeightUI = ToSliderRange(Config::CustomHeight.load());
            int newHeightUI = fnAddIntSlider(
                pUIContext,
                "CAMERA HEIGHT",
                "Change The Third-Person Camera Height From The Player",
                curHeightUI,
                ToSliderRange(-0.67f),
                0,
                100,
                &coordParams
            );
            if (newHeightUI != curHeightUI) {
                float newHeightVal = FromSliderRange(newHeightUI);
                Config::CustomHeight.store(newHeightVal);
                bChanged = true;
            }

            SliderExtraParams stepParams{};
            stepParams.stepConfig = 0x100000001LL;
            stepParams.suffix = "";
            stepParams.reserved = 0;

            int curStepUI = ToStepSlider(Config::Step);
            int newStepUI = fnAddIntSlider(
                pUIContext,
                "HOTKEY STEP SPEED",
                "Sets How Fast Camera Coords Changes When Using Hotkeys",
                curStepUI,
                18,
                1,
                100,
                &stepParams
            );
            if (newStepUI != curStepUI) {
                Config::Step = FromStepSlider(newStepUI);
                bChanged = true;
            }

            char curSmoothing = Config::EnableCameraSmoothing.load() ? 1 : 0;
            char newSmoothing = fnAddToggleOption(
                pUIContext,
                "CAMERA SMOOTHING",
                "ENABLE SMOOTH MOMENTUM TRACKING OR LOCK DISTANCE",
                curSmoothing,
                0,
                toggleLabels
            );
            if (newSmoothing != curSmoothing) {
                std::cout << "ui: Camera Smoothing changed -> " << (int)newSmoothing << "\n";
                Config::EnableCameraSmoothing.store(newSmoothing != 0);
                bChanged = true;
            }

            fnAddSectionHeader(pUIContext, "SHIPS & CORVETTE THIRD PERSON CAMERA", 2);

            SliderExtraParams vehicleParams{};
            vehicleParams.stepConfig = 0x100000001LL;
            vehicleParams.suffix = "";
            vehicleParams.reserved = 0;

            int curShipsDistUI = ToVehicleSlider(Config::CustomShipsDist.load(), 20.0f, 65.0f);
            int newShipsDistUI = fnAddIntSlider(
                pUIContext,
                "SHIPS Camera Distance",
                "Change The Third-Person Camera Distance For All Starships",
                curShipsDistUI,
                1,
                1,
                100,
                &vehicleParams
            );
            if (newShipsDistUI != curShipsDistUI) {
                float newShipsDistVal = FromVehicleSlider(newShipsDistUI, 20.0f, 65.0f);
                Config::CustomShipsDist.store(newShipsDistVal);
                bChanged = true;
            }

            int curShipsXUI = ToShipsXSlider(Config::CustomShipsX.load());
            int newShipsXUI = fnAddIntSlider(
                pUIContext,
                "SHIPS X-OFFSET",
                "Shift The Third-Person Camera Left Or Right For All Starships - Default Value = 50",
                curShipsXUI,
                50,
                1,
                100,
                &vehicleParams
            );
            if (newShipsXUI != curShipsXUI) {
                float newShipsXVal = FromShipsXSlider(newShipsXUI);
                Config::CustomShipsX.store(newShipsXVal);
                bChanged = true;
            }

            int curCorvetteDistUI = ToVehicleSlider(Config::CustomCorvetteDist.load(), 28.0f, 350.0f, 1, 300);
            int newCorvetteDistUI = fnAddIntSlider(
                pUIContext,
                "CORVETTE Camera Distance",
                "Change The Third-Person Camera Distance For The Corvette",
                curCorvetteDistUI,
                1,
                1,
                300,
                &vehicleParams
            );
            if (newCorvetteDistUI != curCorvetteDistUI) {
                float newCorvetteDistVal = FromVehicleSlider(newCorvetteDistUI, 28.0f, 350.0f, 1, 300);
                Config::CustomCorvetteDist.store(newCorvetteDistVal);
                bChanged = true;
            }

            int curCorvetteXUI = ToShipsXSlider(Config::CustomCorvetteX.load(), 50.0f);
            int newCorvetteXUI = fnAddIntSlider(
                pUIContext,
                "CORVETTE X-OFFSET",
                "Shift The Third-Person Camera Left Or Right For The Corvette - Default Value = 50",
                curCorvetteXUI,
                50,
                1,
                100,
                &vehicleParams
            );
            if (newCorvetteXUI != curCorvetteXUI) {
                float newCorvetteXVal = FromShipsXSlider(newCorvetteXUI, 50.0f);
                Config::CustomCorvetteX.store(newCorvetteXVal);
                bChanged = true;
            }

            fnAddSectionHeader(pUIContext, "EXOCRAFTS THIRD PERSON CAMERA", 2);

            int curMechDistUI = ToVehicleSlider(Config::CustomMechDist.load(), 6.5f, 30.0f);
            int newMechDistUI = fnAddIntSlider(
                pUIContext,
                "Minotaur Camera Distance",
                "Change The Third-Person Camera Distance For The Minotaur",
                curMechDistUI,
                1,
                1,
                100,
                &vehicleParams
            );
            if (newMechDistUI != curMechDistUI) {
                float newMechDistVal = FromVehicleSlider(newMechDistUI, 6.5f, 30.0f);
                Config::CustomMechDist.store(newMechDistVal);
                bChanged = true;
            }

            int curMechHeightUI = ToVehicleSlider(Config::CustomMechHeight.load(), -0.5f, 10.0f);
            int newMechHeightUI = fnAddIntSlider(
                pUIContext,
                "Minotaur Camera Height",
                "Change The Third-Person Camera Height For The Minotaur",
                curMechHeightUI,
                1,
                1,
                100,
                &vehicleParams
            );
            if (newMechHeightUI != curMechHeightUI) {
                float newMechHeightVal = FromVehicleSlider(newMechHeightUI, -0.5f, 10.0f);
                Config::CustomMechHeight.store(newMechHeightVal);
                bChanged = true;
            }

            int curTruckDistUI = ToVehicleSlider(Config::CustomTruckDist.load(), 12.0f, 50.0f);
            int newTruckDistUI = fnAddIntSlider(
                pUIContext,
                "Colossus Camera Distance",
                "Change The Third-Person Camera Distance For The Colossus Truck",
                curTruckDistUI,
                1,
                1,
                100,
                &vehicleParams
            );
            if (newTruckDistUI != curTruckDistUI) {
                float newTruckDistVal = FromVehicleSlider(newTruckDistUI, 12.0f, 50.0f);
                Config::CustomTruckDist.store(newTruckDistVal);
                bChanged = true;
            }

            int curSubDistUI = ToVehicleSlider(Config::CustomSubDist.load(), 15.0f, 50.0f);
            int newSubDistUI = fnAddIntSlider(
                pUIContext,
                "Nautilon Camera Distance",
                "Change The Third-Person Camera Distance For The Nautilon",
                curSubDistUI,
                1,
                1,
                100,
                &vehicleParams
            );
            if (newSubDistUI != curSubDistUI) {
                float newSubDistVal = FromVehicleSlider(newSubDistUI, 15.0f, 50.0f);
                Config::CustomSubDist.store(newSubDistVal);
                bChanged = true;
            }

            int curExoGeneralDistUI = ToVehicleSlider(Config::CustomExoGeneralDist.load(), 15.25f, 45.0f);
            int newExoGeneralDistUI = fnAddIntSlider(
                pUIContext,
                "Roamer Camera Distance",
                "Change The Third-Person Camera Distance For The Roamer",
                curExoGeneralDistUI,
                1,
                1,
                100,
                &vehicleParams
            );
            if (newExoGeneralDistUI != curExoGeneralDistUI) {
                float newExoGeneralDistVal = FromVehicleSlider(newExoGeneralDistUI, 15.25f, 45.0f);
                Config::CustomExoGeneralDist.store(newExoGeneralDistVal);
                bChanged = true;
            }

            int curHoverDistUI = ToVehicleSlider(Config::CustomHoverDist.load(), 16.5f, 45.0f);
            int newHoverDistUI = fnAddIntSlider(
                pUIContext,
                "Nomad Camera Distance",
                "Change The Third-Person Camera Distance For The Nomad",
                curHoverDistUI,
                1,
                1,
                100,
                &vehicleParams
            );
            if (newHoverDistUI != curHoverDistUI) {
                float newHoverDistVal = FromVehicleSlider(newHoverDistUI, 16.5f, 45.0f);
                Config::CustomHoverDist.store(newHoverDistVal);
                bChanged = true;
            }

            int curPilgrimDistUI = ToVehicleSlider(Config::CustomPilgrimDist.load(), 15.25f, 45.0f);
            int newPilgrimDistUI = fnAddIntSlider(
                pUIContext,
                "Pilgrim Camera Distance",
                "Change The Third-Person Camera Distance For The Pilgrim",
                curPilgrimDistUI,
                1,
                1,
                100,
                &vehicleParams
            );
            if (newPilgrimDistUI != curPilgrimDistUI) {
                float newPilgrimDistVal = FromVehicleSlider(newPilgrimDistUI, 15.25f, 45.0f);
                Config::CustomPilgrimDist.store(newPilgrimDistVal);
                bChanged = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            std::cout << "ui: Exception in RenderCustomCameraOptions!\n";
            return;
        }

        if (bChanged) {
            RebuildCameraShellcode();
            VehicleCamera::RebuildShellcode();
            SaveConfig();
        }
    }

    static int __fastcall Detour_EndCyclicCallSite(void* pUIContext) {
        int result = fnEndCyclicOption(pUIContext);
        RenderCustomCameraOptions(pUIContext);
        return result;
    }

    bool Initialize() {
        const char* modName = "NMS.exe";

        std::cout << "ui: resolving dynamic string cross-references\n";

        // UI_OPTIONS_COMFORT string xref
        uintptr_t strComfort = FindString(modName, "UI_OPTIONS_COMFORT");
        if (!strComfort) {
            std::cout << "ui: failed to locate 'UI_OPTIONS_COMFORT'\n";
            return false;
        }
        uintptr_t xrefComfort = FindRipRef(modName, strComfort);
        if (!xrefComfort) {
            std::cout << "ui: failed to locate XREF for 'UI_OPTIONS_COMFORT'\n";
            return false;
        }

        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION pFunc = RtlLookupFunctionEntry(static_cast<DWORD64>(xrefComfort), &imageBase, nullptr);
        if (!pFunc) {
            std::cout << "ui: failed to query function entry via RtlLookupFunctionEntry\n";
            return false;
        }

        uintptr_t funcStart = static_cast<uintptr_t>(imageBase + pFunc->BeginAddress);
        uintptr_t funcEnd = static_cast<uintptr_t>(imageBase + pFunc->EndAddress);
        size_t funcSize = funcEnd - funcStart;

        std::cout << "ui: menu function range 0x" << std::hex << funcStart 
                  << "-0x" << funcEnd << " (" << std::dec << funcSize << " bytes)\n";

        // AddSectionHeader
        uintptr_t callHeader = FindNthCallForward(xrefComfort, 0x100, 1);
        if (!callHeader || callHeader >= funcEnd) {
            std::cout << "ui: AddSectionHeader out of function bounds\n";
            return false;
        }
        fnAddSectionHeader = reinterpret_cast<tAddSectionHeader>(ResolveCallTarget(callHeader));
        std::cout << "ui: AddSectionHeader @ 0x" << std::hex << (uintptr_t)fnAddSectionHeader << std::dec << "\n";

        // AddToggleOption (UI_HEAD_BOB)
        uintptr_t strHeadBob = FindString(modName, "UI_HEAD_BOB");
        if (!strHeadBob) {
            std::cout << "ui: failed to locate 'UI_HEAD_BOB'\n";
            return false;
        }
        uintptr_t xrefHeadBob = FindRipRef(modName, strHeadBob);
        if (!xrefHeadBob || xrefHeadBob < funcStart || xrefHeadBob >= funcEnd) {
            std::cout << "ui: 'UI_HEAD_BOB' XREF out of function bounds\n";
            return false;
        }
        uintptr_t callToggle = FindNthCallForward(xrefHeadBob, 0x100, 1);
        if (!callToggle || callToggle >= funcEnd) {
            std::cout << "ui: AddToggleOption out of function bounds\n";
            return false;
        }
        fnAddToggleOption = reinterpret_cast<tAddToggleOption>(ResolveCallTarget(callToggle));
        std::cout << "ui: AddToggleOption  @ 0x" << std::hex << (uintptr_t)fnAddToggleOption << std::dec << "\n";

        // AddIntSlider (UI_OPTIONS_CAMERA_SHAKE_L)
        uintptr_t strShake = FindString(modName, "UI_OPTIONS_CAMERA_SHAKE_L");
        if (!strShake) {
            std::cout << "ui: failed to locate 'UI_OPTIONS_CAMERA_SHAKE_L'\n";
            return false;
        }
        uintptr_t xrefShake = FindRipRef(modName, strShake);
        if (!xrefShake || xrefShake < funcStart || xrefShake >= funcEnd) {
            std::cout << "ui: 'UI_OPTIONS_CAMERA_SHAKE_L' XREF out of function bounds\n";
            return false;
        }
        uintptr_t callSlider = FindNthCallForward(xrefShake, 0x100, 1);
        if (!callSlider || callSlider >= funcEnd) {
            std::cout << "ui: AddIntSlider out of function bounds\n";
            return false;
        }
        fnAddIntSlider = reinterpret_cast<tAddIntSlider>(ResolveCallTarget(callSlider));
        std::cout << "ui: AddIntSlider     @ 0x" << std::hex << (uintptr_t)fnAddIntSlider << std::dec << "\n";

        // EndCyclicOption hook site (UI_HAND_RIGHT / UI_DOMINANT_HAND)
        uintptr_t candidateCallSite = 0;

        uintptr_t strHandRight = FindString(modName, "UI_HAND_RIGHT");
        if (strHandRight) {
            uintptr_t xrefHandRight = FindRipRef(modName, strHandRight);
            if (xrefHandRight >= funcStart && xrefHandRight < funcEnd) {
                candidateCallSite = FindNthCallForward(xrefHandRight, 0x50, 2);
            }
        }

        if (!candidateCallSite) {
            uintptr_t strDominantHand = FindString(modName, "UI_DOMINANT_HAND");
            if (strDominantHand) {
                uintptr_t xrefDominantHand = FindRipRef(modName, strDominantHand);
                if (xrefDominantHand >= funcStart && xrefDominantHand < funcEnd) {
                    candidateCallSite = FindNthCallForward(xrefDominantHand, 0x100, 4);
                }
            }
        }

        s_CallSiteEndCyclic = candidateCallSite;
        if (!s_CallSiteEndCyclic || s_CallSiteEndCyclic < funcStart || s_CallSiteEndCyclic >= funcEnd) {
            std::cout << "ui: hook call site out of function bounds\n";
            return false;
        }

        fnEndCyclicOption = reinterpret_cast<tEndCyclicOption>(ResolveCallTarget(s_CallSiteEndCyclic));
        std::cout << "ui: EndCyclicOption  @ 0x" << std::hex << (uintptr_t)fnEndCyclicOption << std::dec << "\n";
        std::cout << "ui: call site hook @ 0x" << std::hex << s_CallSiteEndCyclic << std::dec << "\n";

        if (!fnAddSectionHeader || !fnAddToggleOption || !fnAddIntSlider || !fnEndCyclicOption) {
            std::cout << "ui: failed to resolve UI function pointers\n";
            return false;
        }

        if (*reinterpret_cast<uint8_t*>(s_CallSiteEndCyclic) != 0xE8) {
            std::cout << "ui: target is not a call instruction\n";
            return false;
        }

        // Trampoline
        s_pRelayTrampoline = AllocateNearAddress(s_CallSiteEndCyclic, 0x40);
        if (!s_pRelayTrampoline) {
            std::cout << "ui: failed to allocate relay trampoline memory\n";
            return false;
        }

        int64_t fullDistance = reinterpret_cast<uintptr_t>(s_pRelayTrampoline) - (s_CallSiteEndCyclic + 5);
        if (fullDistance < INT32_MIN || fullDistance > INT32_MAX) {
            std::cout << "ui: relay displacement out of range\n";
            return false;
        }

        uint8_t relayStub[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        uintptr_t detourAddr = reinterpret_cast<uintptr_t>(&Detour_EndCyclicCallSite);
        memcpy(&relayStub[6], &detourAddr, sizeof(uintptr_t));

        DWORD oldProtect;
        VirtualProtect(s_pRelayTrampoline, sizeof(relayStub), PAGE_READWRITE, &oldProtect);
        memcpy(s_pRelayTrampoline, relayStub, sizeof(relayStub));
        VirtualProtect(s_pRelayTrampoline, sizeof(relayStub), PAGE_EXECUTE_READ, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), s_pRelayTrampoline, sizeof(relayStub));

        VirtualProtect(reinterpret_cast<void*>(s_CallSiteEndCyclic), 5, PAGE_EXECUTE_READWRITE, &oldProtect);
        int32_t relTarget = static_cast<int32_t>(fullDistance);
        *reinterpret_cast<int32_t*>(s_CallSiteEndCyclic + 1) = relTarget;
        VirtualProtect(reinterpret_cast<void*>(s_CallSiteEndCyclic), 5, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(s_CallSiteEndCyclic), 5);

        std::cout << "ui: camera menu hooked\n";
        return true;
    }
}