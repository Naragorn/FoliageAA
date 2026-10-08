#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Knowing the engine's leaf shaders by their bytes, and - for the coverage
// method - a sharpened copy of the leaf pixel shaders.
//
// Why coverage alone leaves the leaves see-through: alpha-to-coverage turns
// the shader's alpha into the share of covered samples, one to one.
// Oblivion's leaf pixel shaders hand the texture alpha through untouched
// (LeafShaders.h), and at a distance that alpha is never 1: the mipmaps
// average leaf and gap, so a cluster that is 1 and 0 up close is 0.3 to 0.6
// everywhere two mip levels out. The old alpha test cut that at a threshold
// - all or nothing, hence the shimmer; coverage renders it as a
// half-transparent cloud. NVIDIA's "Antialiasing with Transparency" paper
// asks for exactly the cure modern engines apply: scale the alpha in the
// shader so it falls from the threshold to 0 only at the edge.
//
// So the coverage method hands the engine a copy of each leaf pixel shader
// with one instruction added right after the texture read:
//
//   mad_sat_pp r0.w, r0.w, c31.x, c31.y     ; a' = sat((a - threshold) * steepness + 0.5)
//
// with c31 = (steepness, 0.5 - threshold * steepness, 0, 0) written by the
// plugin whenever it sets the copy and whenever the engine changes its alpha
// reference - because the threshold the engine tests against is not fixed:
// Oblivion's leaves were seen with D3DRS_ALPHAREF from 84 to 159 (0.33 to
// 0.62), moving with distance. Written as a register rather than a "def" in
// the shader, so no runtime's rules about when "def" values reload can get
// between the plugin and the value. c31 is free: neither leaf shader
// declares a constant.
//
// The copy is substituted when the engine sets the shader (Setup.cpp hooks
// SetPixelShader), so it does not matter that the engine created its shaders
// long before the plugin had a device.
//
// The supersampling method (Supersample.h) needs no copy; it only needs to
// know, at each draw, that the engine's leaf pixel shader and one of its leaf
// vertex shaders are bound.

namespace foliageaa {

enum class LeafShader { None, Leaf2000, Leaf2001 };

// Which vanilla leaf pixel shader these bytes are, byte for byte; None for
// anything else, including other sizes, so a modified package (Oblivion
// Reloaded and the like ship their own leaf shaders) is left alone.
LeafShader IdentifyLeafShader(const uint8_t* bytes, size_t length);
const char* LeafShaderName(LeafShader shader);

// The vanilla leaf vertex shaders, in either of their two builds (packages
// 001-009 and 010-019 differ). All of them project through the constants
// c0..c3, which is what the supersampling passes rely on.
enum class LeafVertexShader { None, Vs000, Vs001, Vs002, Vs003 };
LeafVertexShader IdentifyLeafVertexShader(const uint8_t* bytes, size_t length);
const char* LeafVertexShaderName(LeafVertexShader shader);

// Builds the sharpened copy. False if the bytes are not a ps_2_x shader with
// a texld to patch after (never the case for the two vanilla shaders, but
// the walker refuses rather than guesses).
bool BuildSharpenedShader(const uint8_t* original, size_t length, std::vector<uint8_t>* out);

// The constant register the copy reads, and its four values for a threshold
// (clamped to [0, 1]) and steepness (clamped to [1, 64]).
constexpr uint32_t kSharpenConstantRegister = 31;
float SharpenScale(float steepness);
float SharpenOffset(float threshold, float steepness);
void SharpenConstants(float threshold, float steepness, float out[4]);

// D3DRS_ALPHAREF as a threshold: the engine's 0..255 reference on the alpha's
// 0..1 scale.
float ThresholdFromAlphaRef(uint32_t alphaRef);

// What the plugin learnt about a shader object the engine set: which leaf
// shader it is (an enum value, 0 for none) and, for a leaf pixel shader under
// the coverage method, the sharpened copy to set instead of it. Keyed by
// object address; a Create*Shader at a known address means the old object is
// gone and the entry must be forgotten.
struct ShaderEntry {
	int kind = 0;
	void* replacement = nullptr;
};

class ShaderCache {
public:
	// True if the shader was seen before; *out is then what was learnt.
	bool Lookup(void* shader, ShaderEntry* out) const;
	void Remember(void* shader, const ShaderEntry& entry);
	void Forget(void* shader);
	size_t Size() const { return m_entries.size(); }

private:
	std::unordered_map<void*, ShaderEntry> m_entries;
};

}  // namespace foliageaa
