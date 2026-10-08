// The DLL's exports and real side effects; the decisions live in PluginEntry,
// Coverage and Setup.

#include "PluginEntry.h"
#include "Setup.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr const char* kLogFile = "FoliageAA.log";
constexpr const char* kIniFile = "FoliageAA.ini";

// Oblivion 1.2.416: the NiDX9Renderer pointer, and the IDirect3DDevice9 at
// +0x280 inside it. Two independent sources: OBGEv2's Nodes/NiDX9Renderer.cpp
// (namespace v1_2_416, "mov eax,0x00B3F928; mov eax,[eax]") and xOBSE's
// obse/obse/NiRenderer.h ("IDirect3DDevice9 * device; // 280", with the
// struct's size asserted at compile time). The 5-byte read "A1 28 F9 B3 00"
// occurs 41 times in Oblivion.exe, which is what a renderer global looks like.
constexpr uintptr_t kRendererPointer = 0x00B3F928;
constexpr uintptr_t kRendererDeviceOffset = 0x280;

// xOBSE PluginAPI.h: kInterface_Messaging = 4 (Console 0, Serialization 1,
// StringVar 2, IO 3, Messaging 4).
constexpr uint32_t kInterfaceMessaging = 4;

HMODULE g_module = nullptr;
SRWLOCK g_logLock = SRWLOCK_INIT;

void AppendLine(const char* format, va_list args) {
	AcquireSRWLockExclusive(&g_logLock);
	if (FILE* file = std::fopen(kLogFile, "a")) {
		std::vfprintf(file, format, args);
		std::fputc('\n', file);
		std::fclose(file);
	}
	ReleaseSRWLockExclusive(&g_logLock);
}

void Log(const char* format, ...) {
	va_list args;
	va_start(args, format);
	AppendLine(format, args);
	va_end(args);
}

void ResetLog() {
	AcquireSRWLockExclusive(&g_logLock);
	if (FILE* file = std::fopen(kLogFile, "w")) {
		std::fclose(file);
	}
	ReleaseSRWLockExclusive(&g_logLock);
}

bool IniPath(char* path, size_t length) {
	char module[MAX_PATH] = {};
	if (GetModuleFileNameA(g_module, module, MAX_PATH) == 0) {
		return false;
	}
	char* slash = std::strrchr(module, '\\');
	if (slash == nullptr) {
		return false;
	}
	*(slash + 1) = '\0';
	std::snprintf(path, length, "%s%s", module, kIniFile);
	return true;
}

// A float key; the fallback when the key is missing or not a number.
float ReadFloat(const char* path, const char* section, const char* key, float fallback) {
	char text[32] = {};
	GetPrivateProfileStringA(section, key, "", text, sizeof(text), path);
	char* end = nullptr;
	const float value = std::strtof(text, &end);
	if (text[0] == '\0' || end == text) {
		return fallback;
	}
	return value;
}

