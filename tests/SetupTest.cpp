// SetupDevice against a real Direct3D 9 device of this machine: the vendor
// and the 'ATOC' probe are what the adapter reports, the chosen back door is
// what Coverage decides from them, the hooks apply it from the next
// BeginScene on, the leaf draws are issued in supersampling passes with the
// right mask and jitter each (seen through a recorder the test puts in the
// draw slots before the plugin's hooks), the coverage method substitutes
// sharpened copies the runtime accepts, and every refusal path logs and
// leaves the device alone.
//
// Prints what this machine's driver answered for 'ATOC', which is the one
// fact about native drivers the plugin's design could not verify from
// sources.

#include "Coverage.h"
#include "DeviceHook.h"
#include "LeafShaders.h"
#include "Setup.h"
#include "ShaderPatch.h"
#include "Supersample.h"
#include "TestDevice.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what);
	}
}

std::vector<std::string> g_lines;

void Log(const char* format, ...) {
	char buffer[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	g_lines.push_back(buffer);
	std::printf("  log: %s\n", buffer);
}

bool Logged(const char* text) {
	for (const auto& line : g_lines) {
		if (line.find(text) != std::string::npos) {
			return true;
		}
	}
	return false;
}

int Count(const char* text) {
	int count = 0;
	for (const auto& line : g_lines) {
		if (line.find(text) != std::string::npos) {
			++count;
		}
	}
	return count;
}

using namespace foliageaa;

Options Opts(Mode mode, LeafMethod method, int passes, bool coverage) {
	Options options;
	options.mode = mode;
	options.leafMethod = method;
	options.passes = passes;
	options.coverage = coverage;
	return options;
}

std::vector<uint8_t> FunctionOf(IDirect3DPixelShader9* shader) {
	UINT size = 0;
	shader->GetFunction(nullptr, &size);
	std::vector<uint8_t> bytes(size);
	shader->GetFunction(bytes.data(), &size);
	return bytes;
}

bool Near(float a, float b) {
	return std::fabs(a - b) < 1e-6f;
}

// The recorder in the draw slots: put there before the plugin's hooks, so
// the plugin chains into it as "the original". It draws nothing and notes
// the state of every call.
struct DrawRecord {
	DWORD mask;
	DWORD adaptiveTessY;
	float c0[4];
	float c1[4];
};
std::vector<DrawRecord> g_records;

void Record(void* self) {
	auto* device = static_cast<IDirect3DDevice9*>(self);
	DrawRecord record{};
	device->GetRenderState(D3DRS_MULTISAMPLEMASK, &record.mask);
	device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &record.adaptiveTessY);
	device->GetVertexShaderConstantF(0, record.c0, 1);
	device->GetVertexShaderConstantF(1, record.c1, 1);
	g_records.push_back(record);
}
HRESULT __stdcall RecordDraw(void* self, uint32_t, uint32_t, uint32_t) {
	Record(self);
	return D3D_OK;
}
HRESULT __stdcall RecordIndexedDraw(void* self, uint32_t, int, uint32_t, uint32_t, uint32_t, uint32_t) {
	Record(self);
	return D3D_OK;
}

bool g_keyIsDown = false;
int g_keyReads = 0;
bool FakeKey(int virtualKey) {
	++g_keyReads;
	return virtualKey == 0x7A && g_keyIsDown;
}

void WriteSlot(void** vtable, uint32_t slot, void* value) {
	DWORD previous = 0;
	VirtualProtect(vtable + slot, sizeof(void*), PAGE_READWRITE, &previous);
	vtable[slot] = value;
	DWORD ignored = 0;
	VirtualProtect(vtable + slot, sizeof(void*), previous, &ignored);
}

