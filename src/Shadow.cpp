#include "Shadow.h"

#include "Coverage.h"

#include <cstring>

namespace foliageaa {

void ShadowRenderState(DeviceShadow* shadow, uint32_t state, uint32_t value) {
	switch (state) {
		case kRsAlphaTestEnable: shadow->alphaTestEnable = value; break;
		case kRsAlphaRef: shadow->alphaRef = value; break;
		case kRsMultiSampleMask: shadow->multiSampleMask = value; break;
		case kRsAlphaBlendEnable: shadow->alphaBlendEnable = value; break;
		case kRsZWriteEnable: shadow->zWriteEnable = value; break;
		default: break;
	}
}

namespace {

// The registers base..base+3 of a write, into rows.
void TakeRows(float rows[16], uint32_t base, uint32_t startRegister, const float* data, uint32_t count) {
	if (startRegister >= base + 4 || startRegister + count <= base) {
		return;
	}
	const uint32_t first = startRegister > base ? startRegister : base;
	const uint32_t end = startRegister + count < base + 4 ? startRegister + count : base + 4;
	for (uint32_t reg = first; reg < end; ++reg) {
		std::memcpy(rows + (reg - base) * 4, data + (reg - startRegister) * 4, 4 * sizeof(float));
	}
}

}  // namespace

void ShadowVertexConstants(DeviceShadow* shadow, uint32_t startRegister, const float* data, uint32_t count) {
	if (data == nullptr || count == 0) {
		return;
	}
	TakeRows(shadow->clipRows, 0, startRegister, data, count);
	TakeRows(shadow->grassRows, kGrassClipBase, startRegister, data, count);
}

void ShadowViewport(DeviceShadow* shadow, uint32_t width, uint32_t height) {
	shadow->viewportWidth = width;
	shadow->viewportHeight = height;
}

void ShadowRenderTarget(DeviceShadow* shadow, uint32_t multiSampleType) {
	// D3DMULTISAMPLE_NONE = 0, D3DMULTISAMPLE_NONMASKABLE = 1, then the
	// sample counts 2..16 (d3d9types.h).
	shadow->targetSamples = multiSampleType >= 2 && multiSampleType <= 16 ? multiSampleType : 0;
}

}  // namespace foliageaa
