#include "voice_presets.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int Fail(const char* message) {
	std::cerr << message << '\n';
	return 1;
}

} // namespace

int main(int argc, char** argv) {
	if (argc != 2) return Fail("expected fixture directory argument");

	VoicePresetRegistry registry;
	if (registry.Reload(argv[1]) != 1) return Fail("expected exactly one valid preset");
	if (registry.Errors().size() != 1) return Fail("expected exactly one rejected preset");

	const AudioEffects::Preset& loaded = registry.CustomPresets().front();
	if (loaded.id < AudioEffects::CUSTOM_ID_MIN || loaded.id > AudioEffects::CUSTOM_ID_MAX)
		return Fail("custom preset ID is outside the reserved range");
	if (loaded.name != "Parser test radio" || loaded.key != "parser_test_radio")
		return Fail("metadata was parsed incorrectly");
	if (std::fabs(loaded.gain - 8.0f) > 0.0001f || loaded.downsample != 32)
		return Fail("numeric limits were not applied");

	const int stableId = loaded.id;
	AudioEffects::Preset found;
	if (!registry.Find(stableId, found) || found.sourceFile != "valid_radio.gvox")
		return Fail("lookup by ID failed");
	if (registry.Reload(argv[1]) != 1 || registry.CustomPresets().front().id != stableId)
		return Fail("preset ID changed after reload");

	const std::vector<std::pair<std::string, std::string>> memoryFiles = {
		{"injected.gvox", "version=1\nname=Injected from Lua\nlowpass_hz=3000\n"},
		{"invalid.gvox", "version=2\nname=Rejected\n"},
	};
	if (registry.ReloadFromMemory(memoryFiles) != 1 || registry.Errors().size() != 1)
		return Fail("memory-backed reload did not validate Lua-provided files");
	if (registry.CustomPresets().front().name != "Injected from Lua" ||
		registry.Directory() != "DATA:gvox/presets")
		return Fail("memory-backed preset metadata is incorrect");

	std::cout << "preset registry test passed; disk_id=" << stableId
		<< ", memory_id=" << registry.CustomPresets().front().id << '\n';
	return 0;
}
