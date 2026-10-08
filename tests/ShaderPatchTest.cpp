// Every flow of ShaderPatch without a device: identification, the sharpened
// copy's token stream, the constants and their clamps, the refusals, and the
// cache.

#include "LeafShaders.h"
#include "ShaderPatch.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what);
	}
}

using namespace foliageaa;

uint32_t Token(const std::vector<uint8_t>& bytes, size_t index) {
	uint32_t token = 0;
	std::memcpy(&token, bytes.data() + index * 4, 4);
	return token;
}

void TestIdentify() {
	Check(IdentifyLeafShader(kLeaf2000, sizeof(kLeaf2000)) == LeafShader::Leaf2000, "STLEAF2000 identified");
	Check(IdentifyLeafShader(kLeaf2001, sizeof(kLeaf2001)) == LeafShader::Leaf2001, "STLEAF2001 identified");
	Check(IdentifyLeafShader(kLeaf2000, sizeof(kLeaf2000) - 4) == LeafShader::None, "truncated is none");
	std::vector<uint8_t> altered(kLeaf2000, kLeaf2000 + sizeof(kLeaf2000));
	altered[sizeof(kLeaf2000) - 8] ^= 1;
	Check(IdentifyLeafShader(altered.data(), altered.size()) == LeafShader::None, "one bit off is none");
	Check(IdentifyLeafShader(nullptr, 0) == LeafShader::None, "null is none");
	Check(std::strcmp(LeafShaderName(LeafShader::Leaf2000), "STLEAF2000") == 0 &&
	          std::strcmp(LeafShaderName(LeafShader::Leaf2001), "STLEAF2001") == 0 &&
	          std::strcmp(LeafShaderName(LeafShader::None), "none") == 0,
	      "names");
}

void TestConstants() {
	Check(SharpenScale(4.0f) == 4.0f, "scale is the steepness");
	Check(SharpenScale(0.0f) == 1.0f && SharpenScale(-3.0f) == 1.0f, "steepness floor 1");
	Check(SharpenScale(1000.0f) == 64.0f, "steepness ceiling 64");
	Check(SharpenOffset(0.5f, 4.0f) == 0.5f - 2.0f, "offset 0.5 - t*k");
	Check(SharpenOffset(-1.0f, 4.0f) == 0.5f, "threshold floor 0");
	Check(SharpenOffset(2.0f, 4.0f) == 0.5f - 4.0f, "threshold ceiling 1");
	float values[4] = {9, 9, 9, 9};
	SharpenConstants(0.25f, 8.0f, values);
	Check(values[0] == 8.0f && values[1] == 0.5f - 2.0f && values[2] == 0.0f && values[3] == 0.0f, "register values");
	Check(ThresholdFromAlphaRef(0) == 0.0f, "alpha ref 0");
	Check(ThresholdFromAlphaRef(255) == 1.0f, "alpha ref 255");
	Check(ThresholdFromAlphaRef(84) > 0.329f && ThresholdFromAlphaRef(84) < 0.330f, "alpha ref 84 is 0.329");
	Check(ThresholdFromAlphaRef(1000) == 1.0f, "alpha ref beyond 255 clamps");
	// The mapping itself: threshold to 0.5, a quarter above to 1, a quarter below to 0.
	auto map = [](float a, float t, float k) {
		const float v = a * SharpenScale(k) + SharpenOffset(t, k);
		return v < 0 ? 0.0f : (v > 1 ? 1.0f : v);
	};
	Check(map(0.5f, 0.5f, 4.0f) == 0.5f, "threshold maps to half");
	Check(map(0.75f, 0.5f, 4.0f) == 1.0f, "interior maps to one");
	Check(map(0.25f, 0.5f, 4.0f) == 0.0f, "gap maps to zero");
	Check(map(0.3f, 0.3f, 8.0f) == 0.5f, "other threshold");
}

