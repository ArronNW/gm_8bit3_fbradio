#pragma once

#include "audio_effects.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class VoicePresetRegistry {
public:
	VoicePresetRegistry();

	int Reload(const std::string& requestedDirectory = std::string());
	int ReloadFromMemory(const std::vector<std::pair<std::string, std::string>>& files,
		const std::string& sourceLabel = "DATA:gvox/presets");
	bool WritePresetFile(const std::string& fileName, const std::string& content,
		std::string& storedPath, std::string& error);
	bool Find(int id, AudioEffects::Preset& result) const;
	bool IsCustom(int id) const;
	const std::vector<AudioEffects::Preset>& CustomPresets() const;
	const std::vector<std::string>& Errors() const;
	const std::string& Directory() const;

private:
	static std::string DefaultDirectory();
	static std::vector<std::string> ListPresetFiles(const std::string& directory);
	static bool ParseFile(const std::string& fullPath, const std::string& fileName,
		AudioEffects::Preset& preset, std::string& error);
	static bool ParseContent(const std::string& content, const std::string& fileName,
		AudioEffects::Preset& preset, std::string& error);
	static int StableId(const std::string& fileName, const std::unordered_map<int, size_t>& occupied);

	std::string directory_;
	std::vector<AudioEffects::Preset> custom_;
	std::unordered_map<int, size_t> byId_;
	std::vector<std::string> errors_;
};