void TestRefusals() {
	g_lines.clear();
	SetupResult result = SetupDevice(nullptr, Options(), &Log);
	Check(!result.deviceAccepted && !result.hooksInstalled && Logged("does not carry a method table"), "null refused");

	// An object with a plausible table that is not a Direct3D device:
	// QueryInterface answers E_NOINTERFACE and nothing else is touched.
	struct NotADevice {
		static HRESULT __stdcall QueryInterface(void*, const void*, void** out) {
			*out = nullptr;
			return 0x80004002L;  // E_NOINTERFACE
		}
		static ULONG __stdcall AddRef(void*) { return 1; }
		static ULONG __stdcall Release(void*) { return 1; }
	};
	void* table[kSlotLast + 1];
	for (auto& entry : table) {
		entry = reinterpret_cast<void*>(&NotADevice::Release);
	}
	table[0] = reinterpret_cast<void*>(&NotADevice::QueryInterface);
	table[1] = reinterpret_cast<void*>(&NotADevice::AddRef);
	table[2] = reinterpret_cast<void*>(&NotADevice::Release);
	void** object = table;
	g_lines.clear();
	result = SetupDevice(&object, Options(), &Log);
	Check(!result.deviceAccepted && !result.hooksInstalled && Logged("is not an IDirect3DDevice9"), "non-device refused");
	Check(!AreDeviceHooksInstalled(), "no hooks after refusals");
}

void TestCoverage(TestDevice& test) {
	const Vendor vendor = VendorFromId(test.identifier.VendorId);

	// Nothing asked for: everything read, nothing installed.
	g_lines.clear();
	SetupResult result = SetupDevice(test.device, Opts(Mode::Auto, LeafMethod::Supersample, 1, false), &Log);
	Check(result.deviceAccepted, "device accepted");
	Check(result.vendorId == test.identifier.VendorId && result.vendor == vendor, "vendor read from the adapter");
	Check(result.multiSampleType == static_cast<uint32_t>(test.multisample), "sample count read from render target 0");
	Check(Logged("Device behavior flags 0x"), "behavior flags logged");
	Check(result.hack == Hack::None && !result.hooksInstalled && !result.supersampling && Logged("Coverage: off in the INI") &&
	          Logged("Nothing to do"),
	      "nothing asked: nothing installed");
	Check(!AreDeviceHooksInstalled(), "no hooks");
	const bool atocSupported = result.atocFormatSupported;
	std::printf("this machine: %s, vendor 0x%04X, CheckDeviceFormat('ATOC') %s, multisample %d, behavior flags 0x%08X\n",
	            test.identifier.Description, test.identifier.VendorId, atocSupported ? "supported" : "NOT supported",
	            static_cast<int>(test.multisample), result.behaviorFlags);
	if (test.multisample == D3DMULTISAMPLE_NONE) {
		Check(Logged("NOT multisampled"), "no antialiasing is said");
	} else {
		Check(Logged("multisample type"), "sample count is said");
	}

	// Coverage on with Mode=off: the reason logged, nothing installed.
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Off, LeafMethod::Coverage, 8, true), &Log);
	Check(result.hack == Hack::None && !result.hooksInstalled && Logged("Mode=off in the INI"), "Mode=off: nothing installed");

	// Mode=auto: whatever Coverage decides from the real answers.
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Auto, LeafMethod::Coverage, 8, true), &Log);
	const Hack expected = ChooseHack(Mode::Auto, vendor, atocSupported);
	Check(result.hack == expected, "auto picks what Coverage decides");
	Check(result.hooksInstalled == (expected != Hack::None), "hooks follow the decision");
	TeardownDevice();
	Check(!AreDeviceHooksInstalled(), "teardown removes the hooks");

	// Forced NVIDIA route: ATOC is written after each BeginScene and reads
	// back; a cleared state is restored by the next BeginScene.
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Nvidia, LeafMethod::Coverage, 8, true), &Log);
	Check(result.hack == Hack::NvidiaAtoc && result.hooksInstalled && Logged("enabled on"), "nvidia: installed");
	DWORD readBack = 0;
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(readBack != kFourCCAtoc, "nvidia: nothing written before the first frame");
	test.device->BeginScene();
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(readBack == kFourCCAtoc, "nvidia: ATOC set by BeginScene");
	Check(Logged("First frame seen"), "nvidia: first frame logged");
	test.device->EndScene();
	test.device->SetRenderState(D3DRS_ADAPTIVETESS_Y, 0);
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	test.device->GetRenderState(D3DRS_POINTSIZE, &readBack);
	Check(readBack != kFourCCA2M1, "nvidia: alpha test does not touch POINTSIZE");
	test.device->BeginScene();
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(readBack == kFourCCAtoc, "nvidia: ATOC set again next frame");
	test.device->EndScene();
	TeardownDevice();

	// Forced AMD route: A2M1/A2M0 follow the engine's alpha test, BeginScene
	// writes nothing.
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Amd, LeafMethod::Coverage, 8, true), &Log);
	Check(result.hack == Hack::AmdA2M && result.hooksInstalled, "amd: installed");
	test.device->SetRenderState(D3DRS_POINTSIZE, 0x3F800000);  // 1.0f, a plain point size
	test.device->BeginScene();
	test.device->GetRenderState(D3DRS_POINTSIZE, &readBack);
	Check(readBack == 0x3F800000, "amd: BeginScene leaves POINTSIZE alone");
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	test.device->GetRenderState(D3DRS_POINTSIZE, &readBack);
	Check(readBack == kFourCCA2M1, "amd: alpha test on -> A2M1");
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	test.device->GetRenderState(D3DRS_POINTSIZE, &readBack);
	Check(readBack == kFourCCA2M0, "amd: alpha test off -> A2M0");
	test.device->SetRenderState(D3DRS_ALPHAREF, 0x80);
	test.device->GetRenderState(D3DRS_POINTSIZE, &readBack);
	Check(readBack == kFourCCA2M0, "amd: other states leave POINTSIZE alone");
	test.device->EndScene();
	TeardownDevice();

	// Installing twice without teardown: the second setup refuses and says so.
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Nvidia, LeafMethod::Coverage, 8, true), &Log);
	Check(result.hooksInstalled, "first of two installs");
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Nvidia, LeafMethod::Coverage, 8, true), &Log);
	Check(!result.hooksInstalled && Logged("already installed"), "second install refused and logged");
	TeardownDevice();
}

