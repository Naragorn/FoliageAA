// Loads the built plugin DLL the way xOBSE does and calls its real exports.
//
//   ExportsTest.exe <FoliageAA.dll>
//
// Checks: the two exports exist and nothing else is exported; Query fills
// PluginInfo; the editor Load does nothing; a game Load reads the INI next to
// the DLL, registers with a fake messaging interface, and the registered
// handler, on the game-initialized message, finds no renderer in this process
// and says so instead of touching memory.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what);
	}
}

struct Info {
	UINT32 infoVersion;
	const char* name;
	UINT32 version;
};
using QueryFn = bool (*)(const void*, Info*);
using LoadFn = bool (*)(const void*);

// xOBSE's OBSEInterface and OBSEMessagingInterface, as far as the plugin
// reads them.
struct Message {
	const char* sender;
	UINT32 type;
	UINT32 dataLen;
	void* data;
};
using Handler = void (*)(Message*);
struct Messaging {
	UINT32 version;
	bool (*RegisterListener)(UINT32, const char*, Handler);
	bool (*Dispatch)(UINT32, UINT32, void*, UINT32, const char*);
};
struct Obse {
	UINT32 obseVersion, oblivionVersion, editorVersion, isEditor;
	bool (*RegisterCommand)(void*);
	void (*SetOpcodeBase)(UINT32);
	void* (*QueryInterface)(UINT32);
	UINT32 (*GetPluginHandle)();
};

Handler g_handler = nullptr;
UINT32 g_listenerHandle = 0;
std::string g_sender;
bool RegisterListener(UINT32 handle, const char* sender, Handler handler) {
	g_listenerHandle = handle;
	g_sender = sender;
	g_handler = handler;
	return true;
}
bool Dispatch(UINT32, UINT32, void*, UINT32, const char*) { return false; }
Messaging g_messaging{1, &RegisterListener, &Dispatch};
void* QueryInterface(UINT32 id) { return id == 4 ? &g_messaging : nullptr; }
void* QueryNothing(UINT32) { return nullptr; }
UINT32 GetPluginHandle() { return 7; }

DWORD ExportedNameCount(HMODULE module) {
	auto* base = reinterpret_cast<const BYTE*>(module);
	auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
	return exports->NumberOfNames;
}

std::string ReadLog() {
	std::ifstream file("FoliageAA.log");
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

}  // namespace

int main(int argc, char** argv) {
	if (argc != 2) {
		std::printf("usage: ExportsTest.exe <FoliageAA.dll>\n");
		return 2;
	}
	// The plugin writes its log into the current directory and reads its INI
	// next to the DLL, as in the game.
	char temp[MAX_PATH];
	GetTempPathA(MAX_PATH, temp);
	const std::string dir = std::string(temp) + "FoliageAAExportsTest";
	CreateDirectoryA(dir.c_str(), nullptr);
	char dll[MAX_PATH];
	GetFullPathNameA(argv[1], MAX_PATH, dll, nullptr);
	SetCurrentDirectoryA(dir.c_str());
	std::string ini = dll;
	ini = ini.substr(0, ini.find_last_of('\\') + 1) + "FoliageAA.ini";
	{
		std::ofstream file(ini);
		file << "[Main]\nEnable=1\nMode=nvidia\n[Leaves]\nMethod=coverage\nPasses=4\nSharpenLeaves=1\nThreshold=0.6\n"
		        "Steepness=8\n[Coverage]\nEnable=1\n[Diagnostics]\nDumpShaders=1\nToggleKey=0x7A\n";
	}

	HMODULE module = LoadLibraryA(dll);
	Check(module != nullptr, "DLL loads");
	if (module == nullptr) {
		return 1;
	}
	Check(ExportedNameCount(module) == 2, "exactly two exports");
	auto queryFn = reinterpret_cast<QueryFn>(GetProcAddress(module, "OBSEPlugin_Query"));
	auto loadFn = reinterpret_cast<LoadFn>(GetProcAddress(module, "OBSEPlugin_Load"));
	Check(queryFn != nullptr && loadFn != nullptr, "Query and Load exported");
	if (queryFn == nullptr || loadFn == nullptr) {
		return 1;
	}

	Info info{};
	Obse editor{22, 0, 0x01020000, 1, nullptr, nullptr, &QueryInterface, &GetPluginHandle};
	Check(queryFn(&editor, &info), "Query returns true");
	Check(info.infoVersion == 3 && info.name != nullptr && std::strcmp(info.name, "FoliageAA") == 0 && info.version == 1,
	      "Query fills PluginInfo");
	Check(loadFn(&editor), "editor Load returns true");
	Check(ReadLog().find("Editor: nothing to do") != std::string::npos, "editor Load logged");

	// A game without a messaging interface.
	Obse bare{22, 0x010201A0, 0, 0, nullptr, nullptr, &QueryNothing, &GetPluginHandle};
	queryFn(&bare, &info);
	Check(loadFn(&bare), "game Load without messaging returns true");
	Check(ReadLog().find("no messaging interface") != std::string::npos, "missing messaging logged");
	Check(g_handler == nullptr, "nothing registered without messaging");

	// A game with one: the INI next to the DLL is read, the listener registered.
	Obse game{22, 0x010201A0, 0, 0, nullptr, nullptr, &QueryInterface, &GetPluginHandle};
	queryFn(&game, &info);
	Check(loadFn(&game), "game Load returns true");
	const std::string log = ReadLog();
	Check(log.find("Enable=1 Mode=nvidia Coverage=1 Leaves=coverage Passes=4 SharpenLeaves=1 Threshold=0.60 Steepness=8.0 DumpShaders=1 ToggleKey=0x7A") != std::string::npos,
	      "INI next to the DLL read, floats included");
	Check(log.find("Waiting for the game to initialize (Mode=nvidia, Coverage=1, Leaves=coverage, Passes=4, SharpenLeaves=1, Threshold=0.60, Steepness=8.0, DumpShaders=1, ToggleKey=0x7A)") !=
	          std::string::npos,
	      "waiting logged");
	Check(g_handler != nullptr && g_listenerHandle == 7 && g_sender == "OBSE", "listener registered for OBSE with the plugin handle");

	if (g_handler != nullptr) {
		Message other{"OBSE", 8, 0, nullptr};
		g_handler(&other);
		Check(ReadLog().find("Game initialized") == std::string::npos, "other messages ignored");
		Message initialized{"OBSE", 11, 0, nullptr};
		g_handler(&initialized);
		const std::string after = ReadLog();
		// This process has no Oblivion renderer. Either the address is not
		// mapped at all (the usual case) and the plugin says so, or something
		// unrelated happens to sit there and the device check refuses it. The
		// one outcome that must not happen is a device being set up.
		Check(after.find("renderer has no device") != std::string::npos ||
		          after.find("does not carry a method table") != std::string::npos ||
		          after.find("is not an IDirect3DDevice9") != std::string::npos,
		      "no renderer in this process, said in the log");
		Check(after.find("enabled, applied from the next frame on") == std::string::npos, "nothing set up");
	}
	std::printf("log:\n%s", ReadLog().c_str());
	DeleteFileA(ini.c_str());

	std::printf(g_failures == 0 ? "ExportsTest: all passed\n" : "ExportsTest: %d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
