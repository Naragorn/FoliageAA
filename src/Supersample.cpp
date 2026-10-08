#include "Supersample.h"

namespace foliageaa {

bool StandardSamplePositions(uint32_t count, SamplePosition out[8]) {
	// Vulkan specification, "Standard sample locations".
	switch (count) {
		case 2:
			out[0] = {0.75f, 0.75f};
			out[1] = {0.25f, 0.25f};
			return true;
		case 4:
			out[0] = {0.375f, 0.125f};
			out[1] = {0.875f, 0.375f};
			out[2] = {0.125f, 0.625f};
			out[3] = {0.625f, 0.875f};
			return true;
		case 8:
			out[0] = {0.5625f, 0.3125f};
			out[1] = {0.4375f, 0.6875f};
			out[2] = {0.8125f, 0.5625f};
			out[3] = {0.3125f, 0.1875f};
			out[4] = {0.1875f, 0.8125f};
			out[5] = {0.0625f, 0.4375f};
			out[6] = {0.6875f, 0.9375f};
			out[7] = {0.9375f, 0.0625f};
			return true;
		default:
			return false;
	}
}

int PlanPasses(uint32_t targetSamples, int requested, SupersamplePass out[kMaxPasses]) {
	if (targetSamples != 2 && targetSamples != 4 && targetSamples != 8 && targetSamples != 16) {
		return 1;
	}
	int passes = requested;
	if (passes > kMaxPasses) {
		passes = kMaxPasses;
	}
	if (passes > static_cast<int>(targetSamples)) {
		passes = static_cast<int>(targetSamples);
	}
	// Down to a power of two, so the samples split evenly.
	int power = 1;
	while (power * 2 <= passes) {
		power *= 2;
	}
	passes = power;
	if (passes < 2) {
		return 1;
	}
	SamplePosition positions[8];
	if (!StandardSamplePositions(static_cast<uint32_t>(passes), positions)) {
		return 1;
	}
	const uint32_t perPass = targetSamples / static_cast<uint32_t>(passes);
	const uint32_t runMask = (1u << perPass) - 1u;
	for (int i = 0; i < passes; ++i) {
		out[i].mask = runMask << (static_cast<uint32_t>(i) * perPass);
		out[i].offsetX = positions[i].x - 0.5f;
		out[i].offsetY = positions[i].y - 0.5f;
	}
	return passes;
}

void JitterClipRows(const float rows[16], float offsetX, float offsetY, uint32_t viewportWidth,
                    uint32_t viewportHeight, float outRows[8]) {
	// One pixel is 2 / size of NDC; window y runs down, NDC y up.
	const float shiftX = -offsetX * 2.0f / static_cast<float>(viewportWidth);
	const float shiftY = offsetY * 2.0f / static_cast<float>(viewportHeight);
	for (int i = 0; i < 4; ++i) {
		outRows[i] = rows[i] + shiftX * rows[12 + i];
		outRows[4 + i] = rows[4 + i] + shiftY * rows[12 + i];
	}
}

const char* LeafDrawRefusal(const DeviceShadow& shadow, int requestedPasses) {
	if (shadow.pixelShader == LeafShader::None) {
		return "no leaf pixel shader bound";
	}
	if (shadow.vertexShader == LeafVertexShader::None) {
		return "the leaf pixel shader is bound with a vertex shader the plugin does not know";
	}
	if (shadow.alphaTestEnable == 0) {
		return "the alpha test is off";
	}
	if (shadow.targetSamples < 2) {
		return "render target 0 is not multisampled (or its samples cannot be masked)";
	}
	if (shadow.viewportWidth == 0 || shadow.viewportHeight == 0) {
		return "no viewport";
	}
	SupersamplePass passes[kMaxPasses];
	if (PlanPasses(shadow.targetSamples, requestedPasses, passes) < 2) {
		return "fewer than two passes asked for, or an unsupported sample count";
	}
	return "";
}

bool PlanLeafDraw(const DeviceShadow& shadow, int requestedPasses, LeafDrawPlan* plan) {
	if (LeafDrawRefusal(shadow, requestedPasses)[0] != '\0') {
		return false;
	}
	plan->passes = PlanPasses(shadow.targetSamples, requestedPasses, plan->pass);
	return plan->passes >= 2;
}

}  // namespace foliageaa
