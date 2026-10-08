#pragma once

#include "Shadow.h"

#include <cstdint>

// Transparency supersampling for the leaves, by hand.
//
// Why not coverage: alpha-to-coverage on NVIDIA's Vulkan driver is dithered -
// a pixel with half its samples covered gets a different half depending on
// where on the screen it is (dxvk issues 3222 and 3380, "alpha to coverage
// dithering", with no switch on NVIDIA). The pattern sticks to the screen
// while the leaves move under it. On a monitor, downscaled, that passes; in a
// headset at full resolution, with the head never still and each eye getting
// a different pattern, it glitters. Distant trees are nearly all partial
// coverage, so they glitter all over (reported 2026-10-08).
//
// What this does instead is what NVIDIA's driver offered under Direct3D 9 as
// "transparency supersampling" and DXVK does not implement: the engine's own
// alpha test, evaluated once per sample. A leaf draw is issued once per
// sample (or per group of samples), each pass writing only its samples
// through D3DRS_MULTISAMPLEMASK, with the clip-space matrix shifted so that
// the pixel-center evaluation lands where that sample lies. Eight passes on
// an 8-sample target give eight real alpha-test decisions per pixel: edges
// with nine levels of coverage from real geometry, no coverage mask, no
// dithering, as stable as 8x supersampling.
//
// What it costs: the leaf draws' vertex and pixel work as many times as
// there are passes, and that many draw calls. Nothing else is touched.
//
// The shift goes into the vertex constants c0 and c1: every vanilla leaf
// vertex shader computes oPos.x = dot(c0, p), oPos.y = dot(c1, p), oPos.w =
// dot(c3, p) (LeafShaders.h), so adding s * c3 to c0 shifts clip x by s * w,
// a pure screen-space translation of s / 2 viewports.
//
// Where the samples lie: Vulkan's standard sample locations (the spec's
// "Standard sample locations" table), which the device uses when it reports
// standardSampleLocations - the RTX 4090 does (vulkaninfo, 2026-10-08).
// D3D9 under DXVK draws into Vulkan's framebuffer space, y down, the same
// orientation as D3D9's window space. Should a device lie elsewhere, the
// passes still evaluate at as many spread-out points inside the pixel and
// the picture is still supersampled; only which sample holds which
// evaluation changes.

namespace foliageaa {

struct SamplePosition {
	float x;
	float y;
};

// The standard locations for 2, 4 or 8 samples, inside the pixel (0..1, y
// down). False for any other count.
bool StandardSamplePositions(uint32_t count, SamplePosition out[8]);

struct SupersamplePass {
	uint32_t mask;  // D3DRS_MULTISAMPLEMASK for this pass
	float offsetX;  // where the pass is evaluated, in pixels from the center, y down
	float offsetY;
};

constexpr int kMaxPasses = 8;

// Splits the target's samples over passes: as many as requested, at most 8,
// a power of two that divides the sample count. Each pass writes a run of
// consecutive samples and is evaluated at one of the standard positions for
// that many passes (with one pass per sample, each sample's own). Returns
// the number of passes; 1 means "draw once, plainly" and fills nothing.
int PlanPasses(uint32_t targetSamples, int requested, SupersamplePass out[kMaxPasses]);

// The rows c0 and c1 shifted for a pass: the content at (offsetX, offsetY)
// pixels from the center is moved onto the center, i.e. by -offsetX pixels
// in x and -offsetY in window y (which is +offsetY in NDC, y up), each as a
// multiple of row c3. rows holds c0..c3, outRows receives the new c0 and c1.
void JitterClipRows(const float rows[16], float offsetX, float offsetY, uint32_t viewportWidth,
                    uint32_t viewportHeight, float outRows[8]);

struct LeafDrawPlan {
	int passes = 1;
	SupersamplePass pass[kMaxPasses] = {};
};

// Whether the draw about to happen is a leaf draw to supersample - a leaf
// pixel shader and a leaf vertex shader bound, the alpha test on, a
// multisampled target with a usable mask, a viewport - and how. False
// means: let it draw once.
bool PlanLeafDraw(const DeviceShadow& shadow, int requestedPasses, LeafDrawPlan* plan);
// Why PlanLeafDraw said no, for the log; "" when it said yes.
const char* LeafDrawRefusal(const DeviceShadow& shadow, int requestedPasses);

}  // namespace foliageaa
