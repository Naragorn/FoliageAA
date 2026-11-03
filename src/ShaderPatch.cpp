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

bool Same(const uint8_t* bytes, size_t length, const uint8_t* known, size_t knownLength) {
	return length == knownLength && std::memcmp(bytes, known, length) == 0;
}

}  // namespace

LeafShader IdentifyLeafShader(const uint8_t* bytes, size_t length) {
	if (bytes == nullptr) {
		return LeafShader::None;
	}
	if (Same(bytes, length, kLeaf2000, sizeof(kLeaf2000))) {
		return LeafShader::Leaf2000;
	}
	if (Same(bytes, length, kLeaf2001, sizeof(kLeaf2001))) {
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

LeafVertexShader IdentifyLeafVertexShader(const uint8_t* bytes, size_t length) {
	if (bytes == nullptr) {
		return LeafVertexShader::None;
	}
	if (Same(bytes, length, kLeafVs000_1734322A, sizeof(kLeafVs000_1734322A)) ||
	    Same(bytes, length, kLeafVs000_0C737235, sizeof(kLeafVs000_0C737235))) {
		return LeafVertexShader::Vs000;
	}
	if (Same(bytes, length, kLeafVs001_68B68346, sizeof(kLeafVs001_68B68346)) ||
	    Same(bytes, length, kLeafVs001_5E91704D, sizeof(kLeafVs001_5E91704D))) {
		return LeafVertexShader::Vs001;
	}
	if (Same(bytes, length, kLeafVs002_F39C1C4D, sizeof(kLeafVs002_F39C1C4D)) ||
	    Same(bytes, length, kLeafVs002_3CC9900E, sizeof(kLeafVs002_3CC9900E))) {
		return LeafVertexShader::Vs002;
	}
	if (Same(bytes, length, kLeafVs003_A87E9827, sizeof(kLeafVs003_A87E9827)) ||
	    Same(bytes, length, kLeafVs003_14152999, sizeof(kLeafVs003_14152999))) {
		return LeafVertexShader::Vs003;
	}
	return LeafVertexShader::None;
}

int ShaderConstantRegister(const uint8_t* bytes, size_t length, const char* name) {
	if (bytes == nullptr || name == nullptr || length < 8 || (length % 4) != 0) {
		return -1;
	}
	const auto* code = reinterpret_cast<const uint32_t*>(bytes);
	const uint32_t count = static_cast<uint32_t>(length / 4);
	constexpr uint32_t kCtabFourCC = 0x42415443u;  // "CTAB"
	uint32_t at = 1;
	while (at < count) {
		const uint32_t token = code[at];
		if ((token & kOpcodeMask) != kOpcodeComment) {
			return -1;  // the table comes before the first instruction
		}
		const uint32_t tokens = TokensAfter(token);
		if (at + 1 + tokens > count) {
			return -1;
		}
		if (tokens >= 8 && code[at + 1] == kCtabFourCC) {
			const uint8_t* table = reinterpret_cast<const uint8_t*>(code + at + 2);
			const uint32_t tableBytes = (tokens - 1) * 4;
			const auto dword = [&](uint32_t offset) -> uint32_t {
				uint32_t value = 0;
				if (offset + 4 <= tableBytes) {
					std::memcpy(&value, table + offset, 4);
				}
				return value;
			};
			const auto word = [&](uint32_t offset) -> uint16_t {
				uint16_t value = 0;
				if (offset + 2 <= tableBytes) {
					std::memcpy(&value, table + offset, 2);
				}
				return value;
			};
			// D3DXSHADER_CONSTANTTABLE: Size, Creator, Version, Constants,
			// ConstantInfo, Flags, Target; each D3DXSHADER_CONSTANTINFO is
			// Name, RegisterSet, RegisterIndex, RegisterCount, Reserved,
			// DefaultValue - 20 bytes, the name an offset into the table.
			const uint32_t constants = dword(12);
			const uint32_t info = dword(16);
			for (uint32_t i = 0; i < constants && info + i * 20 + 20 <= tableBytes; ++i) {
				const uint32_t entry = info + i * 20;
				const uint32_t nameAt = dword(entry);
				const uint16_t set = word(entry + 4);
				const uint16_t index = word(entry + 6);
				uint32_t k = 0;
				while (nameAt + k < tableBytes && name[k] != '\0' && table[nameAt + k] == static_cast<uint8_t>(name[k])) {
					++k;
				}
				if (name[k] == '\0' && nameAt + k < tableBytes && table[nameAt + k] == 0 && set == 2) {
					return index;  // D3DXRS_FLOAT4
				}
			}
			return -1;
		}
		at += 1 + tokens;
	}
	return -1;
}

GrassVertexShaderInfo IdentifyGrassVertexShader(const uint8_t* bytes, size_t length) {
	GrassVertexShaderInfo info;
	if (bytes == nullptr || length < 8 || (length % 4) != 0) {
		return info;
	}
	uint32_t version = 0;
	std::memcpy(&version, bytes, 4);
	// A vertex shader of model 2 or later: 0xFFFE0200 and up.
	if ((version & 0xFFFF0000u) != 0xFFFE0000u || ((version >> 8) & 0xFFu) < 2) {
		return info;
	}
	const int modelViewProj = ShaderConstantRegister(bytes, length, "ModelViewProj");
	if (modelViewProj < 0 || ShaderConstantRegister(bytes, length, "InstanceData") < 0 ||
	    ShaderConstantRegister(bytes, length, "WindData") < 0) {
		return info;
	}
	info.grass = true;
	info.modelViewProj = modelViewProj;
	return info;
}

const char* LeafVertexShaderName(LeafVertexShader shader) {
	switch (shader) {
		case LeafVertexShader::Vs000: return "STLEAF000";
		case LeafVertexShader::Vs001: return "STLEAF001";
		case LeafVertexShader::Vs002: return "STLEAF002";
		case LeafVertexShader::Vs003: return "STLEAF003";
		case LeafVertexShader::None: return "none";
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

bool ShaderCache::Lookup(void* shader, ShaderEntry* out) const {
	const auto found = m_entries.find(shader);
	if (found == m_entries.end()) {
		return false;
	}
	*out = found->second;
	return true;
}

void ShaderCache::Remember(void* shader, const ShaderEntry& entry) {
	m_entries[shader] = entry;
}

void ShaderCache::Forget(void* shader) {
	m_entries.erase(shader);
}

}  // namespace foliageaa
