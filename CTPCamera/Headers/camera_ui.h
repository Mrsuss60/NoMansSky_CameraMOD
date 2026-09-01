#pragma once
#include <windows.h>
#include <cstdint>

namespace CameraUI {
    struct SliderExtraParams {
        int64_t stepConfig;
        const char* suffix;
        int64_t reserved;
    };

    typedef void(__fastcall* tAddSectionHeader)(void* pUIContext, const char* labelKey, __int64 iconId);
    typedef char(__fastcall* tAddToggleOption)(
        void* pUIContext,
        const char* labelKey,
        const char* descKey,
        char currentValue,
        char defaultValue,
        const char** pLabelsArray
        );
    typedef int(__fastcall* tAddIntSlider)(
        void* pUIContext,
        const char* labelKey,
        const char* descKey,
        int currentValue,
        int defaultValue,
        int minValue,
        int maxValue,
        SliderExtraParams* pExtraParams
        );
    typedef int(__fastcall* tEndCyclicOption)(void* pUIContext);

    bool Initialize();
    void RenderCustomCameraOptions(void* pUIContext);
}