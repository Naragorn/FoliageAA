#pragma once

#include <cstddef>
#include <cstdint>

// Entries of Oblivion's IDirect3DDevice9 method table, replaced so the plugin
// learns each frame's BeginScene, the state the engine sets, and each draw -
// and can draw a leaf several times.
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
// CreateStateBlock 59, BeginStateBlock 60, EndStateBlock 61, SetClipStatus 62,
// GetClipStatus 63, GetTexture 64, SetTexture 65, GetTextureStageState 66,
// SetTextureStageState 67, GetSamplerState 68, SetSamplerState 69,
// ValidateDevice 70, SetPaletteEntries 71, GetPaletteEntries 72,
// SetCurrentTexturePalette 73, GetCurrentTexturePalette 74, SetScissorRect 75,
// GetScissorRect 76, SetSoftwareVertexProcessing 77,
// GetSoftwareVertexProcessing 78, SetNPatchMode 79, GetNPatchMode 80,
// DrawPrimitive 81, DrawIndexedPrimitive 82, DrawPrimitiveUP 83,
// DrawIndexedPrimitiveUP 84, ProcessVertices 85, CreateVertexDeclaration 86,
// SetVertexDeclaration 87, GetVertexDeclaration 88, SetFVF 89, GetFVF 90,
// CreateVertexShader 91, SetVertexShader 92, GetVertexShader 93,
// SetVertexShaderConstantF 94, GetVertexShaderConstantF 95, ...I 96, 97,
// ...B 98, 99, SetStreamSource 100, GetStreamSource 101,
// SetStreamSourceFreq 102, GetStreamSourceFreq 103, SetIndices 104,
// GetIndices 105, CreatePixelShader 106, SetPixelShader 107,
// GetPixelShader 108. DeviceHookTest checks every hooked slot against a
// real device.

namespace foliageaa {

constexpr uint32_t kSlotSetRenderTarget = 37;
constexpr uint32_t kSlotBeginScene = 41;
constexpr uint32_t kSlotSetViewport = 47;
constexpr uint32_t kSlotSetRenderState = 57;
constexpr uint32_t kSlotDrawPrimitive = 81;
constexpr uint32_t kSlotDrawIndexedPrimitive = 82;
constexpr uint32_t kSlotCreateVertexShader = 91;
constexpr uint32_t kSlotSetVertexShader = 92;
constexpr uint32_t kSlotSetVertexShaderConstantF = 94;
constexpr uint32_t kSlotCreatePixelShader = 106;
constexpr uint32_t kSlotSetPixelShader = 107;
constexpr uint32_t kSlotLast = kSlotSetPixelShader;
constexpr int kHookedSlots = 11;

// Called after the original BeginScene returned success.
using BeginSceneCallback = void (*)(void* device);
// Called after the original SetRenderState returned, with what was written.
using RenderStateCallback = void (*)(void* device, uint32_t state, uint32_t value);
// Called before the original SetPixelShader, with the shader the engine
// asked for; returns the shader to set instead (the same one to leave it).
using PixelShaderFilter = void* (*)(void* device, void* shader);
// Called after a successful original Create*Shader, with the new object.
using ShaderCreated = void (*)(void* device, void* shader);
// Called after the original SetVertexShader, with what was set (may be null).
using VertexShaderCallback = void (*)(void* device, void* shader);
// Called after the original SetVertexShaderConstantF, with its arguments.
using VertexConstantsCallback = void (*)(void* device, uint32_t startRegister, const float* data, uint32_t count);
// Called after the original SetViewport, with the D3DVIEWPORT9 it was given.
using ViewportCallback = void (*)(void* device, const void* viewport);
// Called after a successful original SetRenderTarget, with index and surface
// (null when the target was cleared).
using RenderTargetCallback = void (*)(void* device, uint32_t index, void* surface);
// Called instead of the original DrawPrimitive / DrawIndexedPrimitive. The
// filter calls issue(context) to perform the original call with the
// original arguments, as often as it likes, and returns the HRESULT to hand
// back to the engine.
using DrawIssue = long (*)(void* context);
using DrawFilter = long (*)(void* device, DrawIssue issue, void* context);

struct HookCallbacks {
	BeginSceneCallback onBeginScene;
	RenderStateCallback onRenderState;
	PixelShaderFilter filterPixelShader;
	ShaderCreated onPixelShaderCreated;
	VertexShaderCallback onVertexShader;
	ShaderCreated onVertexShaderCreated;
	VertexConstantsCallback onVertexConstants;
	ViewportCallback onViewport;
	RenderTargetCallback onRenderTarget;
	DrawFilter filterDraw;
};

// Whether a pointer about to be treated as a method table looks like one at
// the given depth: readable, and every entry a plausible code address.
bool LooksLikeVtable(void* const* vtable, uint32_t entries);

// Replaces the hooked entries. False, with a reason in error, if hooks are
// already installed (installing twice would chain a hook onto itself), the
// device or its table cannot be read, or the table cannot be made writable.
bool InstallDeviceHooks(void* device, const HookCallbacks& callbacks, char* error, size_t errorLength);

// Puts the original pointers back. Safe when nothing is installed.
void RemoveDeviceHooks();

bool AreDeviceHooksInstalled();

// Through the original entries, for use inside the callbacks: going through
// the table again would re-enter the hooks, and the shadow must not take the
// plugin's own writes for the engine's.
long SetRenderStateDirect(void* device, uint32_t state, uint32_t value);
long SetVertexShaderConstantFDirect(void* device, uint32_t startRegister, const float* data, uint32_t count);

}  // namespace foliageaa
