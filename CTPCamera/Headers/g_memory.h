#pragma once
#include <windows.h>
#include <vector>
#include <cstdint>

void ParseSignature(const char* signature, std::vector<int>& bytes);
uintptr_t FindPattern(const char* moduleName, const char* signature);
void* AllocateNearAddress(uintptr_t targetAddr, size_t size);

// Dynamic XREF & Instruction Helpers
uintptr_t FindString(const char* moduleName, const char* str);
uintptr_t FindRipRef(const char* moduleName, uintptr_t targetAddr, uint8_t regOpcode = 0x15);
uintptr_t ResolveCallTarget(uintptr_t callInstructionAddr);
uintptr_t FindNthCallForward(uintptr_t startAddr, size_t maxScanBytes, size_t targetCallIndex);