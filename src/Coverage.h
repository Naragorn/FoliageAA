#pragma once

#include <cstdint>

// The decisions behind the plugin, as pure functions over plain values, so
// every flow can be tested without a Direct3D device.
//
// What the plugin does: Oblivion draws leaves, grass and the like with an
// alpha test, a hard on/off per pixel that shimmers at a distance. Direct3D 9
// has no alpha-to-coverage of its own, but the GPU vendors expose it through
// two render-state back doors that Fallout 3 and New Vegas use as their
// "Transparency Multisampling" launcher option, and that Oblivion.exe never
// calls (its binary holds none of the FourCC values below):
//
//   NVIDIA (also Intel): SetRenderState(D3DRS_ADAPTIVETESS_Y, 'ATOC'), which
//     the driver applies only while D3DRS_ALPHATESTENABLE is on, and turns
//     off again with D3DFMT_UNKNOWN. ("Antialiasing with Transparency",
//     NVIDIA SDK whitepaper; Aras Pranckevicius, "D3D9 GPU Hacks".)
//   AMD: SetRenderState(D3DRS_POINTSIZE, 'A2M1') to enable and 'A2M0' to
//     disable, with no alpha-test gate of its own. ("Advanced DX9
//     Capabilities for ATI Radeon Cards".)
//
// DXVK implements both the same way (src/d3d9/d3d9_device.cpp,
// UpdateAlphaToCoverangeAndAlphaTest): the NVIDIA path needs the alpha test
// on and a non-AMD adapter, the AMD path needs an AMD adapter, and either
// needs render target 0 to be multisampled.

namespace foliageaa {

// d3d9types.h: D3DRS_ALPHATESTENABLE = 15, D3DRS_POINTSIZE = 154,
// D3DRS_ADAPTIVETESS_Y = 181.
constexpr uint32_t kRsAlphaTestEnable = 15;
constexpr uint32_t kRsPointSize = 154;
constexpr uint32_t kRsAdaptiveTessY = 181;

constexpr uint32_t FourCC(char a, char b, char c, char d) {
	return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
	       (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
	       (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) |
	       (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
}
constexpr uint32_t kFourCCAtoc = FourCC('A', 'T', 'O', 'C');
constexpr uint32_t kFourCCA2M1 = FourCC('A', '2', 'M', '1');
constexpr uint32_t kFourCCA2M0 = FourCC('A', '2', 'M', '0');
constexpr uint32_t kFourCCNone = 0;  // D3DFMT_UNKNOWN: ATOC off

// PCI vendor ids as D3DADAPTER_IDENTIFIER9::VendorId reports them.
constexpr uint32_t kVendorIdNvidia = 0x10DE;
constexpr uint32_t kVendorIdAmd = 0x1002;
constexpr uint32_t kVendorIdIntel = 0x8086;

enum class Vendor { Nvidia, Amd, Intel, Other };
Vendor VendorFromId(uint32_t vendorId);
const char* VendorName(Vendor vendor);

// The INI's Mode: which back door to use, or none.
enum class Mode { Auto, Nvidia, Amd, Off };
// Case-insensitive "auto", "nvidia", "amd", "off"; false and *out untouched
// for anything else.
bool ParseMode(const char* text, Mode* out);
const char* ModeName(Mode mode);

enum class Hack { None, NvidiaAtoc, AmdA2M };
const char* HackName(Hack hack);

// Which back door to use on this device. atocFormatSupported is what
// IDirect3D9::CheckDeviceFormat answered for the 'ATOC' surface format, the
// support probe the NVIDIA whitepaper prescribes; DXVK answers yes on every
// non-AMD adapter and no on AMD.
Hack ChooseHack(Mode mode, Vendor vendor, bool atocFormatSupported);
// Why ChooseHack answered None, for the log.
const char* NoneReason(Mode mode, Vendor vendor, bool atocFormatSupported);

struct StateWrite {
	uint32_t state;
	uint32_t value;
};

// The render states to write after each BeginScene. Per frame rather than
// once, so a device Reset - which clears every render state - heals itself
// on the next frame. Returns the count written into out.
int BeginSceneWrites(Hack hack, StateWrite out[2]);

// The render states to write right after the engine wrote
// D3DRS_ALPHATESTENABLE itself. The AMD back door has no alpha-test gate, so
// the plugin follows the engine's alpha test with A2M1/A2M0; otherwise blended
// particles and overlays would get coverage too. Returns the count.
int AlphaTestWrites(Hack hack, uint32_t alphaTestEnabled, StateWrite out[2]);

// The render states that take coverage off for a stretch of draws that use
// the plain alpha test instead (the supersampling passes), and back on after
// them - the AMD back door being tied to the alpha test state at that
// moment. Returns the count.
int SuspendCoverageWrites(Hack hack, StateWrite out[2]);
int ResumeCoverageWrites(Hack hack, uint32_t alphaTestEnabled, StateWrite out[2]);

// Whether a render target with this D3DMULTISAMPLE_TYPE and quality is one
// alpha-to-coverage can act on: two or more samples, or NONMASKABLE (1) with
// a quality above 0 - DXVK's rule, and the plain meaning of the enum.
bool TargetIsMultisampled(uint32_t multiSampleType, uint32_t multiSampleQuality);

// How the leaves are antialiased: the supersampling passes (Supersample.h)
// or coverage with the sharpened shader (ShaderPatch.h).
enum class LeafMethod { Supersample, Coverage };
// Case-insensitive "supersample" or "coverage"; false and *out untouched for
// anything else.
bool ParseLeafMethod(const char* text, LeafMethod* out);
const char* LeafMethodName(LeafMethod method);

}  // namespace foliageaa
