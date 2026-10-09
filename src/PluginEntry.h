#pragma once

#include "Coverage.h"

#include <cstdint>

// What the plugin does when xOBSE queries and loads it, and when the game
// reports itself initialized - with the side effects behind a Host so every
// flow runs in a test.

namespace foliageaa {

// First fields of xOBSE's OBSEInterface (obse/obse/PluginAPI.h); nothing
// past isEditor is read here. The real interface is handed on to the Host
// untouched for the messaging registration.
struct ExtenderInterfaceHead {
	uint32_t obseVersion;
	uint32_t oblivionVersion;
	uint32_t editorVersion;
	uint32_t isEditor;
};

// PluginAPI.h PluginInfo; kInfoVersion is 3 in xOBSE 22.
struct PluginInfo {
	uint32_t infoVersion;
	const char* name;
	uint32_t version;
};

constexpr const char* kPluginName = "FoliageAA";
constexpr uint32_t kPluginVersion = 1;
constexpr uint32_t kInfoVersion = 3;

// obse_common/obse_version.h: MAKE_OBLIVION_VERSION(1, 2, 416). The plugin
// reads the renderer through an address of this build and loads on no other.
constexpr uint32_t kOblivionVersion_1_2_416 = 0x010201A0;

// xOBSE 22.10+: OBSEMessagingInterface::kMessage_GameInitialized, "sent to
// plugins when the game is fully initialized (shows main menu)". The device
// exists by then. Counted from PluginAPI.h's enum: PostLoad 0 ... PostPostLoad
// 9, RuntimeScriptError 10, GameInitialized 11.
constexpr uint32_t kMessageGameInitialized = 11;

struct Settings {
	bool enable = true;
	Mode mode = Mode::Auto;
	// [Coverage] Enable: alpha-to-coverage for the alpha-tested draws.
	bool coverage = false;
	// [Leaves]: Method and Passes (Supersample.h), and for the coverage
	// method the sharpening (ShaderPatch.h); threshold below 0 means
	// "follow the engine's alpha reference".
	LeafMethod leafMethod = LeafMethod::Supersample;
	int passes = 8;
	bool sharpenLeaves = true;
	float threshold = -1.0f;
	float steepness = 4.0f;
	// [Diagnostics] DumpShaders.
	bool dumpShaders = false;
	// [Diagnostics] ToggleKey: a virtual-key code that switches the plugin
	// off and on in the game, 0 for none.
	int toggleKey = 0;
};

struct Host {
	void (*resetLog)();
	void (*log)(const char* format, ...);
	// Reads the INI next to the DLL into *settings; false if there is no INI
	// (the defaults then stand). An unknown Mode is logged and left at auto.
	bool (*readSettings)(Settings* settings);
	// Registers the plugin's message handler (which calls OnMessage) with
	// xOBSE's messaging interface; false if that interface is unavailable.
	bool (*registerListener)(const ExtenderInterfaceHead* xse);
	// The engine's device, or null if the renderer pointer is not readable or
	// the renderer has no device yet.
	void* (*findDevice)();
	// SetupDevice, as a side effect, with the settings Load read.
	void (*setupDevice)(void* device, const Settings& settings);
};

bool Query(const ExtenderInterfaceHead& xse, PluginInfo* info, const Host& host);
// Decides whether to register for the game-initialized message; the Settings
// it read are kept for the message. Always true: refusing to load would make
// nothing safer, and the log says what was skipped.
bool Load(const ExtenderInterfaceHead& xse, const Host& host);
// Runs once on the first game-initialized message, ignores every other type
// and every repeat.
void OnMessage(uint32_t messageType, const Host& host);

// The state Load leaves for OnMessage; exposed for tests.
const Settings& LoadedSettings();
bool GameInitializedHandled();
void ResetForTest();

}  // namespace foliageaa
