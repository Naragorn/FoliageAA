#include "Setup.h"

#include "DeviceHook.h"
#include "ShaderPatch.h"

// initguid.h first: IID_IDirect3DDevice9 is then defined in this file instead
// of needing d3d9.lib, which would pull a second d3d9.dll into the game.
#include <initguid.h>
#include <d3d9.h>
#include <windows.h>

#include <cstdio>
#include <unordered_set>
#include <vector>

namespace foliageaa {

namespace {

Hack g_hack = Hack::None;
LogFn g_log = nullptr;
bool g_announcedLive = false;

Options g_options;
bool g_sharpen = false;
ShaderCache g_cache;
std::vector<IDirect3DPixelShader9*> g_replacements;  // owned, released at teardown
bool g_leafCopyBound = false;                         // the shader last set is one of the copies
std::vector<uint32_t> g_alphaRefsSeen;                // distinct D3DRS_ALPHAREF values at leaf sets, first 16
std::unordered_set<void*> g_dumped;
int g_dumpCount = 0;

void ApplyWrites(void* device, const StateWrite* writes, int count) {
	for (int i = 0; i < count; ++i) {
		SetRenderStateDirect(device, writes[i].state, writes[i].value);
	}
}

void OnBeginScene(void* device) {
	StateWrite writes[2];
	const int count = BeginSceneWrites(g_hack, writes);
	ApplyWrites(device, writes, count);
	if (!g_announcedLive) {
		g_announcedLive = true;
		if (g_log != nullptr) {
			g_log("First frame seen: %s is live", HackName(g_hack));
		}
	}
}

// c31 for the copy that is bound: the engine's alpha reference as the
// threshold unless the INI fixed one.
void WriteSharpenConstants(IDirect3DDevice9* device) {
	float threshold = g_options.threshold;
	if (threshold < 0.0f) {
		DWORD alphaRef = 0;
		device->GetRenderState(D3DRS_ALPHAREF, &alphaRef);
		threshold = ThresholdFromAlphaRef(alphaRef);
	}
	float values[4];
	SharpenConstants(threshold, g_options.steepness, values);
	device->SetPixelShaderConstantF(kSharpenConstantRegister, values, 1);
}

void OnRenderState(void* device, uint32_t state, uint32_t value) {
	if (state == kRsAlphaTestEnable) {
		StateWrite writes[2];
		const int count = AlphaTestWrites(g_hack, value, writes);
		ApplyWrites(device, writes, count);
		return;
	}
	// D3DRS_ALPHAREF = 24: the threshold moved while a copy is bound.
	if (state == 24 && g_leafCopyBound && g_options.threshold < 0.0f) {
		WriteSharpenConstants(static_cast<IDirect3DDevice9*>(device));
	}
}

std::vector<uint8_t> FunctionOf(IDirect3DPixelShader9* shader) {
	UINT size = 0;
	if (FAILED(shader->GetFunction(nullptr, &size)) || size == 0 || size > 65536) {
		return {};
	}
	std::vector<uint8_t> bytes(size);
	if (FAILED(shader->GetFunction(bytes.data(), &size))) {
		return {};
	}
	return bytes;
}

// The first time a shader object is set, its bytes decide whether a
// sharpened copy stands in for it from then on.
void* MakeReplacement(IDirect3DDevice9* device, IDirect3DPixelShader9* shader) {
	const std::vector<uint8_t> bytes = FunctionOf(shader);
	const LeafShader kind = IdentifyLeafShader(bytes.data(), bytes.size());
	if (kind == LeafShader::None) {
		return nullptr;
	}
	std::vector<uint8_t> patched;
	if (!BuildSharpenedShader(bytes.data(), bytes.size(), &patched)) {
		g_log("%s found but could not be patched - left alone", LeafShaderName(kind));
		return nullptr;
	}
	IDirect3DPixelShader9* replacement = nullptr;
	const HRESULT hr = device->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()), &replacement);
	if (FAILED(hr) || replacement == nullptr) {
		g_log("%s found but CreatePixelShader of the sharpened copy failed (0x%08lX) - left alone",
		      LeafShaderName(kind), hr);
		return nullptr;
	}
	g_replacements.push_back(replacement);
	g_log("%s set by the engine: sharpened copy %p created", LeafShaderName(kind), static_cast<void*>(replacement));
	return replacement;
}

