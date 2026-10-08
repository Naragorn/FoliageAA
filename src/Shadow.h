#pragma once

#include "ShaderPatch.h"

#include <cstdint>

// The device state the plugin needs at a draw, kept from the engine's own
// Set* calls rather than asked back from the device.
//
// Oblivion creates its device with behavior flags 0x54, which include
// D3DCREATE_PUREDEVICE (OBVR's log, "device behavior flags 00000054"). On a
// pure device the Get* state calls are not supported by Direct3D - DXVK
// answers them anyway, Microsoft's d3d9.dll does not. So everything the plugin
// decides on at draw time is shadowed here as the engine writes it, and the
// decisions become pure functions over this struct.

namespace foliageaa {

struct DeviceShadow {
	uint32_t alphaTestEnable = 0;           // D3DRS_ALPHATESTENABLE
	uint32_t alphaRef = 0;                  // D3DRS_ALPHAREF, 0..255
	uint32_t multiSampleMask = 0xFFFFFFFF;  // D3DRS_MULTISAMPLEMASK
	// Render target 0's sample count: 2..16, or 0 when the target is not
	// multisampled or is NONMASKABLE, whose samples cannot be masked.
	uint32_t targetSamples = 0;
	uint32_t viewportWidth = 0;
	uint32_t viewportHeight = 0;
	// The vertex constants c0..c3 as the engine last wrote them: the rows of
	// the clip-space matrix for every leaf vertex shader.
	float clipRows[16] = {};
	LeafShader pixelShader = LeafShader::None;
	LeafVertexShader vertexShader = LeafVertexShader::None;
};

// d3d9types.h render state numbers the shadow follows.
constexpr uint32_t kRsAlphaRef = 24;
constexpr uint32_t kRsMultiSampleMask = 162;

void ShadowRenderState(DeviceShadow* shadow, uint32_t state, uint32_t value);
// SetVertexShaderConstantF's arguments: whichever of the registers 0..3 the
// write covers are taken over.
void ShadowVertexConstants(DeviceShadow* shadow, uint32_t startRegister, const float* data, uint32_t count);
void ShadowViewport(DeviceShadow* shadow, uint32_t width, uint32_t height);
// From the new render target 0's D3DSURFACE_DESC::MultiSampleType.
void ShadowRenderTarget(DeviceShadow* shadow, uint32_t multiSampleType);

}  // namespace foliageaa
