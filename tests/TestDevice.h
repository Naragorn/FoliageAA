#pragma once

#include <d3d9.h>

// A real Direct3D 9 device on this machine for the device tests: a hidden
// window, the HAL device of adapter 0, and the highest multisample count the
// adapter accepts out of 8, 4, 2, none.

struct TestDevice {
	IDirect3D9* d3d = nullptr;
	IDirect3DDevice9* device = nullptr;
	HWND window = nullptr;
	D3DMULTISAMPLE_TYPE multisample = D3DMULTISAMPLE_NONE;
	D3DADAPTER_IDENTIFIER9 identifier{};

	bool Create();
	void Destroy();
};
