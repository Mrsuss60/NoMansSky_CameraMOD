#pragma once
#include <windows.h>
#include <vector>


void ParseSignature(const char* signature, std::vector<int>& bytes);
uintptr_t FindPattern(const char* moduleName, const char* signature);
void* AllocateNearAddress(uintptr_t targetAddr, size_t size);