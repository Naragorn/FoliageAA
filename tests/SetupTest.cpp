// SetupDevice against a real Direct3D 9 device of this machine: the vendor
// and the 'ATOC' probe are what the adapter reports, the chosen back door is
// what Coverage decides from them, the hooks apply it from the next
// BeginScene on, the leaf shaders are substituted by sharpened copies the
// runtime accepts, and every refusal path logs and leaves the device alone.
//
// Prints what this machine's driver answered for 'ATOC', which is the one
// fact about native drivers the plugin's design could not verify from
// sources.

#include "Coverage.h"
#include "DeviceHook.h"
#include "LeafShaders.h"
#include "Setup.h"
#include "ShaderPatch.h"
#include "TestDevice.h"

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

Options WithMode(Mode mode, bool sharpen = true) {
	Options options;
	options.mode = mode;
	options.sharpenLeaves = sharpen;
	return options;
}

std::vector<uint8_t> FunctionOf(IDirect3DPixelShader9* shader) {
	UINT size = 0;
	shader->GetFunction(nullptr, &size);
	std::vector<uint8_t> bytes(size);
	shader->GetFunction(bytes.data(), &size);
	return bytes;
}

void TestRefusals() {
	g_lines.clear();
	SetupResult result = SetupDevice(nullptr, WithMode(Mode::Auto), &Log);
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
	result = SetupDevice(&object, WithMode(Mode::Auto), &Log);
	Check(!result.deviceAccepted && !result.hooksInstalled && Logged("is not an IDirect3DDevice9"), "non-device refused");
	Check(!AreDeviceHooksInstalled(), "no hooks after refusals");
}

void TestCoverage(TestDevice& test) {
	const Vendor vendor = VendorFromId(test.identifier.VendorId);

	// Mode=off: everything read, nothing installed.
	g_lines.clear();
	SetupResult result = SetupDevice(test.device, WithMode(Mode::Off), &Log);
	Check(result.deviceAccepted, "device accepted");
	Check(result.vendorId == test.identifier.VendorId && result.vendor == vendor, "vendor read from the adapter");
	Check(result.multiSampleType == static_cast<uint32_t>(test.multisample), "sample count read from render target 0");
	Check(result.hack == Hack::None && !result.hooksInstalled && !result.sharpening && Logged("Mode=off in the INI"),
	      "off: nothing installed");
	Check(!AreDeviceHooksInstalled(), "off: no hooks");
	const bool atocSupported = result.atocFormatSupported;
	std::printf("this machine: %s, vendor 0x%04X, CheckDeviceFormat('ATOC') %s, multisample %d\n",
	            test.identifier.Description, test.identifier.VendorId, atocSupported ? "supported" : "NOT supported",
	            static_cast<int>(test.multisample));
	if (test.multisample == D3DMULTISAMPLE_NONE) {
		Check(Logged("NOT multisampled"), "no antialiasing is said");
	} else {
		Check(Logged("multisample type"), "sample count is said");
	}

	// Mode=auto: whatever Coverage decides from the real answers.
	g_lines.clear();
	result = SetupDevice(test.device, WithMode(Mode::Auto), &Log);
	const Hack expected = ChooseHack(Mode::Auto, vendor, atocSupported);
	Check(result.hack == expected, "auto picks what Coverage decides");
	Check(result.hooksInstalled == (expected != Hack::None), "hooks follow the decision");
	TeardownDevice();
	Check(!AreDeviceHooksInstalled(), "teardown removes the hooks");

	// Forced NVIDIA route: ATOC is written after each BeginScene and reads
	// back; a cleared state is restored by the next BeginScene.
	g_lines.clear();
	result = SetupDevice(test.device, WithMode(Mode::Nvidia), &Log);
	Check(result.hack == Hack::NvidiaAtoc && result.hooksInstalled && Logged("enabled, applied from the next frame on"),
	      "nvidia: installed");
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
	result = SetupDevice(test.device, WithMode(Mode::Amd), &Log);
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
	result = SetupDevice(test.device, WithMode(Mode::Nvidia), &Log);
	Check(result.hooksInstalled, "first of two installs");
	g_lines.clear();
	result = SetupDevice(test.device, WithMode(Mode::Nvidia), &Log);
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

	// Sharpening on, threshold following the engine: setting a leaf shader
	// sets a different object whose bytes are the sharpened copy, and c31
	// carries the steepness and the engine's alpha reference.
	g_lines.clear();
	Options options = WithMode(Mode::Nvidia);
	options.steepness = 8.0f;
	SetupResult result = SetupDevice(test.device, options, &Log);
	Check(result.hooksInstalled && result.sharpening && Logged("following the engine's D3DRS_ALPHAREF"),
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
	options = WithMode(Mode::Nvidia);
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
	result = SetupDevice(test.device, WithMode(Mode::Nvidia, false), &Log);
	Check(result.hooksInstalled && !result.sharpening && Logged("Leaf sharpening off"), "off announced");
	test.device->SetPixelShader(leaf2000);
	test.device->GetPixelShader(&current);
	Check(current == leaf2000, "sharpening off: the vanilla shader is set");
	if (current != nullptr) current->Release();
	test.device->SetPixelShader(nullptr);
	TeardownDevice();

	// Coverage off (Mode=off) installs nothing, so nothing is substituted either.
	g_lines.clear();
	result = SetupDevice(test.device, WithMode(Mode::Off), &Log);
	Check(!result.hooksInstalled && !result.sharpening, "mode off: no sharpening without coverage");
	test.device->SetPixelShader(leaf2000);
	test.device->GetPixelShader(&current);
	Check(current == leaf2000, "mode off: vanilla shader set");
	if (current != nullptr) current->Release();
	test.device->SetPixelShader(nullptr);

	leaf2000->Release();
	leaf2001->Release();
	otherShader->Release();
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
	Options options = WithMode(Mode::Nvidia, false);
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
		TestDump(test);
		test.Destroy();
	}
	std::printf(g_failures == 0 ? "SetupTest: all passed\n" : "SetupTest: %d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