void TestSharpening(TestDevice& test) {
	// Shaders the engine would have created long before the plugin's setup.
	IDirect3DPixelShader9* leaf2000 = nullptr;
	IDirect3DPixelShader9* leaf2001 = nullptr;
	IDirect3DPixelShader9* otherShader = nullptr;
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(kLeaf2000), &leaf2000);
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(kLeaf2001), &leaf2001);
	// A ps_2_x that is not a leaf shader: the sharpened STLEAF2000 itself.
	std::vector<uint8_t> sharpened;
	BuildSharpenedShader(kLeaf2000, sizeof(kLeaf2000), &sharpened);
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(sharpened.data()), &otherShader);
	Check(leaf2000 != nullptr && leaf2001 != nullptr && otherShader != nullptr,
	      "the runtime accepts the vanilla leaf shaders and the sharpened copy");
	if (leaf2000 == nullptr || leaf2001 == nullptr || otherShader == nullptr) {
		return;
	}
	float c31[4] = {};
	const float poison[4] = {-7, -7, -7, -7};

	// Coverage method, threshold following the engine: setting a leaf shader
	// sets a different object whose bytes are the sharpened copy, and c31
	// carries the steepness and the engine's alpha reference as shadowed
	// from its own write.
	g_lines.clear();
	Options options = Opts(Mode::Nvidia, LeafMethod::Coverage, 8, true);
	options.steepness = 8.0f;
	SetupResult result = SetupDevice(test.device, options, &Log);
	Check(result.hooksInstalled && result.sharpening && !result.supersampling &&
	          Logged("Leaves: coverage with sharpening") && Logged("following the engine's D3DRS_ALPHAREF"),
	      "sharpening announced with the engine's threshold");
	test.device->SetRenderState(D3DRS_ALPHAREF, 96);
	test.device->SetPixelShaderConstantF(31, poison, 1);
	test.device->SetPixelShader(leaf2000);
	IDirect3DPixelShader9* current = nullptr;
	test.device->GetPixelShader(&current);
	Check(current != nullptr && current != leaf2000, "a different object was set for STLEAF2000");
	Check(current != nullptr && FunctionOf(current) == sharpened, "its bytes are the sharpened copy");
	test.device->GetPixelShaderConstantF(31, c31, 1);
	Check(c31[0] == 8.0f && c31[1] == SharpenOffset(ThresholdFromAlphaRef(96), 8.0f) && c31[2] == 0 && c31[3] == 0,
	      "c31 written from the engine's alpha ref 96");
	Check(Logged("STLEAF2000 set by the engine: sharpened copy"), "substitution logged");
	Check(Logged("D3DRS_ALPHAREF=96 (0.376 as a threshold)"), "engine alpha ref logged");
	IDirect3DPixelShader9* first = current;
	if (current != nullptr) current->Release();

	// The engine moves its reference while the copy is bound: c31 follows.
	test.device->SetRenderState(D3DRS_ALPHAREF, 160);
	test.device->GetPixelShaderConstantF(31, c31, 1);
	Check(c31[1] == SharpenOffset(ThresholdFromAlphaRef(160), 8.0f), "c31 follows an alpha ref change");
	// Another render state does not touch it.
	test.device->SetPixelShaderConstantF(31, poison, 1);
	test.device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
	test.device->GetPixelShaderConstantF(31, c31, 1);
	Check(c31[0] == -7, "other render states leave c31 alone");

	test.device->SetPixelShader(leaf2000);
	test.device->GetPixelShader(&current);
	Check(current == first, "second set reuses the same copy");
	if (current != nullptr) current->Release();
	Check(Count("sharpened copy") == 1, "no second creation logged");
	Check(Count("D3DRS_ALPHAREF=") == 2, "each distinct alpha ref logged once (96, 160)");
	test.device->SetPixelShader(leaf2000);
	Check(Count("D3DRS_ALPHAREF=") == 2, "a repeated alpha ref is not logged again");

	test.device->SetPixelShader(leaf2001);
	test.device->GetPixelShader(&current);
	Check(current != nullptr && current != leaf2001 && current != first, "STLEAF2001 gets its own copy");
	std::vector<uint8_t> expected;
	BuildSharpenedShader(kLeaf2001, sizeof(kLeaf2001), &expected);
	Check(current != nullptr && FunctionOf(current) == expected, "STLEAF2001's bytes are its sharpened copy");
	if (current != nullptr) current->Release();
	Check(Logged("STLEAF2001 set by the engine"), "second leaf shader logged");

	// A non-leaf shader passes through, and while it is bound the engine's
	// alpha ref changes must not write c31 - it may be that shader's own.
	test.device->SetPixelShader(otherShader);
	test.device->GetPixelShader(&current);
	Check(current == otherShader, "a non-leaf shader passes through");
	if (current != nullptr) current->Release();
	test.device->SetPixelShaderConstantF(31, poison, 1);
	test.device->SetRenderState(D3DRS_ALPHAREF, 200);
	test.device->GetPixelShaderConstantF(31, c31, 1);
	Check(c31[0] == -7, "alpha ref changes do not touch c31 under a foreign shader");
	test.device->SetPixelShader(nullptr);
	test.device->GetPixelShader(&current);
	Check(current == nullptr, "null passes through");
	test.device->SetRenderState(D3DRS_ALPHAREF, 201);
	test.device->GetPixelShaderConstantF(31, c31, 1);
	Check(c31[0] == -7, "alpha ref changes do not touch c31 with no shader");

	// A shader created at the address of a forgotten one is looked at afresh:
	// release the vanilla STLEAF2000 and create a non-leaf shader; whatever
	// address it lands on, setting it must set it, not a stale leaf copy.
	leaf2000->Release();
	IDirect3DPixelShader9* recreated = nullptr;
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(sharpened.data()), &recreated);
	Check(recreated != nullptr, "re-creation succeeds");
	if (recreated != nullptr) {
		test.device->SetPixelShader(recreated);
		test.device->GetPixelShader(&current);
		Check(current == recreated, "a new object at any address is set as itself");
		if (current != nullptr) current->Release();
		test.device->SetPixelShader(nullptr);
		recreated->Release();
	}
	TeardownDevice();
	Check(!AreDeviceHooksInstalled(), "teardown after sharpening");

	// A fixed threshold: c31 carries it, and alpha ref changes are ignored.
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(kLeaf2000), &leaf2000);
	g_lines.clear();
	options = Opts(Mode::Nvidia, LeafMethod::Coverage, 8, true);
	options.threshold = 0.4f;
	options.steepness = 4.0f;
	result = SetupDevice(test.device, options, &Log);
	Check(result.sharpening && Logged("sat((a - 0.40) * 4.0 + 0.5)"), "fixed threshold announced");
	test.device->SetRenderState(D3DRS_ALPHAREF, 96);
	test.device->SetPixelShader(leaf2000);
	test.device->GetPixelShaderConstantF(31, c31, 1);
	Check(c31[0] == 4.0f && c31[1] == SharpenOffset(0.4f, 4.0f), "c31 carries the fixed threshold");
	test.device->SetRenderState(D3DRS_ALPHAREF, 160);
	test.device->GetPixelShaderConstantF(31, c31, 1);
	Check(c31[1] == SharpenOffset(0.4f, 4.0f), "fixed threshold ignores alpha ref changes");
	Check(Count("D3DRS_ALPHAREF=") == 0, "fixed threshold logs no alpha refs");
	test.device->SetPixelShader(nullptr);
	TeardownDevice();

	// Sharpening off: leaf shaders pass through, and the log says so.
	g_lines.clear();
	options = Opts(Mode::Nvidia, LeafMethod::Coverage, 8, true);
	options.sharpenLeaves = false;
	result = SetupDevice(test.device, options, &Log);
	Check(result.hooksInstalled && !result.sharpening && Logged("Leaves: coverage without sharpening"), "off announced");
	test.device->SetPixelShader(leaf2000);
	test.device->GetPixelShader(&current);
	Check(current == leaf2000, "sharpening off: the vanilla shader is set");
	if (current != nullptr) current->Release();
	test.device->SetPixelShader(nullptr);
	TeardownDevice();

	// The coverage method without coverage: nothing to install.
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Auto, LeafMethod::Coverage, 8, false), &Log);
	Check(!result.hooksInstalled && !result.sharpening && Logged("Nothing to do"), "coverage method without coverage: nothing");

	leaf2000->Release();
	leaf2001->Release();
	otherShader->Release();
}

