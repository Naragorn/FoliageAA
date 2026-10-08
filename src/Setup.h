#pragma once

#include "Coverage.h"

#include <cstdint>
#include <string>

// What happens once Oblivion's device exists: read the adapter's vendor, probe
// the 'ATOC' format, look at the back buffer's sample count, pick the back
// door and install the hooks that apply it - and, with the leaf sharpening
// on, substitute the sharpened leaf shaders whenever the engine sets one
// (ShaderPatch.h says why). Everything decided here goes through Coverage
// and ShaderPatch; this file only asks the device and acts on the answers.

namespace foliageaa {

using LogFn = void (*)(const char* format, ...);

struct Options {
	Mode mode = Mode::Auto;
	bool sharpenLeaves = true;
	// Below 0: the threshold follows the engine's D3DRS_ALPHAREF at each
	// leaf draw. Otherwise this fixed value on the 0..1 alpha scale.
	float threshold = -1.0f;
	float steepness = 4.0f;
	// Non-empty: every pixel shader the engine sets is written to this
	// directory once, with the render states of that first moment in the
	// log - the way to learn which shader draws what.
	std::string dumpDirectory;
};

struct SetupResult {
	bool deviceAccepted = false;  // the pointer answered QueryInterface as an IDirect3DDevice9
	uint32_t vendorId = 0;
	Vendor vendor = Vendor::Other;
	bool atocFormatSupported = false;
	uint32_t multiSampleType = 0;
	uint32_t multiSampleQuality = 0;
	Hack hack = Hack::None;
	bool hooksInstalled = false;
	bool sharpening = false;  // leaf shaders will be substituted
};

// Runs the setup on a device pointer taken from the engine. Logs every step
// and every refusal; never throws, never dereferences a pointer that did not
// pass the method-table check.
SetupResult SetupDevice(void* device, const Options& options, LogFn log);

// Undoes SetupDevice's hooks and releases the sharpened shaders, for tests
// that set up more than once.
void TeardownDevice();

}  // namespace foliageaa
