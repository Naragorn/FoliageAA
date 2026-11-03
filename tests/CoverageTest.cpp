// Every flow of the decisions in Coverage.

#include "Coverage.h"

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

using namespace foliageaa;

void TestFourCC() {
	// 'ATOC' as the bytes "ATOC" in memory, the way MAKEFOURCC lays it out.
	const uint32_t atoc = kFourCCAtoc;
	Check(std::memcmp(&atoc, "ATOC", 4) == 0, "ATOC bytes");
	const uint32_t a2m1 = kFourCCA2M1;
	Check(std::memcmp(&a2m1, "A2M1", 4) == 0, "A2M1 bytes");
	const uint32_t a2m0 = kFourCCA2M0;
	Check(std::memcmp(&a2m0, "A2M0", 4) == 0, "A2M0 bytes");
	Check(kFourCCNone == 0, "off is D3DFMT_UNKNOWN");
	Check(kRsAlphaTestEnable == 15 && kRsPointSize == 154 && kRsAdaptiveTessY == 181, "render state ids");
}

void TestVendor() {
	Check(VendorFromId(0x10DE) == Vendor::Nvidia, "NVIDIA id");
	Check(VendorFromId(0x1002) == Vendor::Amd, "AMD id");
	Check(VendorFromId(0x8086) == Vendor::Intel, "Intel id");
	Check(VendorFromId(0x1414) == Vendor::Other, "other id");
	Check(VendorFromId(0) == Vendor::Other, "zero id");
	Check(std::strcmp(VendorName(Vendor::Nvidia), "NVIDIA") == 0, "NVIDIA name");
	Check(std::strcmp(VendorName(Vendor::Amd), "AMD") == 0, "AMD name");
	Check(std::strcmp(VendorName(Vendor::Intel), "Intel") == 0, "Intel name");
	Check(std::strcmp(VendorName(Vendor::Other), "other") == 0, "other name");
}

void TestParseMode() {
	Mode mode = Mode::Off;
	Check(ParseMode("auto", &mode) && mode == Mode::Auto, "auto");
	Check(ParseMode("NVIDIA", &mode) && mode == Mode::Nvidia, "nvidia upper case");
	Check(ParseMode("Amd", &mode) && mode == Mode::Amd, "amd mixed case");
	Check(ParseMode("off", &mode) && mode == Mode::Off, "off");
	mode = Mode::Amd;
	Check(!ParseMode("", &mode) && mode == Mode::Amd, "empty refused, untouched");
	Check(!ParseMode("intel", &mode) && mode == Mode::Amd, "unknown refused, untouched");
	Check(!ParseMode("autox", &mode) && mode == Mode::Amd, "longer refused");
	Check(!ParseMode("aut", &mode) && mode == Mode::Amd, "shorter refused");
	Check(!ParseMode(nullptr, &mode) && mode == Mode::Amd, "null refused");
	Check(std::strcmp(ModeName(Mode::Auto), "auto") == 0 && std::strcmp(ModeName(Mode::Nvidia), "nvidia") == 0 &&
	          std::strcmp(ModeName(Mode::Amd), "amd") == 0 && std::strcmp(ModeName(Mode::Off), "off") == 0,
	      "mode names");
}

void TestChooseHack() {
	// Off wins over everything.
	Check(ChooseHack(Mode::Off, Vendor::Nvidia, true) == Hack::None, "off on NVIDIA");
	Check(ChooseHack(Mode::Off, Vendor::Amd, false) == Hack::None, "off on AMD");
	// Forced modes ignore vendor and probe.
	Check(ChooseHack(Mode::Nvidia, Vendor::Amd, false) == Hack::NvidiaAtoc, "forced nvidia on AMD");
	Check(ChooseHack(Mode::Amd, Vendor::Nvidia, true) == Hack::AmdA2M, "forced amd on NVIDIA");
	// Auto: AMD takes its own route whatever the probe said.
	Check(ChooseHack(Mode::Auto, Vendor::Amd, false) == Hack::AmdA2M, "auto AMD unsupported probe");
	Check(ChooseHack(Mode::Auto, Vendor::Amd, true) == Hack::AmdA2M, "auto AMD supported probe");
	// Auto: everyone else follows the probe.
	Check(ChooseHack(Mode::Auto, Vendor::Nvidia, true) == Hack::NvidiaAtoc, "auto NVIDIA supported");
	Check(ChooseHack(Mode::Auto, Vendor::Nvidia, false) == Hack::None, "auto NVIDIA unsupported");
	Check(ChooseHack(Mode::Auto, Vendor::Intel, true) == Hack::NvidiaAtoc, "auto Intel supported");
	Check(ChooseHack(Mode::Auto, Vendor::Other, false) == Hack::None, "auto other unsupported");

	Check(std::strcmp(NoneReason(Mode::Off, Vendor::Nvidia, true), "Mode=off in the INI") == 0, "reason off");
	Check(std::strstr(NoneReason(Mode::Auto, Vendor::Nvidia, false), "'ATOC'") != nullptr, "reason probe");
	Check(std::strcmp(NoneReason(Mode::Auto, Vendor::Nvidia, true), "") == 0, "no reason when enabled");
	Check(std::strcmp(HackName(Hack::None), "none") == 0 && std::strstr(HackName(Hack::NvidiaAtoc), "ATOC") != nullptr &&
	          std::strstr(HackName(Hack::AmdA2M), "A2M1") != nullptr,
	      "hack names");
}

void TestWrites() {
	StateWrite writes[2] = {};
	Check(BeginSceneWrites(Hack::NvidiaAtoc, writes) == 1 && writes[0].state == kRsAdaptiveTessY &&
	          writes[0].value == kFourCCAtoc,
	      "NVIDIA: ATOC each BeginScene");
	Check(BeginSceneWrites(Hack::AmdA2M, writes) == 0, "AMD: nothing at BeginScene");
	Check(BeginSceneWrites(Hack::None, writes) == 0, "none: nothing at BeginScene");

	Check(AlphaTestWrites(Hack::AmdA2M, 1, writes) == 1 && writes[0].state == kRsPointSize &&
	          writes[0].value == kFourCCA2M1,
	      "AMD: alpha test on -> A2M1");
	Check(AlphaTestWrites(Hack::AmdA2M, 0, writes) == 1 && writes[0].state == kRsPointSize &&
	          writes[0].value == kFourCCA2M0,
	      "AMD: alpha test off -> A2M0");
	Check(AlphaTestWrites(Hack::AmdA2M, 7, writes) == 1 && writes[0].value == kFourCCA2M1, "AMD: any non-zero is on");
	Check(AlphaTestWrites(Hack::NvidiaAtoc, 1, writes) == 0, "NVIDIA: the driver gates on alpha test itself");
	Check(AlphaTestWrites(Hack::None, 1, writes) == 0, "none: nothing on alpha test");
}

void TestMultisampled() {
	Check(!TargetIsMultisampled(0, 0), "NONE");
	Check(!TargetIsMultisampled(1, 0), "NONMASKABLE quality 0");
	Check(TargetIsMultisampled(1, 1), "NONMASKABLE quality 1");
	Check(TargetIsMultisampled(2, 0), "2 samples");
	Check(TargetIsMultisampled(8, 0), "8 samples");
	Check(TargetIsMultisampled(16, 3), "16 samples");
}

}  // namespace

int main() {
	TestFourCC();
	TestVendor();
	TestParseMode();
	TestChooseHack();
	TestWrites();
	TestMultisampled();
	std::printf(g_failures == 0 ? "CoverageTest: all passed\n" : "CoverageTest: %d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
