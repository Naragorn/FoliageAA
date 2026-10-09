#pragma once

#include "Coverage.h"

#include <cstdint>
#include <string>

// What happens once Oblivion's device exists: read the adapter's vendor, probe
// the 'ATOC' format, look at the back buffer's sample count, pick the back
// door and install the hooks that apply it; keep a shadow of the state the
// engine sets (Shadow.h); and treat the leaf draws by the chosen method -
// supersampling passes (Supersample.h) or the sharpened shader under
// coverage (ShaderPatch.h). Everything decided here goes through those pure
// modules; this file only asks the device and acts on the answers.

namespace foliageaa {

using LogFn = void (*)(const char* format, ...);

struct Options {
	Mode mode = Mode::Auto;
	// [Coverage] Enable: alpha-to-coverage for every alpha-tested draw
	// (grass, hair, fences, and the leaves under the coverage method). Off
	// by default: dithered on NVIDIA's Vulkan driver, and in a headset that
	// glittered on hair up close (2026-10-08).
	bool coverage = false;
	LeafMethod leafMethod = LeafMethod::Supersample;
	int passes = 8;  // supersampling passes asked for (1..8)
	// Coverage method only:
	bool sharpenLeaves = true;
	// Below 0: the threshold follows the engine's D3DRS_ALPHAREF at each
	// leaf draw. Otherwise this fixed value on the 0..1 alpha scale.
	float threshold = -1.0f;
	float steepness = 4.0f;
	// Non-empty: every pixel shader the engine sets is written to this
	// directory once, with the render states of that first moment in the
	// log - the way to learn which shader draws what.
	std::string dumpDirectory;
	// Non-zero: a Windows virtual-key code that switches the plugin's work
	// off and on while the game runs, polled at each BeginScene - for
	// comparing in the headset. 0: no key.
	int toggleKey = 0;
};

// The key state the toggle reads; GetAsyncKeyState unless a test swaps it.
using KeyReader = bool (*)(int virtualKey);
void SetKeyReaderForTest(KeyReader reader);

struct SetupResult {
	bool deviceAccepted = false;  // the pointer answered QueryInterface as an IDirect3DDevice9
	uint32_t behaviorFlags = 0;
	uint32_t vendorId = 0;
	Vendor vendor = Vendor::Other;
	bool atocFormatSupported = false;
	uint32_t multiSampleType = 0;
	uint32_t multiSampleQuality = 0;
	Hack hack = Hack::None;
	bool hooksInstalled = false;
	bool sharpening = false;     // leaf shaders will be substituted (coverage method)
	bool supersampling = false;  // leaf draws will be issued in passes
};

// Runs the setup on a device pointer taken from the engine. Logs every step
// and every refusal; never throws, never dereferences a pointer that did not
// pass the method-table check.
SetupResult SetupDevice(void* device, const Options& options, LogFn log);

// Undoes SetupDevice's hooks and releases the sharpened shaders, for tests
// that set up more than once.
void TeardownDevice();

}  // namespace foliageaa
