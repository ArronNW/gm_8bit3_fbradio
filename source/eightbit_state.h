#pragma once

#include "audio_effects.h"
#include "opus_framedecoder.h"
#include "voice_presets.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

struct EffectEncoderStream {
	EffectEncoderStream() : codec(new SteamOpus::Opus_FrameDecoder()) {
		codec->Init(5, 24000);
	}

	std::unique_ptr<SteamOpus::Opus_FrameDecoder> codec;
	AudioEffects::RuntimeState runtime;
};

struct SpeakerVoiceState {
	SpeakerVoiceState() : decoder(new SteamOpus::Opus_FrameDecoder()) {
		decoder->Init(5, 24000);
	}

	int defaultEffect = AudioEffects::EFF_NONE;
	std::unique_ptr<SteamOpus::Opus_FrameDecoder> decoder;
	std::unordered_map<int, std::unique_ptr<EffectEncoderStream>> encoders;
	// FB Radio : flux réencodés au nom d'un autre joueur (haut-parleur en 3D), clé = (effet << 32) | source
	std::unordered_map<uint64_t, std::unique_ptr<EffectEncoderStream>> redirectEncoders;
};

struct EightbitState {
	static uint64_t RouteKey(int listenerUserId, int speakerUserId) {
		return (static_cast<uint64_t>(static_cast<uint32_t>(listenerUserId)) << 32)
			| static_cast<uint32_t>(speakerUserId);
	}

	int crushFactor = 350;
	float gainFactor = 1.2f;
	bool broadcastPackets = false;
	int desampleRate = 2;
	uint16_t port = 4000;
	std::string ip = "127.0.0.1";

	std::unordered_map<int, std::unique_ptr<SpeakerVoiceState>> speakers;
	std::unordered_map<uint64_t, int> receiverEffects;

	// FB Radio : pour une paire auditeur <- émetteur, la voix est envoyée comme si elle venait d'un autre
	// joueur (le porteur de la radio qui reçoit), en mode 3D : l'auditeur l'entend sortir de cette radio.
	struct Redirect { int sourceUserId = 0; uint64_t sourceSteamId = 0; };
	std::unordered_map<uint64_t, Redirect> redirects;
	std::unordered_map<int, double> lastVoice;   // dernière voix reçue de chaque joueur (secondes)
	VoicePresetRegistry presets;
};
