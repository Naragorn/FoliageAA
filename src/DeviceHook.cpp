#include "DeviceHook.h"

#include <windows.h>

#include <cstdio>

namespace foliageaa {

namespace {

using BeginSceneFn = HRESULT(__stdcall*)(void* self);
using SetRenderStateFn = HRESULT(__stdcall*)(void* self, uint32_t state, uint32_t value);
using CreateShaderFn = HRESULT(__stdcall*)(void* self, const uint32_t* function, void** shader);
using SetShaderFn = HRESULT(__stdcall*)(void* self, void* shader);
using SetVertexShaderConstantFFn = HRESULT(__stdcall*)(void* self, uint32_t startRegister, const float* data, uint32_t count);
using SetViewportFn = HRESULT(__stdcall*)(void* self, const void* viewport);
using SetRenderTargetFn = HRESULT(__stdcall*)(void* self, uint32_t index, void* surface);
using DrawPrimitiveFn = HRESULT(__stdcall*)(void* self, uint32_t type, uint32_t startVertex, uint32_t primitiveCount);
using DrawIndexedPrimitiveFn = HRESULT(__stdcall*)(void* self, uint32_t type, int baseVertexIndex, uint32_t minIndex,
                                                   uint32_t numVertices, uint32_t startIndex, uint32_t primitiveCount);

void** g_vtable = nullptr;
HookCallbacks g_callbacks{};

BeginSceneFn g_originalBeginScene = nullptr;
SetRenderStateFn g_originalSetRenderState = nullptr;
CreateShaderFn g_originalCreatePixelShader = nullptr;
SetShaderFn g_originalSetPixelShader = nullptr;
CreateShaderFn g_originalCreateVertexShader = nullptr;
SetShaderFn g_originalSetVertexShader = nullptr;
SetVertexShaderConstantFFn g_originalSetVertexShaderConstantF = nullptr;
SetViewportFn g_originalSetViewport = nullptr;
SetRenderTargetFn g_originalSetRenderTarget = nullptr;
DrawPrimitiveFn g_originalDrawPrimitive = nullptr;
DrawIndexedPrimitiveFn g_originalDrawIndexedPrimitive = nullptr;

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

HRESULT __stdcall HookedCreateVertexShader(void* self, const uint32_t* function, void** shader) {
	const HRESULT hr = g_originalCreateVertexShader(self, function, shader);
	if (SUCCEEDED(hr) && shader != nullptr && *shader != nullptr && g_callbacks.onVertexShaderCreated != nullptr) {
		g_callbacks.onVertexShaderCreated(self, *shader);
	}
	return hr;
}

HRESULT __stdcall HookedSetVertexShader(void* self, void* shader) {
	const HRESULT hr = g_originalSetVertexShader(self, shader);
	if (g_callbacks.onVertexShader != nullptr) {
		g_callbacks.onVertexShader(self, shader);
	}
	return hr;
}

HRESULT __stdcall HookedSetVertexShaderConstantF(void* self, uint32_t startRegister, const float* data, uint32_t count) {
	const HRESULT hr = g_originalSetVertexShaderConstantF(self, startRegister, data, count);
	if (g_callbacks.onVertexConstants != nullptr) {
		g_callbacks.onVertexConstants(self, startRegister, data, count);
	}
	return hr;
}

HRESULT __stdcall HookedSetViewport(void* self, const void* viewport) {
	const HRESULT hr = g_originalSetViewport(self, viewport);
	if (SUCCEEDED(hr) && viewport != nullptr && g_callbacks.onViewport != nullptr) {
		g_callbacks.onViewport(self, viewport);
	}
	return hr;
}

HRESULT __stdcall HookedSetRenderTarget(void* self, uint32_t index, void* surface) {
	const HRESULT hr = g_originalSetRenderTarget(self, index, surface);
	if (SUCCEEDED(hr) && g_callbacks.onRenderTarget != nullptr) {
		g_callbacks.onRenderTarget(self, index, surface);
	}
	return hr;
}

struct DrawContext {
	void* self;
	uint32_t type;
	uint32_t startVertex;
	uint32_t primitiveCount;
};

long IssueDraw(void* raw) {
	auto* c = static_cast<DrawContext*>(raw);
	return g_originalDrawPrimitive(c->self, c->type, c->startVertex, c->primitiveCount);
}

HRESULT __stdcall HookedDrawPrimitive(void* self, uint32_t type, uint32_t startVertex, uint32_t primitiveCount) {
	DrawContext context{self, type, startVertex, primitiveCount};
	if (g_callbacks.filterDraw != nullptr) {
		return g_callbacks.filterDraw(self, &IssueDraw, &context);
	}
	return IssueDraw(&context);
}

struct IndexedDrawContext {
	void* self;
	uint32_t type;
	int baseVertexIndex;
	uint32_t minIndex;
	uint32_t numVertices;
	uint32_t startIndex;
	uint32_t primitiveCount;
};

long IssueIndexedDraw(void* raw) {
	auto* c = static_cast<IndexedDrawContext*>(raw);
	return g_originalDrawIndexedPrimitive(c->self, c->type, c->baseVertexIndex, c->minIndex, c->numVertices,
	                                      c->startIndex, c->primitiveCount);
}

HRESULT __stdcall HookedDrawIndexedPrimitive(void* self, uint32_t type, int baseVertexIndex, uint32_t minIndex,
                                             uint32_t numVertices, uint32_t startIndex, uint32_t primitiveCount) {
	IndexedDrawContext context{self, type, baseVertexIndex, minIndex, numVertices, startIndex, primitiveCount};
	if (g_callbacks.filterDraw != nullptr) {
		return g_callbacks.filterDraw(self, &IssueIndexedDraw, &context);
	}
	return IssueIndexedDraw(&context);
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
	void** original;  // where the entry found in the table is kept
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
	const Slot slots[kHookedSlots] = {
		{kSlotSetRenderTarget, reinterpret_cast<void*>(&HookedSetRenderTarget), reinterpret_cast<void**>(&g_originalSetRenderTarget)},
		{kSlotBeginScene, reinterpret_cast<void*>(&HookedBeginScene), reinterpret_cast<void**>(&g_originalBeginScene)},
		{kSlotSetViewport, reinterpret_cast<void*>(&HookedSetViewport), reinterpret_cast<void**>(&g_originalSetViewport)},
		{kSlotSetRenderState, reinterpret_cast<void*>(&HookedSetRenderState), reinterpret_cast<void**>(&g_originalSetRenderState)},
		{kSlotDrawPrimitive, reinterpret_cast<void*>(&HookedDrawPrimitive), reinterpret_cast<void**>(&g_originalDrawPrimitive)},
		{kSlotDrawIndexedPrimitive, reinterpret_cast<void*>(&HookedDrawIndexedPrimitive), reinterpret_cast<void**>(&g_originalDrawIndexedPrimitive)},
		{kSlotCreateVertexShader, reinterpret_cast<void*>(&HookedCreateVertexShader), reinterpret_cast<void**>(&g_originalCreateVertexShader)},
		{kSlotSetVertexShader, reinterpret_cast<void*>(&HookedSetVertexShader), reinterpret_cast<void**>(&g_originalSetVertexShader)},
		{kSlotSetVertexShaderConstantF, reinterpret_cast<void*>(&HookedSetVertexShaderConstantF), reinterpret_cast<void**>(&g_originalSetVertexShaderConstantF)},
		{kSlotCreatePixelShader, reinterpret_cast<void*>(&HookedCreatePixelShader), reinterpret_cast<void**>(&g_originalCreatePixelShader)},
		{kSlotSetPixelShader, reinterpret_cast<void*>(&HookedSetPixelShader), reinterpret_cast<void**>(&g_originalSetPixelShader)},
	};
	for (int i = 0; i < kHookedSlots; ++i) {
		*slots[i].original = vtable[slots[i].index];
	}
	for (int i = 0; i < kHookedSlots; ++i) {
		if (!WriteEntry(vtable, slots[i].index, slots[i].hook)) {
			for (int j = 0; j < i; ++j) {
				WriteEntry(vtable, slots[j].index, *slots[j].original);
			}
			std::snprintf(error, errorLength, "could not make the method table writable (error %lu)", GetLastError());
			return false;
		}
	}
	g_callbacks = callbacks;
	g_vtable = vtable;
	return true;
}

void RemoveDeviceHooks() {
	if (g_vtable == nullptr) {
		return;
	}
	WriteEntry(g_vtable, kSlotSetRenderTarget, reinterpret_cast<void*>(g_originalSetRenderTarget));
	WriteEntry(g_vtable, kSlotBeginScene, reinterpret_cast<void*>(g_originalBeginScene));
	WriteEntry(g_vtable, kSlotSetViewport, reinterpret_cast<void*>(g_originalSetViewport));
	WriteEntry(g_vtable, kSlotSetRenderState, reinterpret_cast<void*>(g_originalSetRenderState));
	WriteEntry(g_vtable, kSlotDrawPrimitive, reinterpret_cast<void*>(g_originalDrawPrimitive));
	WriteEntry(g_vtable, kSlotDrawIndexedPrimitive, reinterpret_cast<void*>(g_originalDrawIndexedPrimitive));
	WriteEntry(g_vtable, kSlotCreateVertexShader, reinterpret_cast<void*>(g_originalCreateVertexShader));
	WriteEntry(g_vtable, kSlotSetVertexShader, reinterpret_cast<void*>(g_originalSetVertexShader));
	WriteEntry(g_vtable, kSlotSetVertexShaderConstantF, reinterpret_cast<void*>(g_originalSetVertexShaderConstantF));
	WriteEntry(g_vtable, kSlotCreatePixelShader, reinterpret_cast<void*>(g_originalCreatePixelShader));
	WriteEntry(g_vtable, kSlotSetPixelShader, reinterpret_cast<void*>(g_originalSetPixelShader));
	g_vtable = nullptr;
	g_callbacks = HookCallbacks{};
	g_originalBeginScene = nullptr;
	g_originalSetRenderState = nullptr;
	g_originalCreatePixelShader = nullptr;
	g_originalSetPixelShader = nullptr;
	g_originalCreateVertexShader = nullptr;
	g_originalSetVertexShader = nullptr;
	g_originalSetVertexShaderConstantF = nullptr;
	g_originalSetViewport = nullptr;
	g_originalSetRenderTarget = nullptr;
	g_originalDrawPrimitive = nullptr;
	g_originalDrawIndexedPrimitive = nullptr;
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

long SetVertexShaderConstantFDirect(void* device, uint32_t startRegister, const float* data, uint32_t count) {
	if (g_originalSetVertexShaderConstantF != nullptr) {
		return g_originalSetVertexShaderConstantF(device, startRegister, data, count);
	}
	void** vtable = *static_cast<void***>(device);
	return reinterpret_cast<SetVertexShaderConstantFFn>(vtable[kSlotSetVertexShaderConstantF])(device, startRegister, data, count);
}

}  // namespace foliageaa
