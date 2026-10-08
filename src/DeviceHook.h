#pragma once

#include <cstddef>
#include <cstdint>

// Two entries of Oblivion's IDirect3DDevice9 method table, replaced so the
// plugin learns each frame's BeginScene and each render-state write.
//
// A table entry rather than a code patch: every COM object reaches its
// methods through a table of pointers, so swapping one pointer needs no
// address inside the runtime and no assumptions about its code - which
// matters because that runtime may be DXVK rather than Microsoft's d3d9.dll.
// The table belongs to the class, so this affects every device of that class
// in the process; a Direct3D 9 game has one. The table lives in read-only
// memory and is made writable for the write and put back.
//
// Slot numbers follow the method order of IDirect3DDevice9 in d3d9.h:
// IUnknown 0-2, TestCooperativeLevel 3, GetAvailableTextureMem 4,
// EvictManagedResources 5, GetDirect3D 6, GetDeviceCaps 7, GetDisplayMode 8,
// GetCreationParameters 9, SetCursorProperties 10, SetCursorPosition 11,
// ShowCursor 12, CreateAdditionalSwapChain 13, GetSwapChain 14,
// GetNumberOfSwapChains 15, Reset 16, Present 17, GetBackBuffer 18,
// GetRasterStatus 19, SetDialogBoxMode 20, SetGammaRamp 21, GetGammaRamp 22,
// CreateTexture 23, CreateVolumeTexture 24, CreateCubeTexture 25,
// CreateVertexBuffer 26, CreateIndexBuffer 27, CreateRenderTarget 28,
// CreateDepthStencilSurface 29, UpdateSurface 30, UpdateTexture 31,
// GetRenderTargetData 32, GetFrontBufferData 33, StretchRect 34,
// ColorFill 35, CreateOffscreenPlainSurface 36, SetRenderTarget 37,
// GetRenderTarget 38, SetDepthStencilSurface 39, GetDepthStencilSurface 40,
// BeginScene 41, EndScene 42, Clear 43, SetTransform 44, GetTransform 45,
// MultiplyTransform 46, SetViewport 47, GetViewport 48, SetMaterial 49,
// GetMaterial 50, SetLight 51, GetLight 52, LightEnable 53, GetLightEnable 54,
// SetClipPlane 55, GetClipPlane 56, SetRenderState 57, GetRenderState 58,
// ... SetIndices 104, GetIndices 105, CreatePixelShader 106,
// SetPixelShader 107, GetPixelShader 108.
// DeviceHookTest checks every hooked slot against a real device.

namespace foliageaa {

constexpr uint32_t kSlotBeginScene = 41;
constexpr uint32_t kSlotSetRenderState = 57;
constexpr uint32_t kSlotCreatePixelShader = 106;
constexpr uint32_t kSlotSetPixelShader = 107;
constexpr uint32_t kSlotLast = kSlotSetPixelShader;

// Called after the original BeginScene returned success.
using BeginSceneCallback = void (*)(void* device);
// Called after the original SetRenderState returned, with what was written.
using RenderStateCallback = void (*)(void* device, uint32_t state, uint32_t value);
// Called before the original SetPixelShader, with the shader the engine
// asked for; returns the shader to set instead (the same one to leave it).
using PixelShaderFilter = void* (*)(void* device, void* shader);
// Called after a successful original CreatePixelShader, with the new object.
using PixelShaderCreated = void (*)(void* device, void* shader);

struct HookCallbacks {
	BeginSceneCallback onBeginScene;
	RenderStateCallback onRenderState;
	PixelShaderFilter filterPixelShader;
	PixelShaderCreated onPixelShaderCreated;
};

// Whether a pointer about to be treated as a method table looks like one at
// the given depth: readable, and every entry a plausible code address.
bool LooksLikeVtable(void* const* vtable, uint32_t entries);

// Replaces the four entries. False, with a reason in error, if hooks are already
// installed (installing twice would chain a hook onto itself), the device or
// its table cannot be read, or the table cannot be made writable.
bool InstallDeviceHooks(void* device, const HookCallbacks& callbacks, char* error, size_t errorLength);

// Puts the original pointers back. Safe when nothing is installed.
void RemoveDeviceHooks();

bool AreDeviceHooksInstalled();

// SetRenderState through the original entry, for use inside the callbacks:
// going through the table again would re-enter the hook.
long SetRenderStateDirect(void* device, uint32_t state, uint32_t value);

}  // namespace foliageaa
