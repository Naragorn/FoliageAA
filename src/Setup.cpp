#include "Setup.h"

#include "DeviceHook.h"
#include "ShaderPatch.h"
#include "Shadow.h"
#include "Supersample.h"

// initguid.h first: IID_IDirect3DDevice9 is then defined in this file instead
// of needing d3d9.lib, which would pull a second d3d9.dll into the game.
#include <initguid.h>
#include <d3d9.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace foliageaa {

namespace {

Hack g_hack = Hack::None;
LogFn g_log = nullptr;
bool g_announcedLive = false;

Options g_options;
DeviceShadow g_shadow;
bool g_sharpen = false;      // coverage method: substitute the sharpened copies
bool g_supersample = false;  // supersample method: leaf draws in passes
ShaderCache g_pixelShaders;
ShaderCache g_vertexShaders;
std::vector<IDirect3DPixelShader9*> g_replacements;  // owned, released at teardown
bool g_leafCopyBound = false;                         // the pixel shader last set is one of the copies
std::vector<uint32_t> g_alphaRefsSeen;                // distinct D3DRS_ALPHAREF values at leaf sets, first 16
std::unordered_set<void*> g_dumped;
int g_dumpCount = 0;
bool g_announcedSupersample = false;
int g_unknownVertexShaderLogs = 0;

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
	const float threshold = g_options.threshold < 0.0f ? ThresholdFromAlphaRef(g_shadow.alphaRef) : g_options.threshold;
	float values[4];
	SharpenConstants(threshold, g_options.steepness, values);
	device->SetPixelShaderConstantF(kSharpenConstantRegister, values, 1);
}

void OnRenderState(void* device, uint32_t state, uint32_t value) {
	ShadowRenderState(&g_shadow, state, value);
	if (state == kRsAlphaTestEnable) {
		StateWrite writes[2];
		const int count = AlphaTestWrites(g_hack, value, writes);
		ApplyWrites(device, writes, count);
		return;
	}
	// The threshold moved while a copy is bound.
	if (state == kRsAlphaRef && g_leafCopyBound && g_options.threshold < 0.0f) {
		WriteSharpenConstants(static_cast<IDirect3DDevice9*>(device));
	}
}

std::vector<uint8_t> FunctionOf(IUnknown* shader, bool vertex) {
	UINT size = 0;
	HRESULT hr = vertex ? static_cast<IDirect3DVertexShader9*>(shader)->GetFunction(nullptr, &size)
	                    : static_cast<IDirect3DPixelShader9*>(shader)->GetFunction(nullptr, &size);
	if (FAILED(hr) || size == 0 || size > 65536) {
		return {};
	}
	std::vector<uint8_t> bytes(size);
	hr = vertex ? static_cast<IDirect3DVertexShader9*>(shader)->GetFunction(bytes.data(), &size)
	            : static_cast<IDirect3DPixelShader9*>(shader)->GetFunction(bytes.data(), &size);
	if (FAILED(hr)) {
		return {};
	}
	return bytes;
}