void TestBuild(const uint8_t* original, size_t length, const char* name) {
	std::vector<uint8_t> out;
	Check(BuildSharpenedShader(original, length, &out), name);
	Check(out.size() == length + 20, "copy is 5 tokens longer (the mad)");
	Check(Token(out, 0) == 0xFFFF0201, "ps_2_x kept");
	// The original's tokens up to and including the texld are copied
	// verbatim, then the mad, then the rest.
	size_t texEnd = 0;
	for (size_t i = 1; i * 4 < length;) {
		uint32_t token = 0;
		std::memcpy(&token, original + i * 4, 4);
		const size_t next = i + 1 + (((token & 0xFFFF) == 0xFFFE) ? ((token >> 16) & 0x7FFF) : ((token >> 24) & 0xF));
		if ((token & 0xFFFF) == 0x42) {
			texEnd = next;
			break;
		}
		i = next;
	}
	Check(texEnd != 0, "original has a texld");
	Check(std::memcmp(out.data(), original, texEnd * 4) == 0, "head copied verbatim");
	const size_t madAt = texEnd;
	Check(Token(out, madAt) == 0x04000004, "mad after the texld");
	Check(Token(out, madAt + 1) == 0x80380000, "mad writes r0.w with _sat _pp");
	Check(Token(out, madAt + 2) == 0x80FF0000, "mad reads r0.wwww");
	Check(Token(out, madAt + 3) == (0x80000000u | (2u << 28) | 31u), "mad reads c31.xxxx");
	Check(Token(out, madAt + 4) == (0x80000000u | (2u << 28) | (0x55u << 16) | 31u), "mad reads c31.yyyy");
	Check(std::memcmp(out.data() + (madAt + 5) * 4, original + texEnd * 4, length - texEnd * 4) == 0, "tail copied verbatim");
	Check(Token(out, out.size() / 4 - 1) == 0x0000FFFF, "end token last");
	// No def anywhere: the register is the plugin's to write.
	bool def = false;
	for (size_t i = 0; i < out.size() / 4; ++i) {
		def = def || Token(out, i) == 0x05000051;
	}
	Check(!def, "no def in the copy");
}

void TestBuildRefusals() {
	std::vector<uint8_t> out;
	Check(!BuildSharpenedShader(nullptr, 0, &out), "null refused");
	Check(!BuildSharpenedShader(kLeaf2000, 7, &out), "unaligned refused");
	std::vector<uint8_t> vs(kLeaf2000, kLeaf2000 + sizeof(kLeaf2000));
	vs[3] = 0xFE;  // vs_2_1
	Check(!BuildSharpenedShader(vs.data(), vs.size(), &out), "vertex shader refused");
	// A ps_2_x with no texld: version, one mov, end.
	const uint32_t noTex[] = {0xFFFF0201, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF};
	Check(!BuildSharpenedShader(reinterpret_cast<const uint8_t*>(noTex), sizeof(noTex), &out), "no texld refused");
	// A texld but no end token.
	const uint32_t noEnd[] = {0xFFFF0201, 0x03000042, 0x800F0000, 0x80E40000, 0xA0E40800};
	Check(!BuildSharpenedShader(reinterpret_cast<const uint8_t*>(noEnd), sizeof(noEnd), &out), "no end token refused");
	// A comment whose payload contains a texld-looking token must be skipped.
	const uint32_t commentTex[] = {0xFFFF0201, 0x0001FFFE, 0x03000042, 0x0000FFFF};
	Check(!BuildSharpenedShader(reinterpret_cast<const uint8_t*>(commentTex), sizeof(commentTex), &out),
	      "texld inside a comment does not count");
}

