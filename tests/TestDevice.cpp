#include "TestDevice.h"

#include <cstdio>

bool TestDevice::Create() {
	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (d3d == nullptr) {
		std::printf("Direct3DCreate9 failed\n");
		return false;
	}
	d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &identifier);

	WNDCLASSA wc{};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "FoliageAATestWindow";
	RegisterClassA(&wc);
	window = CreateWindowA(wc.lpszClassName, "FoliageAA test", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
	                       wc.hInstance, nullptr);
	if (window == nullptr) {
		std::printf("CreateWindow failed\n");
		return false;
	}

	const D3DMULTISAMPLE_TYPE candidates[] = {D3DMULTISAMPLE_8_SAMPLES, D3DMULTISAMPLE_4_SAMPLES,
	                                          D3DMULTISAMPLE_2_SAMPLES, D3DMULTISAMPLE_NONE};
	for (D3DMULTISAMPLE_TYPE candidate : candidates) {
		if (candidate != D3DMULTISAMPLE_NONE &&
		    FAILED(d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE,
		                                           candidate, nullptr))) {
			continue;
		}
		D3DPRESENT_PARAMETERS pp{};
		pp.Windowed = TRUE;
		pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
		pp.BackBufferFormat = D3DFMT_X8R8G8B8;
		pp.BackBufferWidth = 64;
		pp.BackBufferHeight = 64;
		pp.MultiSampleType = candidate;
		pp.hDeviceWindow = window;
		const HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
		                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
		if (SUCCEEDED(hr) && device != nullptr) {
			multisample = candidate;
			std::printf("device: %s, multisample %d\n", identifier.Description, static_cast<int>(candidate));
			return true;
		}
		std::printf("CreateDevice with multisample %d failed: 0x%08lX\n", static_cast<int>(candidate), hr);
	}
	return false;
}

void TestDevice::Destroy() {
	if (device != nullptr) {
		device->Release();
		device = nullptr;
	}
	if (d3d != nullptr) {
		d3d->Release();
		d3d = nullptr;
	}
	if (window != nullptr) {
		DestroyWindow(window);
		window = nullptr;
	}
}
