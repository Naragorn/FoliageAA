// The method-table hooks against a real Direct3D 9 device of this machine:
// every hooked slot number is right (the interface's own methods reach the
// hooks), the originals still run, the direct calls bypass the hooks, a
// second install is refused, and removal restores the table exactly.

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
void OnBeginScene(void* device) {
	++g_beginScenes;
	g_beginSceneDevice = device;
}

int g_renderStates = 0;
uint32_t g_lastState = 0;
uint32_t g_lastValue = 0;
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

int g_psCreated = 0;
void* g_psCreatedShader = nullptr;
void OnPixelShaderCreated(void*, void* shader) {
	++g_psCreated;
	g_psCreatedShader = shader;
}

int g_vsSets = 0;
void* g_vsSet = nullptr;
void OnVertexShader(void*, void* shader) {
	++g_vsSets;
	g_vsSet = shader;
}

int g_vsCreated = 0;
void* g_vsCreatedShader = nullptr;
void OnVertexShaderCreated(void*, void* shader) {
	++g_vsCreated;
	g_vsCreatedShader = shader;
}

int g_constants = 0;
uint32_t g_constantStart = 0;
uint32_t g_constantCount = 0;
float g_constantFirst = 0;
void OnVertexConstants(void*, uint32_t start, const float* data, uint32_t count) {
	++g_constants;
	g_constantStart = start;
	g_constantCount = count;
	g_constantFirst = data[0];
}

int g_viewports = 0;
uint32_t g_viewportWidth = 0;
void OnViewport(void*, const void* viewport) {
	++g_viewports;
	g_viewportWidth = static_cast<const D3DVIEWPORT9*>(viewport)->Width;
}

int g_targets = 0;
uint32_t g_targetIndex = 99;
void* g_targetSurface = nullptr;
void OnRenderTarget(void*, uint32_t index, void* surface) {
	++g_targets;
	g_targetIndex = index;
	g_targetSurface = surface;
}

int g_draws = 0;
int g_issuesPerDraw = 1;
long g_lastIssueResult = 0;
long g_drawAnswer = 0;  // what the filter hands back; 0 = the last issue's result
long FilterDraw(void*, foliageaa::DrawIssue issue, void* context) {
	++g_draws;
	for (int i = 0; i < g_issuesPerDraw; ++i) {
		g_lastIssueResult = issue(context);
	}
	return g_drawAnswer != 0 ? g_drawAnswer : g_lastIssueResult;
}