void DumpShader(IDirect3DDevice9* device, IDirect3DPixelShader9* shader) {
	if (!g_dumped.insert(shader).second) {
		return;
	}
	const std::vector<uint8_t> bytes = FunctionOf(shader);
	const int index = g_dumpCount++;
	char path[MAX_PATH];
	std::snprintf(path, sizeof(path), "%s\\ps%03d_%u.pso", g_options.dumpDirectory.c_str(), index,
	              static_cast<unsigned>(bytes.size()));
	bool written = false;
	if (FILE* file = std::fopen(path, "wb")) {
		written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
		std::fclose(file);
	}
	DWORD alphaTest = 0, alphaRef = 0, blend = 0, src = 0, dst = 0, zWrite = 0;
	device->GetRenderState(D3DRS_ALPHATESTENABLE, &alphaTest);
	device->GetRenderState(D3DRS_ALPHAREF, &alphaRef);
	device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
	device->GetRenderState(D3DRS_SRCBLEND, &src);
	device->GetRenderState(D3DRS_DESTBLEND, &dst);
	device->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
	g_log("Shader dump %03d: %u bytes %s, first set with alphaTest=%lu alphaRef=%lu blend=%lu src=%lu dst=%lu "
	      "zWrite=%lu (%s)",
	      index, static_cast<unsigned>(bytes.size()), written ? "written" : "NOT written", alphaTest, alphaRef, blend,
	      src, dst, zWrite, LeafShaderName(IdentifyLeafShader(bytes.data(), bytes.size())));
}

void* FilterPixelShader(void* rawDevice, void* rawShader) {
	auto* device = static_cast<IDirect3DDevice9*>(rawDevice);
	auto* shader = static_cast<IDirect3DPixelShader9*>(rawShader);
	if (shader != nullptr && !g_options.dumpDirectory.empty()) {
		DumpShader(device, shader);
	}
	if (!g_sharpen || shader == nullptr) {
		g_leafCopyBound = false;
		return rawShader;
	}
	void* replacement = nullptr;
	if (!g_cache.Lookup(rawShader, &replacement)) {
		replacement = MakeReplacement(device, shader);
		g_cache.Remember(rawShader, replacement);
	}
	if (replacement == nullptr) {
		g_leafCopyBound = false;
		return rawShader;
	}
	g_leafCopyBound = true;
	// The constants belong to the copy about to be bound: SetPixelShader
	// does not touch constant registers, so writing them here, before the
	// original call binds the copy, is in order.
	WriteSharpenConstants(device);
	if (g_options.threshold < 0.0f && g_alphaRefsSeen.size() < 16) {
		DWORD alphaRef = 0;
		device->GetRenderState(D3DRS_ALPHAREF, &alphaRef);
		bool seen = false;
		for (uint32_t known : g_alphaRefsSeen) {
			seen = seen || known == alphaRef;
		}
		if (!seen) {
			g_alphaRefsSeen.push_back(alphaRef);
			g_log("Leaf shader set with D3DRS_ALPHAREF=%lu (%.3f as a threshold)", alphaRef, ThresholdFromAlphaRef(alphaRef));
		}
	}
	return replacement;
}

void OnPixelShaderCreated(void*, void* shader) {
	// A new object at a known address means the old one is gone.
	g_cache.Forget(shader);
	g_dumped.erase(shader);
}

}  // namespace

