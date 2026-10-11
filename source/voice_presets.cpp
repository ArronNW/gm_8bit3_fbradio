#include "voice_presets.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace {

std::string Trim(const std::string& input) {
	size_t begin = 0;
	while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
	size_t end = input.size();
	while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
	return input.substr(begin, end - begin);
}

std::string Lower(std::string input) {
	std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return input;
}

bool EndsWithPresetExtension(const std::string& fileName) {
	const std::string lower = Lower(fileName);
	return lower.size() >= 5 && lower.substr(lower.size() - 5) == ".gvox";
}

bool DirectoryExists(const std::string& path) {
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesA(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	struct stat info;
	return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

bool CreateDirectoryIfMissing(const std::string& path) {
	if (DirectoryExists(path)) return true;
#ifdef _WIN32
	return CreateDirectoryA(path.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
	return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

bool IsSafePresetFileName(const std::string& fileName) {
	if (fileName.size() <= 5 || fileName.size() > 128 || !EndsWithPresetExtension(fileName)) return false;
	if (fileName.front() == '.') return false;
	for (unsigned char c : fileName) {
		const bool alphaNumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9');
		if (!alphaNumeric && c != '_' && c != '-' && c != '.') return false;
	}
	return fileName.find("..") == std::string::npos;
}

bool ParseFloat(const std::string& text, float& value) {
	char* end = nullptr;
	errno = 0;
	const float parsed = std::strtof(text.c_str(), &end);
	if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
	value = parsed;
	return true;
}

bool ParseInt(const std::string& text, int& value) {
	char* end = nullptr;
	errno = 0;
	const long parsed = std::strtol(text.c_str(), &end, 10);
	if (errno != 0 || end == text.c_str() || *end != '\0' ||
		parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) return false;
	value = static_cast<int>(parsed);
	return true;
}

bool ParseBool(const std::string& text, bool& value) {
	const std::string lower = Lower(Trim(text));
	if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") { value = true; return true; }
	if (lower == "0" || lower == "false" || lower == "no" || lower == "off") { value = false; return true; }
	return false;
}

std::string JoinPath(const std::string& directory, const std::string& fileName) {
	if (directory.empty()) return fileName;
	const char tail = directory[directory.size() - 1];
	if (tail == '/' || tail == '\\') return directory + fileName;
	return directory + "/" + fileName;
}

} // namespace

VoicePresetRegistry::VoicePresetRegistry() : directory_(DefaultDirectory()) {}

std::string VoicePresetRegistry::DefaultDirectory() {
	const char* candidates[] = {
		"data/gvox/presets",
		"garrysmod/data/gvox/presets"
	};
	for (const char* candidate : candidates) {
		if (DirectoryExists(candidate) && !ListPresetFiles(candidate).empty()) return candidate;
	}
	for (const char* candidate : candidates) {
		if (DirectoryExists(candidate)) return candidate;
	}
	return candidates[0];
}

std::vector<std::string> VoicePresetRegistry::ListPresetFiles(const std::string& directory) {
	std::vector<std::string> result;
#ifdef _WIN32
	WIN32_FIND_DATAA data;
	const std::string pattern = JoinPath(directory, "*.gvox");
	HANDLE handle = FindFirstFileA(pattern.c_str(), &data);
	if (handle != INVALID_HANDLE_VALUE) {
		do {
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && EndsWithPresetExtension(data.cFileName)) {
				result.emplace_back(data.cFileName);
			}
		} while (FindNextFileA(handle, &data));
		FindClose(handle);
	}
#else
	DIR* handle = opendir(directory.c_str());
	if (handle != nullptr) {
		while (dirent* entry = readdir(handle)) {
			const std::string name(entry->d_name);
			if (name != "." && name != ".." && EndsWithPresetExtension(name)) result.push_back(name);
		}
		closedir(handle);
	}
#endif
	std::sort(result.begin(), result.end());
	if (result.size() > 128) result.resize(128);
	return result;
}

int VoicePresetRegistry::StableId(const std::string& fileName, const std::unordered_map<int, size_t>& occupied) {
	uint32_t hash = 2166136261u;
	const std::string normalized = Lower(fileName);
	for (unsigned char c : normalized) {
		hash ^= c;
		hash *= 16777619u;
	}
	const int range = AudioEffects::CUSTOM_ID_MAX - AudioEffects::CUSTOM_ID_MIN + 1;
	int candidate = AudioEffects::CUSTOM_ID_MIN + static_cast<int>(hash % static_cast<uint32_t>(range));
	while (occupied.find(candidate) != occupied.end()) {
		++candidate;
		if (candidate > AudioEffects::CUSTOM_ID_MAX) candidate = AudioEffects::CUSTOM_ID_MIN;
	}
	return candidate;
}

bool VoicePresetRegistry::ParseFile(const std::string& fullPath, const std::string& fileName,
	AudioEffects::Preset& preset, std::string& error) {
	std::ifstream input(fullPath.c_str(), std::ios::binary);
	if (!input) { error = "cannot open file"; return false; }

	input.seekg(0, std::ios::end);
	const std::streamoff size = input.tellg();
	if (size <= 0 || size > 65536) { error = "file is empty or larger than 64 KiB"; return false; }
	input.seekg(0, std::ios::beg);
	std::string content(static_cast<size_t>(size), '\0');
	input.read(&content[0], size);
	if (!input) { error = "cannot read file"; return false; }
	return ParseContent(content, fileName, preset, error);
}

bool VoicePresetRegistry::ParseContent(const std::string& content, const std::string& fileName,
	AudioEffects::Preset& preset, std::string& error) {
	if (content.empty() || content.size() > 65536) {
		error = "file is empty or larger than 64 KiB";
		return false;
	}
	if (!EndsWithPresetExtension(fileName) || fileName.size() <= 5 || fileName.find('/') != std::string::npos
		|| fileName.find('\\') != std::string::npos) {
		error = "invalid preset filename";
		return false;
	}
	std::istringstream input(content);

	preset = AudioEffects::Preset();
	preset.custom = true;
	preset.sourceFile = fileName;
	preset.key = Lower(fileName.substr(0, fileName.size() - 5));
	preset.name = preset.key;

	std::string line;
	int lineNumber = 0;
	bool hasVersion = false;
	while (std::getline(input, line)) {
		++lineNumber;
		if (lineNumber == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF
			&& static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
			line.erase(0, 3);
		}
		line = Trim(line);
		if (line.empty() || line[0] == '#' || line[0] == ';') continue;
		const size_t separator = line.find('=');
		if (separator == std::string::npos) {
			error = "line " + std::to_string(lineNumber) + ": expected key=value";
			return false;
		}

		const std::string key = Lower(Trim(line.substr(0, separator)));
		const std::string value = Trim(line.substr(separator + 1));
		bool ok = true;
		if (key == "version") {
			int version = 0; ok = ParseInt(value, version) && version == 1; hasVersion = ok;
		} else if (key == "name") preset.name = value;
		else if (key == "description") preset.description = value;
		else if (key == "key") preset.key = Lower(value);
		else if (key == "gain") ok = ParseFloat(value, preset.gain);
		else if (key == "highpass" || key == "highpass_hz") ok = ParseFloat(value, preset.highpassHz);
		else if (key == "lowpass" || key == "lowpass_hz") ok = ParseFloat(value, preset.lowpassHz);
		else if (key == "drive") ok = ParseFloat(value, preset.drive);
		else if (key == "clip") ok = ParseFloat(value, preset.clip);
		else if (key == "pitch") ok = ParseFloat(value, preset.pitch);
		else if (key == "ring_frequency" || key == "ring_frequency_hz") ok = ParseFloat(value, preset.ringFrequency);
		else if (key == "ring_mix") ok = ParseFloat(value, preset.ringMix);
		else if (key == "echo_delay" || key == "echo_delay_ms") ok = ParseFloat(value, preset.echoDelayMs);
		else if (key == "echo_mix") ok = ParseFloat(value, preset.echoMix);
		else if (key == "noise") ok = ParseFloat(value, preset.noise);
		else if (key == "quantize") ok = ParseInt(value, preset.quantize);
		else if (key == "downsample") ok = ParseInt(value, preset.downsample);
		else if (key == "soft_clip") ok = ParseBool(value, preset.softClip);
		else if (key == "radio_model") ok = ParseInt(value, preset.radioModel);
		else if (key == "highpass_order") ok = ParseInt(value, preset.highpassOrder);
		else if (key == "lowpass_order") ok = ParseInt(value, preset.lowpassOrder);
		else if (key == "low_eq_hz") ok = ParseFloat(value, preset.lowEqHz);
		else if (key == "low_eq_db") ok = ParseFloat(value, preset.lowEqDb);
		else if (key == "low_eq_q") ok = ParseFloat(value, preset.lowEqQ);
		else if (key == "presence_hz") ok = ParseFloat(value, preset.presenceHz);
		else if (key == "presence_db") ok = ParseFloat(value, preset.presenceDb);
		else if (key == "presence_q") ok = ParseFloat(value, preset.presenceQ);
		else if (key == "mulaw_bits") ok = ParseInt(value, preset.mulawBits);
		else if (key == "glitch") ok = ParseFloat(value, preset.glitch);
		else if (key == "glitch_mix") ok = ParseFloat(value, preset.glitchMix);
		else if (key == "clicks") ok = ParseFloat(value, preset.clicks);
		else if (key == "gate_db") ok = ParseFloat(value, preset.gateDb);
		else continue;

		if (!ok) {
			error = "line " + std::to_string(lineNumber) + ": invalid value for " + key;
			return false;
		}
	}

	if (!hasVersion) {
		error = "missing version=1";
		return false;
	}
	if (preset.name.empty() || preset.name.size() > 96 || preset.key.empty() || preset.key.size() > 64 ||
		preset.description.size() > 192) {
		error = "invalid name/key/description length";
		return false;
	}

	preset.gain = AudioEffects::ClampFloat(preset.gain, 0.0f, 8.0f);
	preset.highpassHz = AudioEffects::ClampFloat(preset.highpassHz, 0.0f, 11000.0f);
	preset.lowpassHz = AudioEffects::ClampFloat(preset.lowpassHz, 0.0f, 11000.0f);
	preset.drive = AudioEffects::ClampFloat(preset.drive, 0.05f, 16.0f);
	preset.clip = AudioEffects::ClampFloat(preset.clip, 0.05f, 1.0f);
	preset.pitch = AudioEffects::ClampFloat(preset.pitch, 0.5f, 2.0f);
	preset.ringFrequency = AudioEffects::ClampFloat(preset.ringFrequency, 0.0f, 8000.0f);
	preset.ringMix = AudioEffects::ClampFloat(preset.ringMix, 0.0f, 1.0f);
	preset.echoDelayMs = AudioEffects::ClampFloat(preset.echoDelayMs, 0.0f, 500.0f);
	preset.echoMix = AudioEffects::ClampFloat(preset.echoMix, 0.0f, 0.95f);
	preset.noise = AudioEffects::ClampFloat(preset.noise, 0.0f, 0.25f);
	preset.quantize = std::max(0, std::min(32767, preset.quantize));
	preset.downsample = std::max(1, std::min(32, preset.downsample));
	preset.radioModel = std::max(0, std::min(1, preset.radioModel));
	preset.highpassOrder = std::max(2, std::min(16, preset.highpassOrder));
	preset.lowpassOrder = std::max(2, std::min(16, preset.lowpassOrder));
	preset.lowEqHz = AudioEffects::ClampFloat(preset.lowEqHz, 0.0f, 11000.0f);
	preset.lowEqDb = AudioEffects::ClampFloat(preset.lowEqDb, -24.0f, 24.0f);
	preset.lowEqQ = AudioEffects::ClampFloat(preset.lowEqQ, 0.2f, 8.0f);
	preset.presenceHz = AudioEffects::ClampFloat(preset.presenceHz, 0.0f, 11000.0f);
	preset.presenceDb = AudioEffects::ClampFloat(preset.presenceDb, -24.0f, 24.0f);
	preset.presenceQ = AudioEffects::ClampFloat(preset.presenceQ, 0.2f, 8.0f);
	preset.mulawBits = std::max(0, std::min(12, preset.mulawBits));
	preset.glitch = AudioEffects::ClampFloat(preset.glitch, 0.0f, 0.5f);
	preset.glitchMix = AudioEffects::ClampFloat(preset.glitchMix, 0.0f, 1.0f);
	preset.clicks = AudioEffects::ClampFloat(preset.clicks, 0.0f, 50.0f);
	preset.gateDb = AudioEffects::ClampFloat(preset.gateDb, -120.0f, 0.0f);
	return true;
}

int VoicePresetRegistry::Reload(const std::string& requestedDirectory) {
	if (!requestedDirectory.empty()) directory_ = requestedDirectory;
	else directory_ = DefaultDirectory();

	custom_.clear();
	byId_.clear();
	errors_.clear();

	const std::vector<std::string> files = ListPresetFiles(directory_);
	for (const std::string& fileName : files) {
		AudioEffects::Preset preset;
		std::string error;
		if (!ParseFile(JoinPath(directory_, fileName), fileName, preset, error)) {
			errors_.push_back(fileName + ": " + error);
			continue;
		}
		preset.id = StableId(fileName, byId_);
		byId_[preset.id] = custom_.size();
		custom_.push_back(preset);
	}
	return static_cast<int>(custom_.size());
}

int VoicePresetRegistry::ReloadFromMemory(const std::vector<std::pair<std::string, std::string>>& files,
	const std::string& sourceLabel) {
	directory_ = sourceLabel;
	custom_.clear();
	byId_.clear();
	errors_.clear();

	std::vector<std::pair<std::string, std::string>> sorted = files;
	std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
		return left.first < right.first;
	});
	if (sorted.size() > 128) sorted.resize(128);

	for (const auto& file : sorted) {
		AudioEffects::Preset preset;
		std::string error;
		if (!ParseContent(file.second, file.first, preset, error)) {
			errors_.push_back(file.first + ": " + error);
			continue;
		}
		preset.id = StableId(file.first, byId_);
		byId_[preset.id] = custom_.size();
		custom_.push_back(preset);
	}
	return static_cast<int>(custom_.size());
}