const foliageaa::HookCallbacks kCallbacks{&OnBeginScene,   &OnRenderState,        &FilterPixelShader, &OnPixelShaderCreated,
                                          &OnVertexShader, &OnVertexShaderCreated, &OnVertexConstants, &OnViewport,
                                          &OnRenderTarget, &FilterDraw};

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
	Check(!foliageaa::InstallDeviceHooks(nullptr, kCallbacks, error, sizeof(error)) &&
	          std::strstr(error, "not readable") != nullptr,
	      "install on null refused");
	void** fakeObject[1] = {zeros};
	Check(!foliageaa::InstallDeviceHooks(fakeObject, kCallbacks, error, sizeof(error)) &&
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
	void* originals[foliageaa::kSlotLast + 1];
	std::memcpy(originals, vtable, sizeof(originals));

	char error[160] = {};
	Check(foliageaa::InstallDeviceHooks(test.device, kCallbacks, error, sizeof(error)), "install on a real device");
	Check(foliageaa::AreDeviceHooksInstalled(), "reports installed");
	Check(!foliageaa::InstallDeviceHooks(test.device, kCallbacks, error, sizeof(error)) &&
	          std::strstr(error, "already installed") != nullptr,
	      "second install refused");

	// BeginScene: the hook sees the device, and only on success.
	Check(SUCCEEDED(test.device->BeginScene()), "hooked BeginScene succeeds");
	Check(g_beginScenes == 1 && g_beginSceneDevice == test.device, "BeginScene reached the hook with the device");
	Check(SUCCEEDED(test.device->EndScene()), "EndScene after hooked BeginScene");
	test.device->BeginScene();
	Check(FAILED(test.device->BeginScene()), "nested BeginScene fails as D3D specifies");
	Check(g_beginScenes == 2, "failed BeginScene not reported");
	test.device->EndScene();

	// SetRenderState: state and value, the original ran, the direct call bypasses.
	Check(SUCCEEDED(test.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE)), "hooked SetRenderState succeeds");
	Check(g_renderStates == 1 && g_lastState == D3DRS_ALPHATESTENABLE && g_lastValue == TRUE,
	      "SetRenderState reached the hook with state and value");
	DWORD readBack = 0;
	test.device->GetRenderState(D3DRS_ALPHATESTENABLE, &readBack);
	Check(readBack == TRUE, "the original SetRenderState ran");
	Check(foliageaa::SetRenderStateDirect(test.device, D3DRS_ALPHAREF, 0x40) >= 0, "direct render state call succeeds");
	Check(g_renderStates == 1, "direct render state call bypasses the hook");
	test.device->GetRenderState(D3DRS_ALPHAREF, &readBack);
	Check(readBack == 0x40, "direct render state call reached the device");

	// The pixel shader slots.
	IDirect3DPixelShader9* leaf = nullptr;
	Check(SUCCEEDED(test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(foliageaa::kLeaf2000), &leaf)) &&
	          leaf != nullptr,
	      "vanilla STLEAF2000 bytes make a shader on this device");
	Check(g_psCreated == 1 && g_psCreatedShader == leaf, "CreatePixelShader reached the hook with the new object");
	IDirect3DPixelShader9* other = nullptr;
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(foliageaa::kLeaf2001), &other);
	Check(g_psCreated == 2 && g_psCreatedShader == other, "second creation reported");
	const DWORD garbage[2] = {0x12345678, 0};
	IDirect3DPixelShader9* none = nullptr;
	Check(FAILED(test.device->CreatePixelShader(garbage, &none)), "garbage bytes are refused by the runtime");
	Check(g_psCreated == 2, "failed creation not reported");
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

	// The vertex shader slots.
	IDirect3DVertexShader9* leafVs = nullptr;
	Check(SUCCEEDED(test.device->CreateVertexShader(reinterpret_cast<const DWORD*>(foliageaa::kLeafVs000_0C737235), &leafVs)) &&
	          leafVs != nullptr,
	      "vanilla STLEAF000 bytes make a vertex shader on this device");
	Check(g_vsCreated == 1 && g_vsCreatedShader == leafVs, "CreateVertexShader reached the hook with the new object");
	Check(SUCCEEDED(test.device->SetVertexShader(leafVs)), "hooked SetVertexShader succeeds");
	Check(g_vsSets == 1 && g_vsSet == leafVs, "SetVertexShader reached the hook");
	IDirect3DVertexShader9* currentVs = nullptr;
	test.device->GetVertexShader(&currentVs);
	Check(currentVs == leafVs, "the original SetVertexShader ran");
	if (currentVs != nullptr) currentVs->Release();
	test.device->SetVertexShader(nullptr);
	Check(g_vsSets == 2 && g_vsSet == nullptr, "null reported too");

	// Vertex constants: arguments seen, the original ran, the direct call bypasses.
	const float rows[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	Check(SUCCEEDED(test.device->SetVertexShaderConstantF(2, rows, 2)), "hooked SetVertexShaderConstantF succeeds");
	Check(g_constants == 1 && g_constantStart == 2 && g_constantCount == 2 && g_constantFirst == 1.0f,
	      "SetVertexShaderConstantF reached the hook with its arguments");
	float back[8] = {};
	test.device->GetVertexShaderConstantF(2, back, 2);
	Check(std::memcmp(back, rows, sizeof(rows)) == 0, "the original SetVertexShaderConstantF ran");
	const float direct[4] = {9, 9, 9, 9};
	Check(foliageaa::SetVertexShaderConstantFDirect(test.device, 0, direct, 1) >= 0, "direct constant call succeeds");
	Check(g_constants == 1, "direct constant call bypasses the hook");
	test.device->GetVertexShaderConstantF(0, back, 1);
	Check(back[0] == 9.0f, "direct constant call reached the device");

	// Viewport.
	D3DVIEWPORT9 viewport{0, 0, 32, 16, 0.0f, 1.0f};
	Check(SUCCEEDED(test.device->SetViewport(&viewport)), "hooked SetViewport succeeds");
	Check(g_viewports == 1 && g_viewportWidth == 32, "SetViewport reached the hook");
	D3DVIEWPORT9 viewportBack{};
	test.device->GetViewport(&viewportBack);
	Check(viewportBack.Width == 32 && viewportBack.Height == 16, "the original SetViewport ran");

	// Render target: the back buffer set as target 0, then a bad index refused and not reported.
	IDirect3DSurface9* backBuffer = nullptr;
	test.device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
	Check(backBuffer != nullptr && SUCCEEDED(test.device->SetRenderTarget(0, backBuffer)), "hooked SetRenderTarget succeeds");
	Check(g_targets == 1 && g_targetIndex == 0 && g_targetSurface == backBuffer, "SetRenderTarget reached the hook");
	Check(FAILED(test.device->SetRenderTarget(0, nullptr)), "clearing target 0 is refused by D3D");
	Check(g_targets == 1, "a failed SetRenderTarget is not reported");
	if (backBuffer != nullptr) backBuffer->Release();

	// Draws: the filter is called instead, issue() runs the original (which
	// fails here, nothing being bound to draw), and the filter's answer is
	// what the engine gets.
	g_issuesPerDraw = 1;
	const HRESULT plain = test.device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
	Check(g_draws == 1 && plain == g_lastIssueResult, "DrawPrimitive reached the filter, issue ran the original");
	g_issuesPerDraw = 2;
	g_drawAnswer = 0x1234;
	Check(test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1) == 0x1234,
	      "DrawIndexedPrimitive hands back the filter's answer");
	Check(g_draws == 2, "DrawIndexedPrimitive reached the filter");
	g_drawAnswer = 0;
	g_issuesPerDraw = 1;

	// Removal restores every entry; nothing reaches the hooks afterwards.
	foliageaa::RemoveDeviceHooks();
	Check(!foliageaa::AreDeviceHooksInstalled(), "reports removed");
	Check(std::memcmp(originals, vtable, sizeof(originals)) == 0, "table restored exactly");
	test.device->BeginScene();
	test.device->EndScene();
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	test.device->SetPixelShader(leaf);
	test.device->SetPixelShader(nullptr);
	test.device->SetVertexShader(leafVs);
	test.device->SetVertexShader(nullptr);
	test.device->SetVertexShaderConstantF(0, rows, 1);
	test.device->SetViewport(&viewport);
	test.device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
	IDirect3DPixelShader9* afterRemoval = nullptr;
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(foliageaa::kLeaf2000), &afterRemoval);
	Check(g_beginScenes == 2 && g_renderStates == 1 && g_filters == 3 && g_psCreated == 2 && g_vsSets == 2 &&
	          g_vsCreated == 1 && g_constants == 1 && g_viewports == 1 && g_targets == 1 && g_draws == 2,
	      "nothing reaches the hooks after removal");
	foliageaa::RemoveDeviceHooks();
	Check(!foliageaa::AreDeviceHooksInstalled(), "second removal is harmless");

	// The direct calls without hooks go through the table itself.
	Check(foliageaa::SetRenderStateDirect(test.device, D3DRS_ALPHAREF, 0x41) >= 0, "direct render state call without hooks");
	test.device->GetRenderState(D3DRS_ALPHAREF, &readBack);
	Check(readBack == 0x41, "it reached the device");
	Check(foliageaa::SetVertexShaderConstantFDirect(test.device, 0, direct, 1) >= 0, "direct constant call without hooks");

	if (afterRemoval != nullptr) afterRemoval->Release();
	leaf->Release();
	other->Release();
	leafVs->Release();
	test.Destroy();
}

}  // namespace

int main() {
	TestVtableGuard();
	TestRealDevice();
	std::printf(g_failures == 0 ? "DeviceHookTest: all passed\n" : "DeviceHookTest: %d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
