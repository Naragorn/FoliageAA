// The method-table hooks against a real Direct3D 9 device of this machine:
// the slot numbers are right (BeginScene and SetRenderState reach the hooks),
// the originals still run, the direct call bypasses the hook, a second
// install is refused, and removal restores the table exactly.

#include "DeviceHook.h"
#include "LeafShaders.h"
#include "TestDevice.h"

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what);
	}
}

int g_beginScenes = 0;
void* g_beginSceneDevice = nullptr;
int g_renderStates = 0;
uint32_t g_lastState = 0;
uint32_t g_lastValue = 0;

void OnBeginScene(void* device) {
	++g_beginScenes;
	g_beginSceneDevice = device;
}

void OnRenderState(void*, uint32_t state, uint32_t value) {
	++g_renderStates;
	g_lastState = state;
	g_lastValue = value;
}

int g_filters = 0;
void* g_filterSaw = nullptr;
void* g_filterAnswer = nullptr;  // null = answer with what was asked
void* FilterPixelShader(void*, void* shader) {
	++g_filters;
	g_filterSaw = shader;
	return g_filterAnswer != nullptr ? g_filterAnswer : shader;
}

int g_created = 0;
void* g_createdShader = nullptr;
void OnPixelShaderCreated(void*, void* shader) {
	++g_created;
	g_createdShader = shader;
}

void TestVtableGuard() {
	Check(!foliageaa::LooksLikeVtable(nullptr, 3), "null is no table");
	void* zeros[4] = {nullptr, nullptr, nullptr, nullptr};
	Check(!foliageaa::LooksLikeVtable(zeros, 4), "zero entries are no table");
	void* low[2] = {reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000)};
	Check(!foliageaa::LooksLikeVtable(low, 2), "entries under 64 KiB are no table");
	void* code[2] = {reinterpret_cast<void*>(&TestVtableGuard), reinterpret_cast<void*>(&OnBeginScene)};
	Check(foliageaa::LooksLikeVtable(code, 2), "code addresses pass");
	Check(!foliageaa::LooksLikeVtable(code, 0), "zero depth refused");
	Check(!foliageaa::LooksLikeVtable(reinterpret_cast<void* const*>(0x10), 1), "unmapped memory refused");

	char error[160] = {};
	const foliageaa::HookCallbacks callbacks{&OnBeginScene, &OnRenderState, &FilterPixelShader, &OnPixelShaderCreated};
	Check(!foliageaa::InstallDeviceHooks(nullptr, callbacks, error, sizeof(error)) &&
	          std::strstr(error, "not readable") != nullptr,
	      "install on null refused");
	void** fakeObject[1] = {zeros};
	Check(!foliageaa::InstallDeviceHooks(fakeObject, callbacks, error, sizeof(error)) &&
	          std::strstr(error, "method table") != nullptr,
	      "install on a zero table refused");
	Check(!foliageaa::AreDeviceHooksInstalled(), "nothing installed after refusals");
}

