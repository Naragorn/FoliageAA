// Every flow of Query, Load and OnMessage, with the side effects replaced by
// fakes.

#include "PluginEntry.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what);
	}
}

struct Fake {
	int resets = 0;
	bool iniExists = true;
	foliageaa::Settings iniSettings;
	bool registerResult = true;
	int registrations = 0;
	void* device = nullptr;
	int finds = 0;
	int setups = 0;
	foliageaa::Settings setupSettings;
	void* setupDevice = nullptr;
	std::vector<std::string> lines;

	bool Logged(const char* text) const {
		for (const auto& line : lines) {
			if (line.find(text) != std::string::npos) {
				return true;
			}
		}
		return false;
	}
};
Fake* g_fake = nullptr;

void FakeReset() { ++g_fake->resets; }
void FakeLog(const char* format, ...) {
	char buffer[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	g_fake->lines.push_back(buffer);
}
bool FakeReadSettings(foliageaa::Settings* settings) {
	if (!g_fake->iniExists) {
		return false;
	}
	*settings = g_fake->iniSettings;
	return true;
}
bool FakeRegister(const foliageaa::ExtenderInterfaceHead*) {
	++g_fake->registrations;
	return g_fake->registerResult;
}
void* FakeFind() {
	++g_fake->finds;
	return g_fake->device;
}
void FakeSetup(void* device, const foliageaa::Settings& settings) {
	++g_fake->setups;
	g_fake->setupDevice = device;
	g_fake->setupSettings = settings;
}

const foliageaa::Host kFakeHost{&FakeReset, &FakeLog, &FakeReadSettings, &FakeRegister, &FakeFind, &FakeSetup};

using foliageaa::ExtenderInterfaceHead;
using foliageaa::Mode;

const ExtenderInterfaceHead kGame{22, foliageaa::kOblivionVersion_1_2_416, 0, 0};

void TestQuery() {
	Fake fake;
	g_fake = &fake;
	foliageaa::PluginInfo info{};
	Check(foliageaa::Query(kGame, &info, kFakeHost), "Query true");
	Check(info.infoVersion == 3 && std::string(info.name) == "FoliageAA" && info.version == 1, "Query fills PluginInfo");
	Check(fake.resets == 1, "Query resets the log");
	Check(fake.Logged("FoliageAA 1 - xOBSE 22, Oblivion 010201A0, editor=0"), "Query logs the versions");
}

void TestLoadEditor() {
	Fake fake;
	g_fake = &fake;
	foliageaa::ResetForTest();
	const ExtenderInterfaceHead editor{22, 0, 0x01020000, 1};
	Check(foliageaa::Load(editor, kFakeHost), "editor Load true");
	Check(fake.registrations == 0 && fake.Logged("Editor: nothing to do"), "editor: no registration");
}

void TestLoadWrongVersion() {
	Fake fake;
	g_fake = &fake;
	foliageaa::ResetForTest();
	const ExtenderInterfaceHead other{22, 0x01020100, 0, 0};
	Check(foliageaa::Load(other, kFakeHost), "wrong version Load true");
	Check(fake.registrations == 0 && fake.Logged("is not 1.2.416"), "wrong version: refused, logged");
}

void TestLoadDisabled() {
	Fake fake;
	fake.iniSettings.enable = false;
	g_fake = &fake;
	foliageaa::ResetForTest();
	Check(foliageaa::Load(kGame, kFakeHost), "disabled Load true");
	Check(fake.registrations == 0 && fake.Logged("Enable=0"), "disabled: no registration");
}

void TestLoadNoIni() {
	Fake fake;
	fake.iniExists = false;
	g_fake = &fake;
	foliageaa::ResetForTest();
	Check(foliageaa::Load(kGame, kFakeHost), "no INI Load true");
	Check(fake.Logged("No FoliageAA.ini"), "no INI logged");
	Check(fake.registrations == 1 && foliageaa::LoadedSettings().enable &&
	          foliageaa::LoadedSettings().mode == Mode::Auto,
	      "no INI: defaults, registered");
}

void TestLoadNoMessaging() {
	Fake fake;
	fake.registerResult = false;
	g_fake = &fake;
	foliageaa::ResetForTest();
	Check(foliageaa::Load(kGame, kFakeHost), "no messaging Load true");
	Check(fake.registrations == 1 && fake.Logged("no messaging interface"), "no messaging: logged");
}

void TestLoadOk() {
	Fake fake;
	fake.iniSettings.mode = Mode::Nvidia;
	g_fake = &fake;
	foliageaa::ResetForTest();
	Check(foliageaa::Load(kGame, kFakeHost), "Load true");
	Check(fake.registrations == 1 &&
	          fake.Logged("Waiting for the game to initialize (Mode=nvidia, SharpenLeaves=1, Threshold=engine, Steepness=4.0, DumpShaders=0)"),
	      "registered with the INI's settings");
	Check(foliageaa::LoadedSettings().mode == Mode::Nvidia, "settings kept for the message");
}

void TestMessageOtherType() {
	Fake fake;
	fake.device = &fake;
	g_fake = &fake;
	foliageaa::ResetForTest();
	foliageaa::Load(kGame, kFakeHost);
	foliageaa::OnMessage(8, kFakeHost);  // kMessage_PostLoadGame
	Check(fake.finds == 0 && fake.setups == 0 && !foliageaa::GameInitializedHandled(), "other message ignored");
}

void TestMessageNoDevice() {
	Fake fake;
	fake.device = nullptr;
	g_fake = &fake;
	foliageaa::ResetForTest();
	foliageaa::Load(kGame, kFakeHost);
	foliageaa::OnMessage(foliageaa::kMessageGameInitialized, kFakeHost);
	Check(fake.finds == 1 && fake.setups == 0 && fake.Logged("renderer has no device"), "no device: logged, no setup");
	Check(foliageaa::GameInitializedHandled(), "counts as handled");
}

void TestMessageDevice() {
	Fake fake;
	fake.device = &fake;
	fake.iniSettings.mode = Mode::Amd;
	fake.iniSettings.sharpenLeaves = false;
	fake.iniSettings.threshold = 0.3f;
	fake.iniSettings.steepness = 9.0f;
	fake.iniSettings.dumpShaders = true;
	g_fake = &fake;
	foliageaa::ResetForTest();
	foliageaa::Load(kGame, kFakeHost);
	foliageaa::OnMessage(foliageaa::kMessageGameInitialized, kFakeHost);
	Check(fake.setups == 1 && fake.setupDevice == &fake && fake.setupSettings.mode == Mode::Amd &&
	          !fake.setupSettings.sharpenLeaves && fake.setupSettings.threshold == 0.3f &&
	          fake.setupSettings.steepness == 9.0f && fake.setupSettings.dumpShaders,
	      "device: setup with the INI's settings");
	Check(fake.Logged("Game initialized, device"), "device logged");
	// A repeat does nothing.
	foliageaa::OnMessage(foliageaa::kMessageGameInitialized, kFakeHost);
	Check(fake.finds == 1 && fake.setups == 1, "repeat ignored");
}

}  // namespace

int main() {
	TestQuery();
	TestLoadEditor();
	TestLoadWrongVersion();
	TestLoadDisabled();
	TestLoadNoIni();
	TestLoadNoMessaging();
	TestLoadOk();
	TestMessageOtherType();
	TestMessageNoDevice();
	TestMessageDevice();
	std::printf(g_failures == 0 ? "PluginEntryTest: all passed\n" : "PluginEntryTest: %d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
