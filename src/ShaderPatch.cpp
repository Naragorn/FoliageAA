#include "ShaderPatch.h"

#include "LeafShaders.h"

#include <cstring>

namespace foliageaa {

namespace {

// Direct3D 9 shader token encoding (d3d9types.h, "Shader Token Stream").
constexpr uint32_t kOpcodeMask = 0x0000FFFF;
constexpr uint32_t kOpcodeComment = 0xFFFE;
constexpr uint32_t kEndToken = 0x0000FFFF;
constexpr uint32_t kOpcodeTex = 0x42;   // D3DSIO_TEX, "texld" from ps_1_4 on
constexpr uint32_t kOpcodeMad = 0x04;   // D3DSIO_MAD
constexpr uint32_t kVersionPs2x = 0xFFFF0201;

uint32_t ReadToken(const uint8_t* bytes, size_t index) {
	uint32_t token = 0;
	std::memcpy(&token, bytes + index * 4, 4);
	return token;
}

void AppendToken(std::vector<uint8_t>* out, uint32_t token) {
	uint8_t bytes[4];
	std::memcpy(bytes, &token, 4);
	out->insert(out->end(), bytes, bytes + 4);
}

// Instruction length in tokens after the opcode token: comments carry it in
// bits 16-30, everything else from ps_2_0 on in bits 24-27.
uint32_t TokensAfter(uint32_t opcodeToken) {
	if ((opcodeToken & kOpcodeMask) == kOpcodeComment) {
		return (opcodeToken >> 16) & 0x7FFF;
	}
	return (opcodeToken >> 24) & 0x0F;
}

float Clamp(float value, float low, float high) {
	return value < low ? low : (value > high ? high : value);
}

}  // namespace

LeafShader IdentifyLeafShader(const uint8_t* bytes, size_t length) {
	if (bytes == nullptr) {
		return LeafShader::None;
	}
	if (length == sizeof(kLeaf2000) && std::memcmp(bytes, kLeaf2000, length) == 0) {
		return LeafShader::Leaf2000;
	}
	if (length == sizeof(kLeaf2001) && std::memcmp(bytes, kLeaf2001, length) == 0) {
		return LeafShader::Leaf2001;
	}
	return LeafShader::None;
}

const char* LeafShaderName(LeafShader shader) {
	switch (shader) {
		case LeafShader::Leaf2000: return "STLEAF2000";
		case LeafShader::Leaf2001: return "STLEAF2001";
		case LeafShader::None: return "none";
	}
	return "none";
}

float SharpenScale(float steepness) {
	return Clamp(steepness, 1.0f, 64.0f);
}

float SharpenOffset(float threshold, float steepness) {
	return 0.5f - Clamp(threshold, 0.0f, 1.0f) * SharpenScale(steepness);
}

void SharpenConstants(float threshold, float steepness, float out[4]) {
	out[0] = SharpenScale(steepness);
	out[1] = SharpenOffset(threshold, steepness);
	out[2] = 0.0f;
	out[3] = 0.0f;
}

float ThresholdFromAlphaRef(uint32_t alphaRef) {
	return Clamp(static_cast<float>(alphaRef) / 255.0f, 0.0f, 1.0f);
}

bool BuildSharpenedShader(const uint8_t* original, size_t length, std::vector<uint8_t>* out) {
	if (original == nullptr || length < 8 || length % 4 != 0 || ReadToken(original, 0) != kVersionPs2x) {
		return false;
	}
	const size_t tokenCount = length / 4;
	// Find the first texld and the end token, walking instruction by
	// instruction so a comment's payload is never mistaken for one.
	size_t afterTex = 0;
	size_t index = 1;
	while (index < tokenCount) {
		const uint32_t token = ReadToken(original, index);
		if (token == kEndToken) {
			break;
		}
		const size_t next = index + 1 + TokensAfter(token);
		if (afterTex == 0 && (token & kOpcodeMask) == kOpcodeTex) {
			afterTex = next;
		}
		index = next;
	}
	if (afterTex == 0 || index >= tokenCount) {
		return false;
	}

	out->clear();
	out->reserve(length + 20);
	// Everything up to and including the texld, unchanged.
	out->insert(out->end(), original, original + afterTex * 4);
	// mad_sat_pp r0.w, r0.w, c31.x, c31.y
	AppendToken(out, 0x04000000 | kOpcodeMad);                                              // mad, 4 tokens follow
	AppendToken(out, 0x80000000 | (0x8u << 16) | (1u << 20) | (2u << 20));                  // r0.w, _sat, _pp
	AppendToken(out, 0x80000000 | (0xFFu << 16));                                           // r0.wwww
	AppendToken(out, 0x80000000 | (2u << 28) | (0x00u << 16) | kSharpenConstantRegister);  // c31.xxxx
	AppendToken(out, 0x80000000 | (2u << 28) | (0x55u << 16) | kSharpenConstantRegister);  // c31.yyyy
	// The rest, end token included.
	out->insert(out->end(), original + afterTex * 4, original + length);
	return true;
}

bool ShaderCache::Lookup(void* shader, void** replacement) const {
	const auto found = m_entries.find(shader);
	if (found == m_entries.end()) {
		return false;
	}
	*replacement = found->second;
	return true;
}

void ShaderCache::Remember(void* shader, void* replacement) {
	m_entries[shader] = replacement;
}

void ShaderCache::Forget(void* shader) {
	m_entries.erase(shader);
}

}  // namespace foliageaa