void TestRealDevice() {
	TestDevice test;
	if (!test.Create()) {
		Check(false, "a Direct3D 9 device could be created");
		return;
	}
	void** vtable = *reinterpret_cast<void***>(test.device);
	void* const originalBegin = vtable[foliageaa::kSlotBeginScene];
	void* const originalSet = vtable[foliageaa::kSlotSetRenderState];
	void* const originalCreatePs = vtable[foliageaa::kSlotCreatePixelShader];
	void* const originalSetPs = vtable[foliageaa::kSlotSetPixelShader];

	char error[160] = {};
	const foliageaa::HookCallbacks callbacks{&OnBeginScene, &OnRenderState, &FilterPixelShader, &OnPixelShaderCreated};
	Check(foliageaa::InstallDeviceHooks(test.device, callbacks, error, sizeof(error)), "install on a real device");
	Check(foliageaa::AreDeviceHooksInstalled(), "reports installed");
	Check(!foliageaa::InstallDeviceHooks(test.device, callbacks, error, sizeof(error)) &&
	          std::strstr(error, "already installed") != nullptr,
	      "second install refused");

	// The slot numbers: calling the interface's own methods must reach the
	// hooks, and the originals must still have run (BeginScene succeeded, the
	// render state reads back).
	Check(SUCCEEDED(test.device->BeginScene()), "hooked BeginScene succeeds");
	Check(g_beginScenes == 1 && g_beginSceneDevice == test.device, "BeginScene reached the hook with the device");
	Check(SUCCEEDED(test.device->EndScene()), "EndScene after hooked BeginScene");
	// A failing BeginScene (nested) does not reach the callback.
	test.device->BeginScene();
	const HRESULT nested = test.device->BeginScene();
	Check(FAILED(nested), "nested BeginScene fails as D3D specifies");
	Check(g_beginScenes == 2, "failed BeginScene not reported");
	test.device->EndScene();

	Check(SUCCEEDED(test.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE)), "hooked SetRenderState succeeds");
	Check(g_renderStates == 1 && g_lastState == D3DRS_ALPHATESTENABLE && g_lastValue == TRUE,
	      "SetRenderState reached the hook with state and value");
	DWORD readBack = 0;
	test.device->GetRenderState(D3DRS_ALPHATESTENABLE, &readBack);
	Check(readBack == TRUE, "the original SetRenderState ran");

	Check(foliageaa::SetRenderStateDirect(test.device, D3DRS_ALPHAREF, 0x40) >= 0, "direct call succeeds");
	Check(g_renderStates == 1, "direct call bypasses the hook");
	test.device->GetRenderState(D3DRS_ALPHAREF, &readBack);
	Check(readBack == 0x40, "direct call reached the device");

	// The pixel shader slots: creating a shader reaches the created callback
	// with the object the runtime returned; setting one goes through the
	// filter, whose answer is what the device ends up with.
	IDirect3DPixelShader9* leaf = nullptr;
	Check(SUCCEEDED(test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(foliageaa::kLeaf2000), &leaf)) &&
	          leaf != nullptr,
	      "vanilla STLEAF2000 bytes make a shader on this device");
	Check(g_created == 1 && g_createdShader == leaf, "CreatePixelShader reached the hook with the new object");
	IDirect3DPixelShader9* other = nullptr;
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(foliageaa::kLeaf2001), &other);
	Check(g_created == 2 && g_createdShader == other, "second creation reported");
	// A failed creation (garbage bytes) is not reported.
	const DWORD garbage[2] = {0x12345678, 0};
	IDirect3DPixelShader9* none = nullptr;
	Check(FAILED(test.device->CreatePixelShader(garbage, &none)), "garbage bytes are refused by the runtime");
	Check(g_created == 2, "failed creation not reported");

	Check(SUCCEEDED(test.device->SetPixelShader(leaf)), "hooked SetPixelShader succeeds");
	Check(g_filters == 1 && g_filterSaw == leaf, "SetPixelShader reached the filter with the shader");
	IDirect3DPixelShader9* current = nullptr;
	test.device->GetPixelShader(&current);
	Check(current == leaf, "pass-through answer sets the asked shader");
	if (current != nullptr) current->Release();
	g_filterAnswer = other;
	test.device->SetPixelShader(leaf);
	test.device->GetPixelShader(&current);
	Check(g_filters == 2 && current == other, "the filter's answer is what the device gets");
	if (current != nullptr) current->Release();
	g_filterAnswer = nullptr;
	test.device->SetPixelShader(nullptr);
	Check(g_filters == 3 && g_filterSaw == nullptr, "null goes through the filter too");
	test.device->GetPixelShader(&current);
	Check(current == nullptr, "null set");

	foliageaa::RemoveDeviceHooks();
	Check(!foliageaa::AreDeviceHooksInstalled(), "reports removed");
	Check(vtable[foliageaa::kSlotBeginScene] == originalBegin && vtable[foliageaa::kSlotSetRenderState] == originalSet &&
	          vtable[foliageaa::kSlotCreatePixelShader] == originalCreatePs &&
	          vtable[foliageaa::kSlotSetPixelShader] == originalSetPs,
	      "table restored exactly");
	test.device->BeginScene();
	test.device->EndScene();
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	test.device->SetPixelShader(leaf);
	IDirect3DPixelShader9* afterRemoval = nullptr;
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(foliageaa::kLeaf2000), &afterRemoval);
	Check(g_beginScenes == 2 && g_renderStates == 1 && g_filters == 3 && g_created == 2,
	      "nothing reaches the hooks after removal");
	test.device->SetPixelShader(nullptr);
	if (afterRemoval != nullptr) afterRemoval->Release();
	leaf->Release();
	other->Release();
	foliageaa::RemoveDeviceHooks();
	Check(!foliageaa::AreDeviceHooksInstalled(), "second removal is harmless");

	// The direct call without hooks goes through the table itself.
	Check(foliageaa::SetRenderStateDirect(test.device, D3DRS_ALPHAREF, 0x41) >= 0, "direct call without hooks");
	test.device->GetRenderState(D3DRS_ALPHAREF, &readBack);
	Check(readBack == 0x41, "direct call without hooks reached the device");

	test.Destroy();
}

}  // namespace

int main() {
	TestVtableGuard();
	TestRealDevice();
	std::printf(g_failures == 0 ? "DeviceHookTest: all passed\n" : "DeviceHookTest: %d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