void TestSupersampling(TestDevice& test) {
	if (test.multisample != D3DMULTISAMPLE_8_SAMPLES) {
		Check(false, "this device gave no 8-sample back buffer; the supersampling test needs one");
		return;
	}
	IDirect3DPixelShader9* leafPs = nullptr;
	IDirect3DVertexShader9* leafVs = nullptr;
	IDirect3DVertexShader9* unknownVs = nullptr;
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(kLeaf2000), &leafPs);
	test.device->CreateVertexShader(reinterpret_cast<const DWORD*>(kLeafVs000_0C737235), &leafVs);
	// A vertex shader the plugin does not know: the same build with one byte
	// changed inside its constant-table comment, which the runtime skips.
	std::vector<uint8_t> altered(kLeafVs000_0C737235, kLeafVs000_0C737235 + sizeof(kLeafVs000_0C737235));
	altered[40] ^= 0x01;
	test.device->CreateVertexShader(reinterpret_cast<const DWORD*>(altered.data()), &unknownVs);
	Check(leafPs != nullptr && leafVs != nullptr && unknownVs != nullptr,
	      "the runtime accepts the leaf vertex shader and its altered twin");
	if (leafPs == nullptr || leafVs == nullptr || unknownVs == nullptr) {
		return;
	}

	// The recorder goes into the draw slots first; the plugin chains to it.
	void** vtable = *reinterpret_cast<void***>(test.device);
	void* const originalDraw = vtable[kSlotDrawPrimitive];
	void* const originalIndexedDraw = vtable[kSlotDrawIndexedPrimitive];
	WriteSlot(vtable, kSlotDrawPrimitive, reinterpret_cast<void*>(&RecordDraw));
	WriteSlot(vtable, kSlotDrawIndexedPrimitive, reinterpret_cast<void*>(&RecordIndexedDraw));

	// Coverage on (forced NVIDIA) and supersampling: the passes must run
	// with coverage off and put it back.
	g_lines.clear();
	SetupResult result = SetupDevice(test.device, Opts(Mode::Nvidia, LeafMethod::Supersample, 8, true), &Log);
	Check(result.hooksInstalled && result.supersampling && !result.sharpening && Logged("Leaves: supersampled") &&
	          Logged("up to 8 passes"),
	      "supersampling announced");
	// The engine's state, set through the hooked slots.
	D3DVIEWPORT9 viewport{0, 0, 64, 64, 0.0f, 1.0f};
	test.device->SetViewport(&viewport);
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	test.device->SetRenderState(D3DRS_ALPHAREF, 84);
	test.device->SetRenderState(D3DRS_MULTISAMPLEMASK, 0xFFFFFFFF);
	const float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	test.device->SetVertexShaderConstantF(0, rows, 4);
	test.device->SetVertexShader(leafVs);
	test.device->SetPixelShader(leafPs);
	test.device->BeginScene();  // the plugin writes ATOC here
	DWORD readBack = 0;
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(readBack == kFourCCAtoc, "ATOC on before the leaf draw");

	g_records.clear();
	Check(test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1) == D3D_OK, "the leaf draw returns the recorder's OK");
	Check(g_records.size() == 8, "eight passes recorded");
	SupersamplePass planned[kMaxPasses];
	PlanPasses(8, 8, planned);
	for (size_t i = 0; i < g_records.size() && i < 8; ++i) {
		float expected[8];
		JitterClipRows(rows, planned[i].offsetX, planned[i].offsetY, 64, 64, expected);
		Check(g_records[i].mask == planned[i].mask, "pass writes its own sample");
		Check(g_records[i].adaptiveTessY == 0, "coverage off during the pass");
		Check(std::memcmp(g_records[i].c0, expected, 4 * sizeof(float)) == 0 &&
		          std::memcmp(g_records[i].c1, expected + 4, 4 * sizeof(float)) == 0,
		      "pass evaluated at its sample's place (c0/c1 jittered)");
	}
	Check(g_records.size() == 8 && g_records[0].mask == 0x01 && g_records[7].mask == 0x80, "masks 1 to 128 in order");
	test.device->GetRenderState(D3DRS_MULTISAMPLEMASK, &readBack);
	Check(readBack == 0xFFFFFFFF, "mask restored after the passes");
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(readBack == kFourCCAtoc, "coverage back after the passes");
	float back[8] = {};
	test.device->GetVertexShaderConstantF(0, back, 2);
	Check(std::memcmp(back, rows, 8 * sizeof(float)) == 0, "c0/c1 restored after the passes");
	Check(Logged("First leaf draw supersampled: 8 passes on a 8-sample target, viewport 64x64, STLEAF2000 with STLEAF000"),
	      "first supersampled draw logged");

	// The other draw entry point takes the same passes.
	g_records.clear();
	Check(test.device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1) == D3D_OK, "DrawPrimitive path");
	Check(g_records.size() == 8 && g_records[3].mask == 0x08, "eight passes through DrawPrimitive");
	Check(Count("First leaf draw supersampled") == 1, "announced once");

	// The gates, each letting the draw through once, untouched.
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_records.clear();
	test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
	Check(g_records.size() == 1 && g_records[0].mask == 0xFFFFFFFF && g_records[0].adaptiveTessY == kFourCCAtoc,
	      "alpha test off: one plain draw, coverage untouched");
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

	test.device->SetVertexShader(unknownVs);
	g_records.clear();
	test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
	Check(g_records.size() == 1, "unknown vertex shader: one plain draw");
	Check(Logged("Leaf draw not supersampled: the leaf pixel shader is bound with a vertex shader the plugin does not know"),
	      "unknown vertex shader logged");
	test.device->SetVertexShader(leafVs);

	test.device->SetPixelShader(nullptr);
	g_records.clear();
	test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
	Check(g_records.size() == 1, "no pixel shader: one plain draw");
	test.device->SetPixelShader(leafPs);

	IDirect3DSurface9* single = nullptr;
	IDirect3DSurface9* backBuffer = nullptr;
	test.device->CreateRenderTarget(64, 64, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &single, nullptr);
	test.device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
	Check(single != nullptr && backBuffer != nullptr, "a single-sample target and the back buffer");
	if (single != nullptr && backBuffer != nullptr) {
		test.device->SetRenderTarget(0, single);
		g_records.clear();
		test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
		Check(g_records.size() == 1, "single-sample target: one plain draw");
		test.device->SetRenderTarget(0, backBuffer);
		test.device->SetViewport(&viewport);  // SetRenderTarget resets the viewport
		g_records.clear();
		test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
		Check(g_records.size() == 8, "back on the 8-sample target: eight passes");
	}
	test.device->EndScene();
	TeardownDevice();

	// Four passes: pairs of samples, evaluated at the 4-sample positions.
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Nvidia, LeafMethod::Supersample, 4, true), &Log);
	Check(result.supersampling && Logged("up to 4 passes"), "four passes announced");
	test.device->SetVertexShaderConstantF(0, rows, 4);
	test.device->SetVertexShader(leafVs);
	test.device->SetPixelShader(leafPs);
	test.device->BeginScene();
	g_records.clear();
	test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
	Check(g_records.size() == 4, "four passes recorded");
	PlanPasses(8, 4, planned);
	for (size_t i = 0; i < g_records.size() && i < 4; ++i) {
		float expected[8];
		JitterClipRows(rows, planned[i].offsetX, planned[i].offsetY, 64, 64, expected);
		Check(g_records[i].mask == planned[i].mask && std::memcmp(g_records[i].c0, expected, 4 * sizeof(float)) == 0,
		      "pair pass at its 4-sample position");
	}
	Check(g_records.size() == 4 && g_records[0].mask == 0x03 && g_records[3].mask == 0xC0, "pair masks");
	test.device->EndScene();
	TeardownDevice();

	// No coverage at all, supersampling alone: the passes run and the
	// coverage state is never touched.
	test.device->SetRenderState(D3DRS_ADAPTIVETESS_Y, 0);
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Auto, LeafMethod::Supersample, 8, false), &Log);
	Check(result.hooksInstalled && result.supersampling && result.hack == Hack::None && Logged("Coverage: off in the INI"),
	      "supersampling without coverage installed");
	test.device->SetVertexShaderConstantF(0, rows, 4);
	test.device->SetVertexShader(leafVs);
	test.device->SetPixelShader(leafPs);
	test.device->BeginScene();
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(readBack == 0, "no ATOC written at BeginScene");
	g_records.clear();
	test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
	Check(g_records.size() == 8 && g_records[0].adaptiveTessY == 0 && g_records[7].adaptiveTessY == 0,
	      "eight passes, coverage state untouched");
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(readBack == 0, "still no ATOC after the passes");
	test.device->EndScene();
	TeardownDevice();

	// Passes=1 under the supersample method: leaves drawn plainly, and with
	// no coverage there is nothing to install.
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Auto, LeafMethod::Supersample, 1, false), &Log);
	Check(!result.hooksInstalled && !result.supersampling && Logged("Nothing to do"), "one pass, no coverage: nothing to do");
	g_lines.clear();
	result = SetupDevice(test.device, Opts(Mode::Nvidia, LeafMethod::Supersample, 1, true), &Log);
	Check(result.hooksInstalled && !result.supersampling && Logged("Passes=1 asks for no supersampling"),
	      "one pass with coverage: coverage alone");
	test.device->SetPixelShader(leafPs);
	test.device->SetVertexShader(leafVs);
	g_records.clear();
	test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
	Check(g_records.size() == 1, "one pass: one draw");
	test.device->SetPixelShader(nullptr);
	test.device->SetVertexShader(nullptr);
	TeardownDevice();

	// The toggle key: off draws the leaves once and takes coverage away,
	// on brings both back; a held key flips once; no key is never read.
	g_keyIsDown = false;
	g_keyReads = 0;
	SetKeyReaderForTest(&FakeKey);
	Options toggled = Opts(Mode::Nvidia, LeafMethod::Supersample, 8, true);
	toggled.toggleKey = 0x7A;
	g_lines.clear();
	result = SetupDevice(test.device, toggled, &Log);
	Check(result.supersampling && Logged("Toggle key 0x7A switches FoliageAA off and on"), "toggle key announced");
	test.device->SetVertexShaderConstantF(0, rows, 4);
	test.device->SetVertexShader(leafVs);
	test.device->SetPixelShader(leafPs);
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	test.device->BeginScene();
	g_records.clear();
	test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
	Check(g_records.size() == 8 && g_keyReads == 1, "on: eight passes, the key read at BeginScene");
	test.device->EndScene();
	g_keyIsDown = true;
	test.device->BeginScene();
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(Logged("Toggle key: FoliageAA OFF") && readBack == 0, "pressed: off, coverage taken away");
	g_records.clear();
	test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
	Check(g_records.size() == 1, "off: one plain draw");
	test.device->EndScene();
	test.device->BeginScene();  // still held
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(Count("Toggle key: FoliageAA") == 1 && readBack == 0, "held: no second flip, no ATOC written");
	test.device->EndScene();
	g_keyIsDown = false;
	test.device->BeginScene();
	test.device->EndScene();
	g_keyIsDown = true;
	test.device->BeginScene();
	test.device->GetRenderState(D3DRS_ADAPTIVETESS_Y, &readBack);
	Check(Logged("Toggle key: FoliageAA ON") && readBack == kFourCCAtoc, "pressed again: on, coverage back");
	g_records.clear();
	test.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
	Check(g_records.size() == 8, "on again: eight passes");
	test.device->EndScene();
	test.device->SetPixelShader(nullptr);
	test.device->SetVertexShader(nullptr);
	TeardownDevice();
	g_keyReads = 0;
	result = SetupDevice(test.device, Opts(Mode::Nvidia, LeafMethod::Supersample, 8, true), &Log);
	test.device->BeginScene();
	test.device->EndScene();
	Check(g_keyReads == 0, "no toggle key: the key is never read");
	TeardownDevice();
	SetKeyReaderForTest(nullptr);
	g_keyIsDown = false;

	// The recorder out again.
	WriteSlot(vtable, kSlotDrawPrimitive, originalDraw);
	WriteSlot(vtable, kSlotDrawIndexedPrimitive, originalIndexedDraw);
	if (single != nullptr) single->Release();
	if (backBuffer != nullptr) backBuffer->Release();
	leafPs->Release();
	leafVs->Release();
	unknownVs->Release();
}

