#include "Shadow.h"

#include "Coverage.h"

#include <cstring>

namespace foliageaa {

void ShadowRenderState(DeviceShadow* shadow, uint32_t state, uint32_t value) {
	switch (state) {
		case kRsAlphaTestEnable: shadow->alphaTestEnable = value; break;
		case kRsAlphaRef: shadow->alphaRef = value; break;
		case kRsMultiSampleMask: shadow->multiSampleMask = value; break;
		default: break;
	}
}

void ShadowVertexConstants(DeviceShadow* shadow, uint32_t startRegister, const float* data, uint32_t count) {
	if (data == nullptr || startRegister >= 4) {
		return;
	}
	const uint32_t end = startRegister + count < 4 ? startRegister + count : 4;
	for (uint32_t reg = startRegister; reg < end; ++reg) {
		std::memcpy(shadow->clipRows + reg * 4, data + (reg - startRegister) * 4, 4 * sizeof(float));
	}
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