bool VoicePresetRegistry::WritePresetFile(const std::string& fileName, const std::string& content,
	std::string& storedPath, std::string& error) {
	storedPath.clear();
	error.clear();
	if (!IsSafePresetFileName(fileName)) {
		error = "filename must contain only letters, digits, dots, underscores or dashes and end in .gvox";
		return false;
	}

	AudioEffects::Preset validated;
	if (!ParseContent(content, fileName, validated, error)) return false;

	std::string directory = DefaultDirectory();
	if (!DirectoryExists(directory)) {
		if (DirectoryExists("garrysmod/data")) {
			if (!CreateDirectoryIfMissing("garrysmod/data/gvox")
				|| !CreateDirectoryIfMissing("garrysmod/data/gvox/presets")) {
				error = "cannot create garrysmod/data/gvox/presets";
				return false;
			}
			directory = "garrysmod/data/gvox/presets";
		} else {
			if (!CreateDirectoryIfMissing("data") || !CreateDirectoryIfMissing("data/gvox")
				|| !CreateDirectoryIfMissing("data/gvox/presets")) {
				error = "cannot create data/gvox/presets";
				return false;
			}
			directory = "data/gvox/presets";
		}
	}

	storedPath = JoinPath(directory, fileName);
	{
		std::ifstream existing(storedPath.c_str(), std::ios::binary);
		if (existing) {
			std::ostringstream current;
			current << existing.rdbuf();
			if (existing.good() || existing.eof()) {
				if (current.str() == content) return true;
			}
		}
	}

	std::ofstream output(storedPath.c_str(), std::ios::binary | std::ios::trunc);
	if (!output) {
		error = "cannot open preset file for writing";
		storedPath.clear();
		return false;
	}
	output.write(content.data(), static_cast<std::streamsize>(content.size()));
	output.close();
	if (!output) {
		error = "cannot write preset file";
		storedPath.clear();
		return false;
	}
	return true;
}

bool VoicePresetRegistry::Find(int id, AudioEffects::Preset& result) const {
	const auto found = byId_.find(id);
	if (found == byId_.end()) return false;
	result = custom_[found->second];
	return true;
}

bool VoicePresetRegistry::IsCustom(int id) const { return byId_.find(id) != byId_.end(); }
const std::vector<AudioEffects::Preset>& VoicePresetRegistry::CustomPresets() const { return custom_; }
const std::vector<std::string>& VoicePresetRegistry::Errors() const { return errors_; }
const std::string& VoicePresetRegistry::Directory() const { return directory_; }
