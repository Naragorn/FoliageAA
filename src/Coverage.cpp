#include "Coverage.h"

namespace foliageaa {

Vendor VendorFromId(uint32_t vendorId) {
	switch (vendorId) {
		case kVendorIdNvidia: return Vendor::Nvidia;
		case kVendorIdAmd: return Vendor::Amd;
		case kVendorIdIntel: return Vendor::Intel;
		default: return Vendor::Other;
	}
}

const char* VendorName(Vendor vendor) {
	switch (vendor) {
		case Vendor::Nvidia: return "NVIDIA";
		case Vendor::Amd: return "AMD";
		case Vendor::Intel: return "Intel";
		case Vendor::Other: return "other";
	}
	return "other";
}

namespace {

bool EqualsIgnoringCase(const char* a, const char* b) {
	for (; *a != '\0' && *b != '\0'; ++a, ++b) {
		const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + ('a' - 'A')) : *a;
		const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + ('a' - 'A')) : *b;
		if (ca != cb) {
			return false;
		}
	}
	return *a == '\0' && *b == '\0';
}

}  // namespace

bool ParseMode(const char* text, Mode* out) {
	if (text == nullptr) {
		return false;
	}
	if (EqualsIgnoringCase(text, "auto")) {
		*out = Mode::Auto;
	} else if (EqualsIgnoringCase(text, "nvidia")) {
		*out = Mode::Nvidia;
	} else if (EqualsIgnoringCase(text, "amd")) {
		*out = Mode::Amd;
	} else if (EqualsIgnoringCase(text, "off")) {
		*out = Mode::Off;
	} else {
		return false;
	}
	return true;
}

const char* ModeName(Mode mode) {
	switch (mode) {
		case Mode::Auto: return "auto";
		case Mode::Nvidia: return "nvidia";
		case Mode::Amd: return "amd";
		case Mode::Off: return "off";
	}
	return "auto";
}

const char* HackName(Hack hack) {
	switch (hack) {
		case Hack::None: return "none";
		case Hack::NvidiaAtoc: return "NVIDIA ATOC (D3DRS_ADAPTIVETESS_Y)";
		case Hack::AmdA2M: return "AMD A2M1/A2M0 (D3DRS_POINTSIZE)";
	}
	return "none";
}

Hack ChooseHack(Mode mode, Vendor vendor, bool atocFormatSupported) {
	switch (mode) {
		case Mode::Off: return Hack::None;
		case Mode::Nvidia: return Hack::NvidiaAtoc;
		case Mode::Amd: return Hack::AmdA2M;
		case Mode::Auto: break;
	}
	if (vendor == Vendor::Amd) {
		return Hack::AmdA2M;
	}
	return atocFormatSupported ? Hack::NvidiaAtoc : Hack::None;
}

const char* NoneReason(Mode mode, Vendor vendor, bool atocFormatSupported) {
	if (ChooseHack(mode, vendor, atocFormatSupported) != Hack::None) {
		return "";
	}
	if (mode == Mode::Off) {
		return "Mode=off in the INI";
	}
	return "the driver does not report the 'ATOC' format (set Mode=nvidia or Mode=amd to force one)";
}

int BeginSceneWrites(Hack hack, StateWrite out[2]) {
	switch (hack) {
		case Hack::NvidiaAtoc:
			out[0] = StateWrite{kRsAdaptiveTessY, kFourCCAtoc};
			return 1;
		case Hack::AmdA2M:
		case Hack::None:
			return 0;
	}
	return 0;
}

int AlphaTestWrites(Hack hack, uint32_t alphaTestEnabled, StateWrite out[2]) {
	switch (hack) {
		case Hack::AmdA2M:
			out[0] = StateWrite{kRsPointSize, alphaTestEnabled != 0 ? kFourCCA2M1 : kFourCCA2M0};
			return 1;
		case Hack::NvidiaAtoc:
		case Hack::None:
			return 0;
	}
	return 0;
}

int SuspendCoverageWrites(Hack hack, StateWrite out[2]) {
	switch (hack) {
		case Hack::NvidiaAtoc:
			out[0] = StateWrite{kRsAdaptiveTessY, kFourCCNone};
			return 1;
		case Hack::AmdA2M:
			out[0] = StateWrite{kRsPointSize, kFourCCA2M0};
			return 1;
		case Hack::None:
			return 0;
	}
	return 0;
}

int ResumeCoverageWrites(Hack hack, uint32_t alphaTestEnabled, StateWrite out[2]) {
	switch (hack) {
		case Hack::NvidiaAtoc:
			out[0] = StateWrite{kRsAdaptiveTessY, kFourCCAtoc};
			return 1;
		case Hack::AmdA2M:
			return AlphaTestWrites(hack, alphaTestEnabled, out);
		case Hack::None:
			return 0;
	}
	return 0;
}

bool ParseLeafMethod(const char* text, LeafMethod* out) {
	if (text == nullptr) {
		return false;
	}
	if (EqualsIgnoringCase(text, "supersample")) {
		*out = LeafMethod::Supersample;
	} else if (EqualsIgnoringCase(text, "coverage")) {
		*out = LeafMethod::Coverage;
	} else {
		return false;
	}
	return true;
}

const char* LeafMethodName(LeafMethod method) {
	switch (method) {
		case LeafMethod::Supersample: return "supersample";
		case LeafMethod::Coverage: return "coverage";
	}
	return "supersample";
}

bool TargetIsMultisampled(uint32_t multiSampleType, uint32_t multiSampleQuality) {
	// D3DMULTISAMPLE_NONE = 0, D3DMULTISAMPLE_NONMASKABLE = 1, then the
	// sample counts 2..16 (d3d9types.h).
	if (multiSampleType >= 2) {
		return true;
	}
	return multiSampleType == 1 && multiSampleQuality > 0;
}

}  // namespace foliageaa
