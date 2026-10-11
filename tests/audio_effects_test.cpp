#include "audio_effects.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr int kSamples = 960;
constexpr int16_t kLeftGuard = 12345;
constexpr int16_t kRightGuard = -23456;

bool Exercise(const AudioEffects::Preset& preset) {
	AudioEffects::RuntimeState state;
	for (int packet = 0; packet < 40; ++packet) {
		std::vector<int16_t> guarded(kSamples + 2);
		guarded.front() = kLeftGuard;
		guarded.back() = kRightGuard;
		for (int i = 0; i < kSamples; ++i) {
			const float phase = static_cast<float>(packet * kSamples + i) * 0.071f;
			guarded[static_cast<size_t>(i) + 1] = static_cast<int16_t>(std::sin(phase) * 22000.0f);
		}

		AudioEffects::ApplyPreset(preset, guarded.data() + 1, kSamples, 24000, state);
		if (guarded.front() != kLeftGuard || guarded.back() != kRightGuard) return false;
	}
	return true;
}

} // namespace

int main() {
	for (int id = AudioEffects::EFF_NONE; id <= AudioEffects::EFF_COMBO; ++id) {
		if (!Exercise(AudioEffects::MakeBuiltin(id, 1.2f, 350, 2))) {
			std::cerr << "guard corruption in built-in effect " << id << '\n';
			return 1;
		}
	}

	AudioEffects::Preset custom;
	custom.id = AudioEffects::CUSTOM_ID_MIN;
	custom.pitch = 1.35f;
	custom.highpassHz = 400.0f;
	custom.lowpassHz = 4200.0f;
	custom.drive = 2.0f;
	custom.clip = 0.75f;
	custom.ringFrequency = 160.0f;
	custom.ringMix = 0.4f;
	custom.echoDelayMs = 12.0f;
	custom.echoMix = 0.35f;
	custom.noise = 0.02f;
	custom.quantize = 200;
	custom.downsample = 3;
	custom.softClip = true;
	if (!Exercise(custom)) {
		std::cerr << "guard corruption in custom effect\n";
		return 1;
	}

	std::cout << "audio effects test passed\n";
	return 0;
}
