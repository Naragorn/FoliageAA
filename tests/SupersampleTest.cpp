// Every flow of the shadow and the supersampling plan without a device: the
// state the shadow keeps and ignores, the sample tables, the pass plan for
// every sample count and request, the jitter arithmetic, and each gate of
// the leaf-draw decision.

#include "Coverage.h"
#include "Shadow.h"
#include "Supersample.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what);
	}
}

bool Near(float a, float b) {
	return std::fabs(a - b) < 1e-6f;
}

using namespace foliageaa;

void TestShadow() {
	DeviceShadow shadow;
	Check(shadow.alphaTestEnable == 0 && shadow.alphaRef == 0 && shadow.multiSampleMask == 0xFFFFFFFF &&
	          shadow.targetSamples == 0 && shadow.viewportWidth == 0 && shadow.pixelShader == LeafShader::None &&
	          shadow.vertexShader == LeafVertexShader::None,
	      "defaults");
	ShadowRenderState(&shadow, kRsAlphaTestEnable, 1);
	ShadowRenderState(&shadow, kRsAlphaRef, 84);
	ShadowRenderState(&shadow, kRsMultiSampleMask, 0x0F);
	ShadowRenderState(&shadow, kRsAdaptiveTessY, kFourCCAtoc);  // not followed
	Check(shadow.alphaTestEnable == 1 && shadow.alphaRef == 84 && shadow.multiSampleMask == 0x0F, "render states followed");

	const float rows[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	ShadowVertexConstants(&shadow, 0, rows, 4);
	Check(std::memcmp(shadow.clipRows, rows, sizeof(rows)) == 0, "c0..c3 taken whole");
	const float row2[4] = {-1, -2, -3, -4};
	ShadowVertexConstants(&shadow, 2, row2, 1);
	Check(shadow.clipRows[8] == -1 && shadow.clipRows[11] == -4 && shadow.clipRows[7] == 8 && shadow.clipRows[12] == 13,
	      "a single row replaced");
	const float later[8] = {99, 99, 99, 99, 99, 99, 99, 99};
	ShadowVertexConstants(&shadow, 5, later, 2);
	Check(shadow.clipRows[15] == 16, "registers past c3 ignored");
	ShadowVertexConstants(&shadow, 3, later, 2);
	Check(shadow.clipRows[12] == 99 && shadow.clipRows[15] == 99 && shadow.clipRows[11] == -4, "a write over c3 and c4 takes c3 only");
	ShadowVertexConstants(&shadow, 0, nullptr, 4);
	Check(shadow.clipRows[0] == 1, "null data ignored");

	ShadowViewport(&shadow, 4028, 3380);
	Check(shadow.viewportWidth == 4028 && shadow.viewportHeight == 3380, "viewport");
	ShadowRenderTarget(&shadow, 8);
	Check(shadow.targetSamples == 8, "8-sample target");
	ShadowRenderTarget(&shadow, 1);
	Check(shadow.targetSamples == 0, "NONMASKABLE has no usable mask");
	ShadowRenderTarget(&shadow, 0);
	Check(shadow.targetSamples == 0, "single-sample target");
	ShadowRenderTarget(&shadow, 16);
	Check(shadow.targetSamples == 16, "16 samples");
	ShadowRenderTarget(&shadow, 17);
	Check(shadow.targetSamples == 0, "beyond 16 is not a sample count");
}

void TestSampleTables() {
	SamplePosition p[8];
	Check(StandardSamplePositions(2, p) && Near(p[0].x, 0.75f) && Near(p[0].y, 0.75f) && Near(p[1].x, 0.25f), "2 samples");
	Check(StandardSamplePositions(4, p) && Near(p[0].x, 0.375f) && Near(p[0].y, 0.125f) && Near(p[3].x, 0.625f) &&
	          Near(p[3].y, 0.875f),
	      "4 samples");
	Check(StandardSamplePositions(8, p) && Near(p[0].x, 0.5625f) && Near(p[0].y, 0.3125f) && Near(p[7].x, 0.9375f) &&
	          Near(p[7].y, 0.0625f),
	      "8 samples");
	Check(!StandardSamplePositions(1, p) && !StandardSamplePositions(16, p) && !StandardSamplePositions(3, p),
	      "other counts refused");
	// Every table's points average to the pixel center, as the spec's do.
	for (uint32_t count : {2u, 4u, 8u}) {
		StandardSamplePositions(count, p);
		float sx = 0, sy = 0;
		for (uint32_t i = 0; i < count; ++i) {
			sx += p[i].x;
			sy += p[i].y;
		}
		Check(Near(sx / count, 0.5f) && Near(sy / count, 0.5f), "table centred");
	}
}

void CheckPlan(uint32_t samples, int requested, int expectedPasses, const char* what) {
	SupersamplePass passes[kMaxPasses];
	const int n = PlanPasses(samples, requested, passes);
	Check(n == expectedPasses, what);
	if (n < 2) {
		return;
	}
	// The masks are disjoint runs that together cover every sample, and the
	// evaluation points are the standard ones for that many passes.
	uint32_t all = 0;
	SamplePosition p[8];
	StandardSamplePositions(static_cast<uint32_t>(n), p);
	for (int i = 0; i < n; ++i) {
		Check((all & passes[i].mask) == 0, "masks disjoint");
		all |= passes[i].mask;
		Check(Near(passes[i].offsetX, p[i].x - 0.5f) && Near(passes[i].offsetY, p[i].y - 0.5f), "pass at its standard point");
	}
	Check(all == (samples == 32 ? 0xFFFFFFFFu : ((1u << samples) - 1u)), "masks cover all samples");
}

void TestPlanPasses() {
	CheckPlan(8, 8, 8, "8 of 8");
	CheckPlan(8, 4, 4, "4 of 8");
	CheckPlan(8, 2, 2, "2 of 8");
	CheckPlan(8, 1, 1, "1 of 8 draws plainly");
	CheckPlan(8, 0, 1, "0 draws plainly");
	CheckPlan(8, 6, 4, "6 rounds down to 4");
	CheckPlan(8, 3, 2, "3 rounds down to 2");
	CheckPlan(8, 9, 8, "more than 8 capped");
	CheckPlan(4, 8, 4, "8 on a 4-sample target is 4");
	CheckPlan(4, 2, 2, "2 of 4");
	CheckPlan(2, 8, 2, "2-sample target");
	CheckPlan(16, 8, 8, "16 samples: 8 passes of two");
	CheckPlan(16, 16, 8, "16 passes capped at 8");
	CheckPlan(0, 8, 1, "no samples");
	CheckPlan(1, 8, 1, "NONMASKABLE");
	CheckPlan(6, 8, 1, "an odd count is not planned");
	SupersamplePass passes[kMaxPasses];
	PlanPasses(8, 4, passes);
	Check(passes[0].mask == 0x03 && passes[1].mask == 0x0C && passes[2].mask == 0x30 && passes[3].mask == 0xC0,
	      "4 of 8: pairs of samples");
	PlanPasses(8, 8, passes);
	Check(passes[0].mask == 0x01 && passes[7].mask == 0x80, "8 of 8: one sample each");
	PlanPasses(16, 8, passes);
	Check(passes[0].mask == 0x0003 && passes[7].mask == 0xC000, "8 of 16: pairs");
}

void TestJitter() {
	// Identity clip rows: c0 = x, c1 = y, c2 = z, c3 = w.
	const float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	float out[8];
	JitterClipRows(rows, 0.0f, 0.0f, 100, 50, out);
	Check(out[0] == 1 && out[3] == 0 && out[5] == 1 && out[7] == 0, "no offset, no change");
	// A sample half a pixel right and a quarter up (y down): content there
	// has to come to the center, i.e. move left by half a pixel (NDC
	// -0.5 * 2/100) and down by a quarter (NDC -0.25 * 2/50).
	JitterClipRows(rows, 0.5f, -0.25f, 100, 50, out);
	Check(Near(out[3], -0.01f) && Near(out[0], 1.0f), "x shift on the w column of c0");
	Check(Near(out[7], -0.01f) && Near(out[5], 1.0f), "y shift on the w column of c1, flipped from window to NDC");
	// A general c3 spreads the shift over all four columns.
	const float general[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 1, 1, 1, 1};
	JitterClipRows(general, -1.0f, 1.0f, 2, 2, out);  // shiftX = +1, shiftY = +1
	Check(out[0] == 2 && out[1] == 3 && out[2] == 4 && out[3] == 5, "c0 + 1 * c3");
	Check(out[4] == 6 && out[5] == 7 && out[6] == 8 && out[7] == 9, "c1 + 1 * c3");
}

DeviceShadow ReadyShadow() {
	DeviceShadow shadow;
	shadow.pixelShader = LeafShader::Leaf2000;
	shadow.vertexShader = LeafVertexShader::Vs002;
	shadow.alphaTestEnable = 1;
	shadow.targetSamples = 8;
	shadow.viewportWidth = 4028;
	shadow.viewportHeight = 3380;
	return shadow;
}

void TestPlanLeafDraw() {
	LeafDrawPlan plan;
	DeviceShadow shadow = ReadyShadow();
	Check(PlanLeafDraw(shadow, 8, &plan) && plan.passes == 8 && LeafDrawRefusal(shadow, 8)[0] == '\0', "all in place: 8 passes");
	Check(PlanLeafDraw(shadow, 4, &plan) && plan.passes == 4, "4 asked");

	shadow = ReadyShadow();
	shadow.pixelShader = LeafShader::None;
	Check(!PlanLeafDraw(shadow, 8, &plan) && std::strcmp(LeafDrawRefusal(shadow, 8), "no leaf pixel shader bound") == 0,
	      "no leaf pixel shader");
	shadow = ReadyShadow();
	shadow.vertexShader = LeafVertexShader::None;
	Check(!PlanLeafDraw(shadow, 8, &plan) && std::strstr(LeafDrawRefusal(shadow, 8), "vertex shader the plugin does not know") != nullptr,
	      "unknown vertex shader");
	shadow = ReadyShadow();
	shadow.alphaTestEnable = 0;
	Check(!PlanLeafDraw(shadow, 8, &plan) && std::strcmp(LeafDrawRefusal(shadow, 8), "the alpha test is off") == 0,
	      "alpha test off");
	shadow = ReadyShadow();
	shadow.targetSamples = 0;
	Check(!PlanLeafDraw(shadow, 8, &plan) && std::strstr(LeafDrawRefusal(shadow, 8), "not multisampled") != nullptr,
	      "single-sample target");
	shadow = ReadyShadow();
	shadow.viewportWidth = 0;
	Check(!PlanLeafDraw(shadow, 8, &plan) && std::strcmp(LeafDrawRefusal(shadow, 8), "no viewport") == 0, "no viewport");
	shadow = ReadyShadow();
	Check(!PlanLeafDraw(shadow, 1, &plan) && std::strstr(LeafDrawRefusal(shadow, 1), "fewer than two passes") != nullptr,
	      "one pass asked");
	shadow = ReadyShadow();
	shadow.targetSamples = 6;
	Check(!PlanLeafDraw(shadow, 8, &plan), "odd sample count");
}

void TestCoverageSuspend() {
	StateWrite writes[2];
	Check(SuspendCoverageWrites(Hack::NvidiaAtoc, writes) == 1 && writes[0].state == kRsAdaptiveTessY && writes[0].value == 0,
	      "NVIDIA: ATOC off");
	Check(ResumeCoverageWrites(Hack::NvidiaAtoc, 1, writes) == 1 && writes[0].state == kRsAdaptiveTessY &&
	          writes[0].value == kFourCCAtoc,
	      "NVIDIA: ATOC back");
	Check(SuspendCoverageWrites(Hack::AmdA2M, writes) == 1 && writes[0].state == kRsPointSize && writes[0].value == kFourCCA2M0,
	      "AMD: A2M0");
	Check(ResumeCoverageWrites(Hack::AmdA2M, 1, writes) == 1 && writes[0].value == kFourCCA2M1, "AMD: A2M1 back with the test on");
	Check(ResumeCoverageWrites(Hack::AmdA2M, 0, writes) == 1 && writes[0].value == kFourCCA2M0, "AMD: stays A2M0 with the test off");
	Check(SuspendCoverageWrites(Hack::None, writes) == 0 && ResumeCoverageWrites(Hack::None, 1, writes) == 0, "none: nothing");

	LeafMethod method = LeafMethod::Coverage;
	Check(ParseLeafMethod("Supersample", &method) && method == LeafMethod::Supersample, "parse supersample");
	Check(ParseLeafMethod("coverage", &method) && method == LeafMethod::Coverage, "parse coverage");
	Check(!ParseLeafMethod("ssaa", &method) && method == LeafMethod::Coverage, "unknown refused, untouched");
	Check(!ParseLeafMethod(nullptr, &method), "null refused");
	Check(std::strcmp(LeafMethodName(LeafMethod::Supersample), "supersample") == 0 &&
	          std::strcmp(LeafMethodName(LeafMethod::Coverage), "coverage") == 0,
	      "method names");
}

}  // namespace

int main() {
	TestShadow();
	TestSampleTables();
	TestPlanPasses();
	TestJitter();
	TestPlanLeafDraw();
	TestCoverageSuspend();
	std::printf(g_failures == 0 ? "SupersampleTest: all passed\n" : "SupersampleTest: %d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