bool ReadSettings(foliageaa::Settings* settings) {
	char path[MAX_PATH] = {};
	if (!IniPath(path, sizeof(path)) || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
	settings->enable = GetPrivateProfileIntA("Main", "Enable", 1, path) != 0;
	char mode[32] = {};
	GetPrivateProfileStringA("Main", "Mode", "auto", mode, sizeof(mode), path);
	if (!foliageaa::ParseMode(mode, &settings->mode)) {
		Log("Mode=%s in the INI is not auto, nvidia, amd or off - using auto", mode);
		settings->mode = foliageaa::Mode::Auto;
	}
	settings->sharpenLeaves = GetPrivateProfileIntA("Leaves", "SharpenLeaves", 1, path) != 0;
	// Threshold: "engine" (or anything that is not a number) follows the
	// engine's alpha reference; a number fixes it.
	settings->threshold = ReadFloat(path, "Leaves", "Threshold", -1.0f);
	settings->steepness = ReadFloat(path, "Leaves", "Steepness", 4.0f);
	settings->dumpShaders = GetPrivateProfileIntA("Diagnostics", "DumpShaders", 0, path) != 0;
	char threshold[16];
	if (settings->threshold < 0.0f) {
		std::snprintf(threshold, sizeof(threshold), "engine");
	} else {
		std::snprintf(threshold, sizeof(threshold), "%.2f", settings->threshold);
	}
	Log("Settings from %s: Enable=%d Mode=%s SharpenLeaves=%d Threshold=%s Steepness=%.1f DumpShaders=%d", path,
	    settings->enable ? 1 : 0, foliageaa::ModeName(settings->mode), settings->sharpenLeaves ? 1 : 0, threshold,
	    settings->steepness, settings->dumpShaders ? 1 : 0);
	return true;
}

// xOBSE PluginAPI.h: OBSEInterface past the four version fields, and
// OBSEMessagingInterface.
struct ObseInterface {
	uint32_t obseVersion;
	uint32_t oblivionVersion;
	uint32_t editorVersion;
	uint32_t isEditor;
	bool (*RegisterCommand)(void* info);
	void (*SetOpcodeBase)(uint32_t opcode);
	void* (*QueryInterface)(uint32_t id);
	uint32_t (*GetPluginHandle)();
};

struct MessagingInterface {
	struct Message {
		const char* sender;
		uint32_t type;
		uint32_t dataLen;
		void* data;
	};
	uint32_t version;
	bool (*RegisterListener)(uint32_t listener, const char* sender, void (*handler)(Message*));
	bool (*Dispatch)(uint32_t sender, uint32_t messageType, void* data, uint32_t dataLen, const char* receiver);
};

extern const foliageaa::Host kHost;

void OnObseMessage(MessagingInterface::Message* message) {
	if (message != nullptr) {
		foliageaa::OnMessage(message->type, kHost);
	}
}

bool RegisterListener(const foliageaa::ExtenderInterfaceHead* xse) {
	const auto* obse = reinterpret_cast<const ObseInterface*>(xse);
	if (obse->QueryInterface == nullptr) {
		return false;
	}
	auto* messaging = static_cast<MessagingInterface*>(obse->QueryInterface(kInterfaceMessaging));
	if (messaging == nullptr || messaging->RegisterListener == nullptr || obse->GetPluginHandle == nullptr) {
		return false;
	}
	return messaging->RegisterListener(obse->GetPluginHandle(), "OBSE", &OnObseMessage);
}

bool IsReadable(uintptr_t address, size_t bytes) {
	MEMORY_BASIC_INFORMATION info{};
	if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) == 0 || info.State != MEM_COMMIT) {
		return false;
	}
	const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
	                       PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
	if ((info.Protect & readable) == 0 || (info.Protect & PAGE_GUARD) != 0) {
		return false;
	}
	const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
	return address + bytes <= end;
}

void* FindDevice() {
	if (!IsReadable(kRendererPointer, sizeof(void*))) {
		Log("Renderer pointer %08X is not readable in this process", static_cast<unsigned>(kRendererPointer));
		return nullptr;
	}
	const uintptr_t renderer = *reinterpret_cast<const uintptr_t*>(kRendererPointer);
	if (renderer == 0 || !IsReadable(renderer + kRendererDeviceOffset, sizeof(void*))) {
		return nullptr;
	}
	return *reinterpret_cast<void* const*>(renderer + kRendererDeviceOffset);
}

void SetupDeviceSideEffect(void* device, const foliageaa::Settings& settings) {
	foliageaa::Options options;
	options.mode = settings.mode;
	options.sharpenLeaves = settings.sharpenLeaves;
	options.threshold = settings.threshold;
	options.steepness = settings.steepness;
	if (settings.dumpShaders) {
		options.dumpDirectory = "FoliageAA-shaders";  // in the game folder, next to the log
	}
	foliageaa::SetupDevice(device, options, &Log);
}

const foliageaa::Host kHost{&ResetLog, &Log, &ReadSettings, &RegisterListener, &FindDevice, &SetupDeviceSideEffect};

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_module = instance;
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}

extern "C" {

__declspec(dllexport) bool OBSEPlugin_Query(const foliageaa::ExtenderInterfaceHead* xse, foliageaa::PluginInfo* info) {
	return foliageaa::Query(*xse, info, kHost);
}

__declspec(dllexport) bool OBSEPlugin_Load(const foliageaa::ExtenderInterfaceHead* xse) {
	return foliageaa::Load(*xse, kHost);
}

}  // extern "C"
