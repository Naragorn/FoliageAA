#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Why coverage alone leaves the leaves see-through, and what this does about
// it. Alpha-to-coverage turns the shader's alpha into the share of covered
// samples, one to one. Oblivion's leaf pixel shaders hand the texture alpha
// through untouched (LeafShaders.h), and at a distance that alpha is never 1:
// the mipmaps average leaf and gap, so a cluster that is 1 and 0 up close is
// 0.3 to 0.6 everywhere two mip levels out. The old alpha test cut that at a
// threshold - all or nothing, hence the shimmer; coverage renders it as a
// half-transparent cloud. NVIDIA's "Antialiasing with Transparency" paper
// asks for exactly the cure modern engines apply: scale the alpha in the
// shader so it falls from the threshold to 0 only at the edge.
//
// So the plugin hands the engine a copy of each leaf shader with one
// instruction added right after the texture read:
//
//   mad_sat_pp r0.w, r0.w, c31.x, c31.y     ; a' = sat((a - threshold) * steepness + 0.5)
//
// with c31 = (steepness, 0.5 - threshold * steepness, 0, 0) written by the
// plugin whenever it sets the copy and whenever the engine changes its alpha
// reference - because the threshold the engine tests against is not fixed:
// Oblivion's leaves were seen with D3DRS_ALPHAREF 84 (0.33), and following
// that register keeps the coverage cut where the test's cut was. Written as a
// register rather than a "def" in the shader, so no runtime's rules about
// when "def" values reload can get between the plugin and the value. c31 is
// free: neither leaf shader declares a constant.
//
// Leaf interiors go back to 1, gaps to 0, and a band of width 1/steepness
// around the threshold stays soft for the coverage to antialias.
//
// The copy is substituted when the engine sets the shader (Setup.cpp hooks
// SetPixelShader), so it does not matter that the engine created its shaders
// long before the plugin had a device.

namespace foliageaa {

enum class LeafShader { None, Leaf2000, Leaf2001 };

// Which vanilla leaf shader these bytes are, byte for byte; None for anything
// else, including other sizes, so a modified package (Oblivion Reloaded and
// the like ship their own leaf shaders) is left alone.
LeafShader IdentifyLeafShader(const uint8_t* bytes, size_t length);
const char* LeafShaderName(LeafShader shader);

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

// Which replacement, if any, stands for a shader object the engine set.
// Keyed by object address; a CreatePixelShader at a known address means the
// old object is gone and the entry must be forgotten.
class ShaderCache {
public:
	// True if the shader was seen before; *replacement is then the object
	// to set instead, or null to set the original.
	bool Lookup(void* shader, void** replacement) const;
	void Remember(void* shader, void* replacement);
	void Forget(void* shader);
	size_t Size() const { return m_entries.size(); }

private:
	std::unordered_map<void*, void*> m_entries;
};

}  // namespace foliageaa