void TestCache() {
	ShaderCache cache;
	int a = 0, b = 0, r = 0;
	ShaderEntry entry;
	entry.kind = 7;
	entry.replacement = &r;
	Check(!cache.Lookup(&a, &entry) && entry.kind == 7 && entry.replacement == &r, "miss leaves the out untouched");
	cache.Remember(&a, ShaderEntry{1, &r});
	cache.Remember(&b, ShaderEntry{2, nullptr});
	Check(cache.Size() == 2, "two entries");
	Check(cache.Lookup(&a, &entry) && entry.kind == 1 && entry.replacement == &r, "hit with kind and replacement");
	Check(cache.Lookup(&b, &entry) && entry.kind == 2 && entry.replacement == nullptr, "hit with kind and pass-through");
	cache.Forget(&a);
	Check(!cache.Lookup(&a, &entry) && cache.Size() == 1, "forgotten");
	cache.Forget(&a);
	Check(cache.Size() == 1, "forgetting twice is harmless");
	cache.Remember(&b, ShaderEntry{3, &r});
	Check(cache.Lookup(&b, &entry) && entry.kind == 3 && entry.replacement == &r, "remember overwrites");
}

void TestIdentifyVertexShaders() {
	struct Known {
		const uint8_t* bytes;
		size_t length;
		LeafVertexShader kind;
	};
	const Known known[] = {
		{kLeafVs000_1734322A, sizeof(kLeafVs000_1734322A), LeafVertexShader::Vs000},
		{kLeafVs000_0C737235, sizeof(kLeafVs000_0C737235), LeafVertexShader::Vs000},
		{kLeafVs001_68B68346, sizeof(kLeafVs001_68B68346), LeafVertexShader::Vs001},
		{kLeafVs001_5E91704D, sizeof(kLeafVs001_5E91704D), LeafVertexShader::Vs001},
		{kLeafVs002_F39C1C4D, sizeof(kLeafVs002_F39C1C4D), LeafVertexShader::Vs002},
		{kLeafVs002_3CC9900E, sizeof(kLeafVs002_3CC9900E), LeafVertexShader::Vs002},
		{kLeafVs003_A87E9827, sizeof(kLeafVs003_A87E9827), LeafVertexShader::Vs003},
		{kLeafVs003_14152999, sizeof(kLeafVs003_14152999), LeafVertexShader::Vs003},
	};
	for (const Known& k : known) {
		Check(IdentifyLeafVertexShader(k.bytes, k.length) == k.kind, "vertex shader variant identified");
		Check(IdentifyLeafVertexShader(k.bytes, k.length - 4) == LeafVertexShader::None, "truncated variant is none");
		std::vector<uint8_t> altered(k.bytes, k.bytes + k.length);
		altered[k.length - 8] ^= 1;
		Check(IdentifyLeafVertexShader(altered.data(), altered.size()) == LeafVertexShader::None, "altered variant is none");
		// Every variant is a vs_1_1 shader (0xFFFE0101).
		Check(k.bytes[0] == 0x01 && k.bytes[1] == 0x01 && k.bytes[2] == 0xFE && k.bytes[3] == 0xFF, "vs_1_1 version token");
	}
	Check(IdentifyLeafVertexShader(kLeaf2000, sizeof(kLeaf2000)) == LeafVertexShader::None, "a pixel shader is no vertex shader");
	Check(IdentifyLeafVertexShader(nullptr, 0) == LeafVertexShader::None, "null is none");
	Check(std::strcmp(LeafVertexShaderName(LeafVertexShader::Vs000), "STLEAF000") == 0 &&
	          std::strcmp(LeafVertexShaderName(LeafVertexShader::Vs003), "STLEAF003") == 0 &&
	          std::strcmp(LeafVertexShaderName(LeafVertexShader::None), "none") == 0,
	      "vertex shader names");
}

}  // namespace

int main() {
	TestIdentify();
	TestConstants();
	TestBuild(kLeaf2000, sizeof(kLeaf2000), "STLEAF2000 builds");
	TestBuild(kLeaf2001, sizeof(kLeaf2001), "STLEAF2001 builds");
	TestBuildRefusals();
	TestCache();
	TestIdentifyVertexShaders();
	std::printf(g_failures == 0 ? "ShaderPatchTest: all passed\n" : "ShaderPatchTest: %d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