SetupResult SetupDevice(void* rawDevice, const Options& options, LogFn log) {
	SetupResult result;
	g_log = log;
	g_options = options;

	// Refuse to call anything on a pointer whose method table does not look
	// like one; QueryInterface is the first real call.
	if (rawDevice == nullptr || !LooksLikeVtable(*static_cast<void* const* const*>(rawDevice), kSlotLast + 1)) {
		log("Device pointer %p does not carry a method table - nothing done", rawDevice);
		return result;
	}
	auto* unknown = static_cast<IUnknown*>(rawDevice);
	IDirect3DDevice9* device = nullptr;
	if (FAILED(unknown->QueryInterface(IID_IDirect3DDevice9, reinterpret_cast<void**>(&device))) || device == nullptr) {
		log("Pointer %p is not an IDirect3DDevice9 - nothing done", rawDevice);
		return result;
	}
	result.deviceAccepted = true;

	D3DDEVICE_CREATION_PARAMETERS creation{};
	device->GetCreationParameters(&creation);
	IDirect3D9* d3d = nullptr;
	if (SUCCEEDED(device->GetDirect3D(&d3d)) && d3d != nullptr) {
		D3DADAPTER_IDENTIFIER9 identifier{};
		if (SUCCEEDED(d3d->GetAdapterIdentifier(creation.AdapterOrdinal, 0, &identifier))) {
			result.vendorId = identifier.VendorId;
			result.vendor = VendorFromId(identifier.VendorId);
			log("Adapter %u: %s (vendor 0x%04X, %s)", creation.AdapterOrdinal, identifier.Description,
			    identifier.VendorId, VendorName(result.vendor));
		} else {
			log("GetAdapterIdentifier failed - vendor unknown");
		}
		// The support probe from NVIDIA's whitepaper: the 'ATOC' FourCC as a
		// surface format against the usual X8R8G8B8 adapter format.
		const HRESULT probe = d3d->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, D3DFMT_X8R8G8B8, 0,
		                                             D3DRTYPE_SURFACE, static_cast<D3DFORMAT>(kFourCCAtoc));
		result.atocFormatSupported = probe == D3D_OK;
		log("CheckDeviceFormat('ATOC') = 0x%08lX (%s)", probe, result.atocFormatSupported ? "supported" : "not supported");
		d3d->Release();
	} else {
		log("GetDirect3D failed - vendor unknown, 'ATOC' not probed");
	}

	IDirect3DSurface9* target = nullptr;
	if (SUCCEEDED(device->GetRenderTarget(0, &target)) && target != nullptr) {
		D3DSURFACE_DESC desc{};
		target->GetDesc(&desc);
		result.multiSampleType = desc.MultiSampleType;
		result.multiSampleQuality = desc.MultiSampleQuality;
		target->Release();
		if (TargetIsMultisampled(desc.MultiSampleType, desc.MultiSampleQuality)) {
			log("Render target 0: %ux%u, multisample type %u quality %u", desc.Width, desc.Height,
			    desc.MultiSampleType, desc.MultiSampleQuality);
		} else {
			log("Render target 0: %ux%u, NOT multisampled - alpha-to-coverage needs antialiasing "
			    "(iMultiSample above 0 in Oblivion.ini), there is nothing to gain until it is on",
			    desc.Width, desc.Height);
		}
	} else {
		log("GetRenderTarget(0) failed - sample count unknown");
	}
	device->Release();  // the QueryInterface reference; the engine keeps its own

	result.hack = ChooseHack(options.mode, result.vendor, result.atocFormatSupported);
	if (result.hack == Hack::None) {
		log("Mode=%s on %s: nothing enabled - %s", ModeName(options.mode), VendorName(result.vendor),
		    NoneReason(options.mode, result.vendor, result.atocFormatSupported));
		return result;
	}

	g_hack = result.hack;
	g_announcedLive = false;
	g_sharpen = options.sharpenLeaves;
	g_leafCopyBound = false;
	g_alphaRefsSeen.clear();
	g_dumped.clear();
	g_dumpCount = 0;
	char error[160] = {};
	const HookCallbacks callbacks{&OnBeginScene, &OnRenderState, &FilterPixelShader, &OnPixelShaderCreated};
	if (!InstallDeviceHooks(rawDevice, callbacks, error, sizeof(error))) {
		g_hack = Hack::None;
		g_sharpen = false;
		log("Could not hook the device: %s - nothing enabled", error);
		return result;
	}
	result.hooksInstalled = true;
	result.sharpening = g_sharpen;
	log("Mode=%s on %s: %s enabled, applied from the next frame on", ModeName(options.mode), VendorName(result.vendor),
	    HackName(result.hack));
	if (g_sharpen) {
		if (options.threshold < 0.0f) {
			log("Leaf sharpening on: STLEAF2000/STLEAF2001 get a' = sat((a - t) * %.1f + 0.5), t following the "
			    "engine's D3DRS_ALPHAREF",
			    SharpenScale(options.steepness));
		} else {
			log("Leaf sharpening on: STLEAF2000/STLEAF2001 get a' = sat((a - %.2f) * %.1f + 0.5)", options.threshold,
			    SharpenScale(options.steepness));
		}
	} else {
		log("Leaf sharpening off (SharpenLeaves=0): leaf alpha goes to coverage as the texture has it");
	}
	if (!options.dumpDirectory.empty()) {
		CreateDirectoryA(options.dumpDirectory.c_str(), nullptr);
		log("Shader dump on: every pixel shader the engine sets goes to %s once", options.dumpDirectory.c_str());
	}
	return result;
}

void TeardownDevice() {
	RemoveDeviceHooks();
	for (IDirect3DPixelShader9* shader : g_replacements) {
		shader->Release();
	}
	g_replacements.clear();
	g_cache = ShaderCache();
	g_hack = Hack::None;
	g_sharpen = false;
	g_announcedLive = false;
	g_leafCopyBound = false;
	g_alphaRefsSeen.clear();
	g_dumped.clear();
	g_dumpCount = 0;
}

}  // namespace foliageaa
