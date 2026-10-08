#include "DeviceHook.h"

#include <windows.h>

#include <cstdio>

namespace foliageaa {

namespace {

using BeginSceneFn = HRESULT(__stdcall*)(void* self);
using SetRenderStateFn = HRESULT(__stdcall*)(void* self, uint32_t state, uint32_t value);
using CreatePixelShaderFn = HRESULT(__stdcall*)(void* self, const uint32_t* function, void** shader);
using SetPixelShaderFn = HRESULT(__stdcall*)(void* self, void* shader);

void** g_vtable = nullptr;
BeginSceneFn g_originalBeginScene = nullptr;
SetRenderStateFn g_originalSetRenderState = nullptr;
CreatePixelShaderFn g_originalCreatePixelShader = nullptr;
SetPixelShaderFn g_originalSetPixelShader = nullptr;
HookCallbacks g_callbacks{nullptr, nullptr, nullptr, nullptr};

HRESULT __stdcall HookedBeginScene(void* self) {
	const HRESULT hr = g_originalBeginScene(self);
	if (SUCCEEDED(hr) && g_callbacks.onBeginScene != nullptr) {
		g_callbacks.onBeginScene(self);
	}
	return hr;
}

HRESULT __stdcall HookedSetRenderState(void* self, uint32_t state, uint32_t value) {
	const HRESULT hr = g_originalSetRenderState(self, state, value);
	if (g_callbacks.onRenderState != nullptr) {
		g_callbacks.onRenderState(self, state, value);
	}
	return hr;
}

HRESULT __stdcall HookedCreatePixelShader(void* self, const uint32_t* function, void** shader) {
	const HRESULT hr = g_originalCreatePixelShader(self, function, shader);
	if (SUCCEEDED(hr) && shader != nullptr && *shader != nullptr && g_callbacks.onPixelShaderCreated != nullptr) {
		g_callbacks.onPixelShaderCreated(self, *shader);
	}
	return hr;
}

HRESULT __stdcall HookedSetPixelShader(void* self, void* shader) {
	if (g_callbacks.filterPixelShader != nullptr) {
		shader = g_callbacks.filterPixelShader(self, shader);
	}
	return g_originalSetPixelShader(self, shader);
}

bool WriteEntry(void** vtable, uint32_t slot, void* value) {
	void** entry = vtable + slot;
	DWORD previous = 0;
	if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &previous)) {
		return false;
	}
	*entry = value;
	DWORD ignored = 0;
	VirtualProtect(entry, sizeof(void*), previous, &ignored);
	return true;
}

bool IsReadable(const void* address, size_t bytes) {
	MEMORY_BASIC_INFORMATION info{};
	if (VirtualQuery(address, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT) {
		return false;
	}
	const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
	                       PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
	if ((info.Protect & readable) == 0 || (info.Protect & PAGE_GUARD) != 0) {
		return false;
	}
	const auto* end = static_cast<const unsigned char*>(info.BaseAddress) + info.RegionSize;
	return static_cast<const unsigned char*>(address) + bytes <= end;
}

struct Slot {
	uint32_t index;
	void* hook;
	void** original;
};

}  // namespace

bool LooksLikeVtable(void* const* vtable, uint32_t entries) {
	if (vtable == nullptr || entries == 0 || !IsReadable(vtable, entries * sizeof(void*))) {
		return false;
	}
	for (uint32_t i = 0; i < entries; ++i) {
		// Code lives well above the first 64 KiB, which Windows never maps.
		if (reinterpret_cast<uintptr_t>(vtable[i]) < 0x10000) {
			return false;
		}
	}
	return true;
}

bool InstallDeviceHooks(void* device, const HookCallbacks& callbacks, char* error, size_t errorLength) {
	if (g_vtable != nullptr) {
		std::snprintf(error, errorLength, "hooks are already installed");
		return false;
	}
	if (device == nullptr || !IsReadable(device, sizeof(void*))) {
		std::snprintf(error, errorLength, "device pointer %p is not readable", device);
		return false;
	}
	void** vtable = *static_cast<void***>(device);
	if (!LooksLikeVtable(vtable, kSlotLast + 1)) {
		std::snprintf(error, errorLength, "device table %p does not look like a method table", static_cast<void*>(vtable));
		return false;
	}
	void* originals[4] = {vtable[kSlotBeginScene], vtable[kSlotSetRenderState], vtable[kSlotCreatePixelShader],
	                      vtable[kSlotSetPixelShader]};
	const Slot slots[4] = {
		{kSlotBeginScene, reinterpret_cast<void*>(&HookedBeginScene), &originals[0]},
		{kSlotSetRenderState, reinterpret_cast<void*>(&HookedSetRenderState), &originals[1]},
		{kSlotCreatePixelShader, reinterpret_cast<void*>(&HookedCreatePixelShader), &originals[2]},
		{kSlotSetPixelShader, reinterpret_cast<void*>(&HookedSetPixelShader), &originals[3]},
	};
	for (size_t i = 0; i < 4; ++i) {
		if (!WriteEntry(vtable, slots[i].index, slots[i].hook)) {
			for (size_t j = 0; j < i; ++j) {
				WriteEntry(vtable, slots[j].index, *slots[j].original);
			}
			std::snprintf(error, errorLength, "could not make the method table writable (error %lu)", GetLastError());
			return false;
		}
	}
	g_callbacks = callbacks;
	g_originalBeginScene = reinterpret_cast<BeginSceneFn>(originals[0]);
	g_originalSetRenderState = reinterpret_cast<SetRenderStateFn>(originals[1]);
	g_originalCreatePixelShader = reinterpret_cast<CreatePixelShaderFn>(originals[2]);
	g_originalSetPixelShader = reinterpret_cast<SetPixelShaderFn>(originals[3]);
	g_vtable = vtable;
	return true;
}

void RemoveDeviceHooks() {
	if (g_vtable == nullptr) {
		return;
	}
	WriteEntry(g_vtable, kSlotBeginScene, reinterpret_cast<void*>(g_originalBeginScene));
	WriteEntry(g_vtable, kSlotSetRenderState, reinterpret_cast<void*>(g_originalSetRenderState));
	WriteEntry(g_vtable, kSlotCreatePixelShader, reinterpret_cast<void*>(g_originalCreatePixelShader));
	WriteEntry(g_vtable, kSlotSetPixelShader, reinterpret_cast<void*>(g_originalSetPixelShader));
	g_vtable = nullptr;
	g_originalBeginScene = nullptr;
	g_originalSetRenderState = nullptr;
	g_originalCreatePixelShader = nullptr;
	g_originalSetPixelShader = nullptr;
	g_callbacks = HookCallbacks{nullptr, nullptr, nullptr, nullptr};
}

bool AreDeviceHooksInstalled() {
	return g_vtable != nullptr;
}

long SetRenderStateDirect(void* device, uint32_t state, uint32_t value) {
	if (g_originalSetRenderState != nullptr) {
		return g_originalSetRenderState(device, state, value);
	}
	void** vtable = *static_cast<void***>(device);
	return reinterpret_cast<SetRenderStateFn>(vtable[kSlotSetRenderState])(device, state, value);
}

}  // namespace foliageaa