void TestDump(TestDevice& test) {
	char temp[MAX_PATH];
	GetTempPathA(MAX_PATH, temp);
	const std::string dir = std::string(temp) + "FoliageAASetupTestDump";
	// Start clean.
	DeleteFileA((dir + "\\ps000_232.pso").c_str());
	DeleteFileA((dir + "\\ps001_296.pso").c_str());
	RemoveDirectoryA(dir.c_str());

	IDirect3DPixelShader9* leaf2000 = nullptr;
	IDirect3DPixelShader9* leaf2001 = nullptr;
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(kLeaf2000), &leaf2000);
	test.device->CreatePixelShader(reinterpret_cast<const DWORD*>(kLeaf2001), &leaf2001);

	g_lines.clear();
	Options options = Opts(Mode::Nvidia, LeafMethod::Supersample, 8, true);
	options.dumpDirectory = dir;
	SetupResult result = SetupDevice(test.device, options, &Log);
	Check(result.hooksInstalled && Logged("Shader dump on"), "dump announced");
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	test.device->SetRenderState(D3DRS_ALPHAREF, 84);
	test.device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	test.device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	test.device->SetPixelShader(leaf2000);
	test.device->SetPixelShader(leaf2000);
	test.device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	test.device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	test.device->SetPixelShader(leaf2001);
	test.device->SetPixelShader(nullptr);
	Check(Logged("Shader dump 000: 232 bytes written, first set with alphaTest=1 alphaRef=84 blend=0") &&
	          Logged("zWrite=1 (STLEAF2000)"),
	      "first shader dumped with its states and name");
	Check(Logged("Shader dump 001: 296 bytes written, first set with alphaTest=0 alphaRef=84 blend=1"),
	      "second shader dumped with its own states");
	Check(Count("Shader dump 0") == 2, "each shader dumped once");
	std::ifstream file(dir + "\\ps000_232.pso", std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	Check(bytes.size() == sizeof(kLeaf2000) && std::memcmp(bytes.data(), kLeaf2000, bytes.size()) == 0,
	      "the file holds the shader's bytes");
	TeardownDevice();

	leaf2000->Release();
	leaf2001->Release();
}

}  // namespace

int main() {
	TestRefusals();
	TestDevice test;
	if (!test.Create()) {
		Check(false, "a Direct3D 9 device could be created");
	} else {
		TestCoverage(test);
		TestSharpening(test);
		TestSupersampling(test);
		TestDump(test);
		test.Destroy();
	}
	std::printf(g_failures == 0 ? "SetupTest: all passed\n" : "SetupTest: %d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