// The first time a leaf pixel shader is set under the coverage method, a
// sharpened copy is made to stand in for it from then on.
void* MakeReplacement(IDirect3DDevice9* device, LeafShader kind, const std::vector<uint8_t>& bytes) {
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

ShaderEntry ClassifyPixelShader(IDirect3DDevice9* device, IDirect3DPixelShader9* shader) {
	ShaderEntry entry;
	const std::vector<uint8_t> bytes = FunctionOf(shader, false);
	const LeafShader kind = IdentifyLeafShader(bytes.data(), bytes.size());
	entry.kind = static_cast<int>(kind);
	if (kind != LeafShader::None && g_sharpen) {
		entry.replacement = MakeReplacement(device, kind, bytes);
	}
	return entry;
}

void DumpShader(IDirect3DDevice9* device, IDirect3DPixelShader9* shader) {
	if (!g_dumped.insert(shader).second) {
		return;
	}
	const std::vector<uint8_t> bytes = FunctionOf(shader, false);
	const int index = g_dumpCount++;
	char path[MAX_PATH];
	std::snprintf(path, sizeof(path), "%s\\ps%03d_%u.pso", g_options.dumpDirectory.c_str(), index,
	              static_cast<unsigned>(bytes.size()));
	bool written = false;
	if (FILE* file = std::fopen(path, "wb")) {
		written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
		std::fclose(file);
	}
	DWORD blend = 0, src = 0, dst = 0, zWrite = 0;
	device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
	device->GetRenderState(D3DRS_SRCBLEND, &src);
	device->GetRenderState(D3DRS_DESTBLEND, &dst);
	device->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
	g_log("Shader dump %03d: %u bytes %s, first set with alphaTest=%u alphaRef=%u blend=%lu src=%lu dst=%lu "
	      "zWrite=%lu (%s)",
	      index, static_cast<unsigned>(bytes.size()), written ? "written" : "NOT written", g_shadow.alphaTestEnable,
	      g_shadow.alphaRef, blend, src, dst, zWrite, LeafShaderName(IdentifyLeafShader(bytes.data(), bytes.size())));
}

void* FilterPixelShader(void* rawDevice, void* rawShader) {
	auto* device = static_cast<IDirect3DDevice9*>(rawDevice);
	auto* shader = static_cast<IDirect3DPixelShader9*>(rawShader);
	if (shader == nullptr) {
		g_shadow.pixelShader = LeafShader::None;
		g_leafCopyBound = false;
		return rawShader;
	}
	if (!g_options.dumpDirectory.empty()) {
		DumpShader(device, shader);
	}
	ShaderEntry entry;
	if (!g_pixelShaders.Lookup(rawShader, &entry)) {
		entry = ClassifyPixelShader(device, shader);
		g_pixelShaders.Remember(rawShader, entry);
	}
	g_shadow.pixelShader = static_cast<LeafShader>(entry.kind);
	if (entry.replacement == nullptr) {
		g_leafCopyBound = false;
		return rawShader;
	}
	g_leafCopyBound = true;
	// The constants belong to the copy about to be bound: SetPixelShader
	// does not touch constant registers, so writing them here, before the
	// original call binds the copy, is in order.
	WriteSharpenConstants(device);
	if (g_options.threshold < 0.0f && g_alphaRefsSeen.size() < 16) {
		bool seen = false;
		for (uint32_t known : g_alphaRefsSeen) {
			seen = seen || known == g_shadow.alphaRef;
		}
		if (!seen) {
			g_alphaRefsSeen.push_back(g_shadow.alphaRef);
			g_log("Leaf shader set with D3DRS_ALPHAREF=%u (%.3f as a threshold)", g_shadow.alphaRef,
			      ThresholdFromAlphaRef(g_shadow.alphaRef));
		}
	}
	return entry.replacement;
}

void OnPixelShaderCreated(void*, void* shader) {
	// A new object at a known address means the old one is gone.
	g_pixelShaders.Forget(shader);
	g_dumped.erase(shader);
}

void OnVertexShader(void*, void* rawShader) {
	if (rawShader == nullptr) {
		g_shadow.vertexShader = LeafVertexShader::None;
		return;
	}
	ShaderEntry entry;
	if (!g_vertexShaders.Lookup(rawShader, &entry)) {
		const std::vector<uint8_t> bytes = FunctionOf(static_cast<IUnknown*>(rawShader), true);
		entry.kind = static_cast<int>(IdentifyLeafVertexShader(bytes.data(), bytes.size()));
		g_vertexShaders.Remember(rawShader, entry);
	}
	g_shadow.vertexShader = static_cast<LeafVertexShader>(entry.kind);
}

void OnVertexShaderCreated(void*, void* shader) {
	g_vertexShaders.Forget(shader);
}

void OnVertexConstants(void*, uint32_t startRegister, const float* data, uint32_t count) {
	ShadowVertexConstants(&g_shadow, startRegister, data, count);
}

void OnViewport(void*, const void* viewport) {
	const auto* vp = static_cast<const D3DVIEWPORT9*>(viewport);
	ShadowViewport(&g_shadow, vp->Width, vp->Height);
}

void OnRenderTarget(void*, uint32_t index, void* surface) {
	if (index != 0) {
		return;
	}
	if (surface == nullptr) {
		ShadowRenderTarget(&g_shadow, 0);
		return;
	}
	D3DSURFACE_DESC desc{};
	if (SUCCEEDED(static_cast<IDirect3DSurface9*>(surface)->GetDesc(&desc))) {
		ShadowRenderTarget(&g_shadow, desc.MultiSampleType);
	} else {
		ShadowRenderTarget(&g_shadow, 0);
	}
}

long FilterDraw(void* rawDevice, DrawIssue issue, void* context) {
	if (!g_supersample || g_shadow.pixelShader == LeafShader::None) {
		return issue(context);
	}
	LeafDrawPlan plan;
	if (!PlanLeafDraw(g_shadow, g_options.passes, &plan)) {
		if (g_shadow.vertexShader == LeafVertexShader::None && g_unknownVertexShaderLogs < 3) {
			++g_unknownVertexShaderLogs;
			g_log("Leaf draw not supersampled: %s", LeafDrawRefusal(g_shadow, g_options.passes));
		}
		return issue(context);
	}
	// The engine's alpha test must decide in these passes, not coverage.
	StateWrite writes[2];
	int count = SuspendCoverageWrites(g_hack, writes);
	ApplyWrites(rawDevice, writes, count);

	float jittered[8];
	long result = 0;
	for (int i = 0; i < plan.passes; ++i) {
		JitterClipRows(g_shadow.clipRows, plan.pass[i].offsetX, plan.pass[i].offsetY, g_shadow.viewportWidth,
		               g_shadow.viewportHeight, jittered);
		SetVertexShaderConstantFDirect(rawDevice, 0, jittered, 2);
		SetRenderStateDirect(rawDevice, kRsMultiSampleMask, plan.pass[i].mask);
		const long hr = issue(context);
		if (hr < 0) {
			result = hr;
		}
	}
	SetVertexShaderConstantFDirect(rawDevice, 0, g_shadow.clipRows, 2);
	SetRenderStateDirect(rawDevice, kRsMultiSampleMask, g_shadow.multiSampleMask);
	count = ResumeCoverageWrites(g_hack, g_shadow.alphaTestEnable, writes);
	ApplyWrites(rawDevice, writes, count);

	if (!g_announcedSupersample) {
		g_announcedSupersample = true;
		g_log("First leaf draw supersampled: %d passes on a %u-sample target, viewport %ux%u, %s with %s", plan.passes,
		      g_shadow.targetSamples, g_shadow.viewportWidth, g_shadow.viewportHeight, LeafShaderName(g_shadow.pixelShader),
		      LeafVertexShaderName(g_shadow.vertexShader));
	}
	return result;
}

// What the device will answer about its current state - works under DXVK,
// not on Microsoft's runtime for a pure device, where the shadow fills in
// from the engine's next writes instead.
void ReadShadowFromDevice(IDirect3DDevice9* device) {
	DWORD value = 0;
	if (SUCCEEDED(device->GetRenderState(D3DRS_ALPHATESTENABLE, &value))) {
		ShadowRenderState(&g_shadow, kRsAlphaTestEnable, value);
	}
	if (SUCCEEDED(device->GetRenderState(D3DRS_ALPHAREF, &value))) {
		ShadowRenderState(&g_shadow, kRsAlphaRef, value);
	}
	if (SUCCEEDED(device->GetRenderState(D3DRS_MULTISAMPLEMASK, &value))) {
		ShadowRenderState(&g_shadow, kRsMultiSampleMask, value);
	}
	D3DVIEWPORT9 viewport{};
	if (SUCCEEDED(device->GetViewport(&viewport))) {
		ShadowViewport(&g_shadow, viewport.Width, viewport.Height);
	}
	float rows[16];
	if (SUCCEEDED(device->GetVertexShaderConstantF(0, rows, 4))) {
		ShadowVertexConstants(&g_shadow, 0, rows, 4);
	}
	IDirect3DVertexShader9* vs = nullptr;
	if (SUCCEEDED(device->GetVertexShader(&vs)) && vs != nullptr) {
		OnVertexShader(device, vs);
		vs->Release();
	}
	IDirect3DPixelShader9* ps = nullptr;
	if (SUCCEEDED(device->GetPixelShader(&ps)) && ps != nullptr) {
		const std::vector<uint8_t> bytes = FunctionOf(ps, false);
		g_shadow.pixelShader = IdentifyLeafShader(bytes.data(), bytes.size());
		ps->Release();
	}
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
	result.behaviorFlags = creation.BehaviorFlags;
	log("Device behavior flags 0x%08lX%s", creation.BehaviorFlags,
	    (creation.BehaviorFlags & D3DCREATE_PUREDEVICE) != 0
	        ? " (a pure device: state is shadowed from the engine's writes, not read back)"
	        : "");
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

	g_shadow = DeviceShadow();
	IDirect3DSurface9* target = nullptr;
	if (SUCCEEDED(device->GetRenderTarget(0, &target)) && target != nullptr) {
		D3DSURFACE_DESC desc{};
		target->GetDesc(&desc);
		result.multiSampleType = desc.MultiSampleType;
		result.multiSampleQuality = desc.MultiSampleQuality;
		ShadowRenderTarget(&g_shadow, desc.MultiSampleType);
		target->Release();
		if (TargetIsMultisampled(desc.MultiSampleType, desc.MultiSampleQuality)) {
			log("Render target 0: %ux%u, multisample type %u quality %u", desc.Width, desc.Height,
			    desc.MultiSampleType, desc.MultiSampleQuality);
		} else {
			log("Render target 0: %ux%u, NOT multisampled - both coverage and supersampling need antialiasing "
			    "(iMultiSample above 0 in Oblivion.ini), there is nothing to gain until it is on",
			    desc.Width, desc.Height);
		}
	} else {
		log("GetRenderTarget(0) failed - sample count unknown");
	}

	if (options.coverage) {
		result.hack = ChooseHack(options.mode, result.vendor, result.atocFormatSupported);
		if (result.hack == Hack::None) {
			log("Coverage: Mode=%s on %s gives nothing - %s", ModeName(options.mode), VendorName(result.vendor),
			    NoneReason(options.mode, result.vendor, result.atocFormatSupported));
		}
	} else {
		log("Coverage: off in the INI");
	}
	const bool supersample = options.leafMethod == LeafMethod::Supersample && options.passes >= 2;
	if (result.hack == Hack::None && !supersample) {
		log("Nothing to do: no coverage and no supersampling - the device is left alone");
		device->Release();
		return result;
	}

	g_hack = result.hack;
	g_announcedLive = false;
	g_sharpen = options.leafMethod == LeafMethod::Coverage && options.sharpenLeaves && result.hack != Hack::None;
	g_supersample = supersample;
	g_leafCopyBound = false;
	g_alphaRefsSeen.clear();
	g_dumped.clear();
	g_dumpCount = 0;
	g_announcedSupersample = false;
	g_unknownVertexShaderLogs = 0;
	g_pixelShaders = ShaderCache();
	g_vertexShaders = ShaderCache();
	ReadShadowFromDevice(device);
	device->Release();  // the QueryInterface reference; the engine keeps its own

	char error[160] = {};
	const HookCallbacks callbacks{&OnBeginScene,     &OnRenderState,    &FilterPixelShader, &OnPixelShaderCreated,
	                              &OnVertexShader,   &OnVertexShaderCreated, &OnVertexConstants, &OnViewport,
	                              &OnRenderTarget,   &FilterDraw};
	if (!InstallDeviceHooks(rawDevice, callbacks, error, sizeof(error))) {
		g_hack = Hack::None;
		g_sharpen = false;
		g_supersample = false;
		log("Could not hook the device: %s - nothing enabled", error);
		return result;
	}
	result.hooksInstalled = true;
	result.sharpening = g_sharpen;
	result.supersampling = g_supersample;
	if (result.hack != Hack::None) {
		log("Coverage: %s enabled on %s, applied from the next frame on", HackName(result.hack), VendorName(result.vendor));
	}
	if (g_supersample) {
		log("Leaves: supersampled - each leaf draw issued in up to %d passes, one run of samples each, with the engine's "
		    "alpha test (coverage off for those draws)",
		    options.passes);
	} else if (options.leafMethod == LeafMethod::Supersample) {
		log("Leaves: Passes=%d asks for no supersampling - leaves drawn plainly", options.passes);
	} else if (g_sharpen) {
		if (options.threshold < 0.0f) {
			log("Leaves: coverage with sharpening - STLEAF2000/STLEAF2001 get a' = sat((a - t) * %.1f + 0.5), t following "
			    "the engine's D3DRS_ALPHAREF",
			    SharpenScale(options.steepness));
		} else {
			log("Leaves: coverage with sharpening - STLEAF2000/STLEAF2001 get a' = sat((a - %.2f) * %.1f + 0.5)",
			    options.threshold, SharpenScale(options.steepness));
		}
	} else if (result.hack != Hack::None) {
		log("Leaves: coverage without sharpening - leaf alpha goes to coverage as the texture has it");
	} else {
		log("Leaves: coverage asked for but no coverage is active - leaves drawn plainly");
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
	g_pixelShaders = ShaderCache();
	g_vertexShaders = ShaderCache();
	g_shadow = DeviceShadow();
	g_hack = Hack::None;
	g_sharpen = false;
	g_supersample = false;
	g_announcedLive = false;
	g_leafCopyBound = false;
	g_alphaRefsSeen.clear();
	g_dumped.clear();
	g_dumpCount = 0;
	g_announcedSupersample = false;
	g_unknownVertexShaderLogs = 0;
}

}  // namespace foliageaa
