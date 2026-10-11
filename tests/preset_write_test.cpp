#include "voice_presets.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int Fail(const std::string& message) {
	std::cerr << message << '\n';
	return 1;
}

} // namespace

int main() {
	VoicePresetRegistry registry;
	const std::string content =
		"version=1\n"
		"name=Created by addon test\n"
		"lowpass_hz=3200\n";
	std::string path;
	std::string error;

	if (!registry.WritePresetFile("addon_test.gvox", content, path, error))
		return Fail("valid preset was not written: " + error);

	std::ifstream input(path.c_str(), std::ios::binary);
	if (!input) return Fail("written preset cannot be opened");
	std::ostringstream stored;
	stored << input.rdbuf();
	if (stored.str() != content) return Fail("written preset content differs from the input");

	if (registry.WritePresetFile("../escape.gvox", content, path, error))
		return Fail("unsafe preset filename was accepted");
	if (registry.WritePresetFile("broken.gvox", "version=2\n", path, error))
		return Fail("invalid preset content was accepted");

	std::cout << "preset writer test passed\n";
	return 0;
}
