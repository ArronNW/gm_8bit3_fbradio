#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace AudioEffects {

enum EffectId {
	EFF_NONE = 0,
	EFF_BITCRUSH = 1,
	EFF_DESAMPLE = 2,
	EFF_COMB = 3,
	EFF_DARTHVADER = 4,
	EFF_RADIO = 5,
	EFF_ROBOT = 6,
	EFF_ALIEN = 7,
	EFF_OVERDRIVE = 8,
	EFF_DISTORTION = 9,
	EFF_TELEPHONE = 10,
	EFF_MEGAPHONE = 11,
	EFF_CHIPMUNK = 12,
	EFF_SLOWMOTION = 13,
	EFF_COMBO = 14,
	CUSTOM_ID_MIN = 1000,
	CUSTOM_ID_MAX = 65534
};

struct Preset {
	int id = EFF_NONE;
	std::string key;
	std::string name;
	std::string description;
	std::string sourceFile;
	bool custom = false;

	float gain = 1.0f;
	float highpassHz = 0.0f;
	float lowpassHz = 0.0f;
	float drive = 1.0f;
	float clip = 1.0f;
	float pitch = 1.0f;
	float ringFrequency = 0.0f;
	float ringMix = 0.0f;
	float echoDelayMs = 0.0f;
	float echoMix = 0.0f;
	float noise = 0.0f;
	int quantize = 0;
	int downsample = 1;
	bool softClip = false;

	// ---- Chaîne « radio » (radio_model=1) : rendu d'une radio portative numérique ----
	// Filtres Butterworth raides (biquads), égalisation, saturation douce, grain du codec (mu-law),
	// erreurs de trame du vocodeur, craquements, souffle limité à la bande, porte de bruit.
	// Aucune réverbération, aucune modulation, aucun rééchantillonnage.
	int radioModel = 0;
	int highpassOrder = 4;        // ordre pair, 2 à 16
	int lowpassOrder = 8;
	float lowEqHz = 0.0f, lowEqDb = 0.0f, lowEqQ = 0.7f;
	float presenceHz = 0.0f, presenceDb = 0.0f, presenceQ = 1.0f;
	int mulawBits = 0;            // 0 = désactivé ; 8 = léger grain, 5 = très granuleux
	float glitch = 0.0f;          // probabilité qu'une trame de 20 ms soit abîmée (erreur du vocodeur)
	float glitchMix = 0.5f;
	float clicks = 0.0f;          // craquements par seconde pendant la parole
	float gateDb = -90.0f;        // sous ce niveau : silence (comme la radio entre les mots)
};

struct RuntimeState {
	float highpassInput = 0.0f;
	float highpassOutput = 0.0f;
	float lowpassOutput = 0.0f;
	float ringPhase = 0.0f;
	float pitchPhase = 0.0f;
	int heldSample = 0;
	int holdRemaining = 0;
	uint32_t randomState = 0x8B17C3A5u;
	std::vector<int16_t> delay;
	size_t delayPosition = 0;

	// chaîne radio
	struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
	Biquad hp[8], lp[8], eqLow, eqPresence;
	int hpStages = 0, lpStages = 0;
	uint32_t radioSignature = 0;
	float envelope = 0.0f, gateGain = 0.0f, clickEnv = 0.0f, clickSign = 1.0f;
	int frameCounter = 0;
	bool frameGlitch = false;
	float glitchLevel = 0.0f;
};

inline int16_t Clamp16(float value) {
	if (value > 32767.0f) return 32767;
	if (value < -32768.0f) return -32768;
	return static_cast<int16_t>(value);
}

inline float ClampFloat(float value, float low, float high) {
	return std::max(low, std::min(high, value));
}

inline Preset MakeBuiltin(int id, float legacyGain, int legacyCrush, int legacyDesample) {
	Preset p;
	p.id = id;
	p.custom = false;

	switch (id) {
	case EFF_NONE:
		p.key = "none"; p.name = "No effect"; p.description = "Unmodified voice";
		break;
	case EFF_BITCRUSH:
		p.key = "bitcrush"; p.name = "Bit crusher"; p.description = "Driven and quantized digital voice";
		p.gain = legacyGain; p.drive = 2.4f; p.clip = 0.78f; p.quantize = std::max(8, legacyCrush); p.softClip = true;
		break;
	case EFF_DESAMPLE:
		p.key = "desample"; p.name = "Desampling"; p.description = "Low fidelity radio-like resampling";
		p.highpassHz = 700.0f; p.lowpassHz = 3600.0f; p.downsample = std::max(1, legacyDesample); p.quantize = 220;
		break;
	case EFF_COMB:
		p.key = "combine"; p.name = "Combine"; p.description = "Dark metallic Combine voice";
		p.pitch = 0.78f; p.highpassHz = 250.0f; p.lowpassHz = 2700.0f; p.drive = 1.8f; p.clip = 0.72f;
		p.ringFrequency = 150.0f; p.ringMix = 0.38f; p.echoDelayMs = 2.1f; p.echoMix = 0.28f; p.quantize = 150;
		break;
	case EFF_DARTHVADER:
		p.key = "darth_vader"; p.name = "Darth Vader"; p.description = "Deep resonant voice";
		p.pitch = 0.70f; p.lowpassHz = 2400.0f; p.ringFrequency = 72.0f; p.ringMix = 0.16f;
		p.echoDelayMs = 4.0f; p.echoMix = 0.18f; p.drive = 1.35f; p.softClip = true;
		break;
	case EFF_RADIO:
		p.key = "radio"; p.name = "Radio"; p.description = "Narrow-band walkie-talkie voice";
		p.highpassHz = 650.0f; p.lowpassHz = 3500.0f; p.drive = 1.35f; p.clip = 0.82f;
		p.quantize = 320; p.downsample = 2; p.noise = 0.008f; p.echoDelayMs = 1.2f; p.echoMix = 0.12f;
		break;
	case EFF_ROBOT:
		p.key = "robot"; p.name = "Robot"; p.description = "Synthetic ring-modulated voice";
		p.pitch = 0.76f; p.highpassHz = 180.0f; p.lowpassHz = 4200.0f; p.ringFrequency = 108.0f;
		p.ringMix = 0.62f; p.echoDelayMs = 3.0f; p.echoMix = 0.34f; p.drive = 1.4f; p.softClip = true;
		break;
	case EFF_ALIEN:
		p.key = "alien"; p.name = "Alien"; p.description = "High and unstable extraterrestrial voice";
		p.pitch = 1.32f; p.highpassHz = 550.0f; p.ringFrequency = 11.0f; p.ringMix = 0.22f;
		p.echoDelayMs = 0.8f; p.echoMix = 0.18f;
		break;
	case EFF_OVERDRIVE:
		p.key = "overdrive"; p.name = "Overdrive"; p.description = "Warm saturated amplifier";
		p.drive = 5.0f; p.gain = 0.92f; p.softClip = true; p.echoDelayMs = 0.5f; p.echoMix = 0.16f;
		break;
	case EFF_DISTORTION:
		p.key = "distortion"; p.name = "Distortion"; p.description = "Hard clipped distortion";
		p.drive = 5.2f; p.clip = 0.68f; p.softClip = false;
		break;
	case EFF_TELEPHONE:
		p.key = "telephone"; p.name = "Telephone"; p.description = "Telephone band-limited voice";
		p.highpassHz = 500.0f; p.lowpassHz = 3200.0f; p.drive = 1.25f; p.clip = 0.86f;
		p.quantize = 380; p.downsample = 2; p.noise = 0.004f; p.echoDelayMs = 0.7f; p.echoMix = 0.13f;
		break;
	case EFF_MEGAPHONE:
		p.key = "megaphone"; p.name = "Megaphone"; p.description = "Public-address loudspeaker voice";
		p.highpassHz = 620.0f; p.lowpassHz = 2700.0f; p.drive = 3.0f; p.clip = 0.72f;
		p.echoDelayMs = 1.0f; p.echoMix = 0.28f; p.quantize = 100;
		break;
	case EFF_CHIPMUNK:
		p.key = "chipmunk"; p.name = "Chipmunk"; p.description = "Raised pitch voice";
		p.pitch = 1.40f; p.highpassHz = 380.0f;
		break;
	case EFF_SLOWMOTION:
		p.key = "slow_motion"; p.name = "Slow motion"; p.description = "Lowered pitch voice";
		p.pitch = 0.68f; p.lowpassHz = 3600.0f;
		break;
	case EFF_COMBO:
		p.key = "metrocop"; p.name = "Metrocop"; p.description = "Civil Protection combined effect";
		p.pitch = 0.84f; p.highpassHz = 300.0f; p.lowpassHz = 3500.0f; p.drive = 1.65f; p.clip = 0.76f;
		p.ringFrequency = 300.0f; p.ringMix = 0.34f; p.echoDelayMs = 1.9f; p.echoMix = 0.30f; p.quantize = 180;
		break;
	default:
		p.id = EFF_NONE; p.key = "none"; p.name = "No effect"; p.description = "Unmodified voice";
		break;
	}

	return p;
}

inline void ApplyPitch(int16_t* audio, int samples, float ratio, RuntimeState& state) {
	if (samples < 2 || std::fabs(ratio - 1.0f) < 0.001f) return;

	std::vector<int16_t> original(audio, audio + samples);
	float position = state.pitchPhase;
	for (int i = 0; i < samples; ++i) {
		while (position >= static_cast<float>(samples - 1)) position -= static_cast<float>(samples - 1);
		const int a = static_cast<int>(position);
		const int b = std::min(a + 1, samples - 1);
		const float fraction = position - static_cast<float>(a);
		audio[i] = Clamp16(original[a] * (1.0f - fraction) + original[b] * fraction);
		position += ratio;
	}
	state.pitchPhase = std::fmod(position, static_cast<float>(samples - 1));
}

namespace Radio {

typedef RuntimeState::Biquad Biquad;

inline void Design(Biquad& q, int type, float f, float Q, float gainDb, float sampleRate) {
	const float pi = 3.14159265358979323846f;
	f = ClampFloat(f, 10.0f, sampleRate * 0.45f);
	const float w = 2.0f * pi * f / sampleRate, c = std::cos(w), s = std::sin(w), al = s / (2.0f * Q);
	const float A = std::pow(10.0f, gainDb / 40.0f);
	float b0, b1, b2, a0, a1, a2;
	if (type == 0) { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; }       // passe-bas
	else if (type == 1) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; } // passe-haut
	else { b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; }        // cloche
	q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
}

inline float Run(Biquad& q, float x) {
	const float y = q.b0 * x + q.z1;
	q.z1 = q.b1 * x - q.a1 * y + q.z2;
	q.z2 = q.b2 * x - q.a2 * y;
	return y;
}

// Butterworth d'ordre pair = cascade de biquads aux Q de Butterworth
inline int DesignButterworth(Biquad* stages, int type, float f, int order, float sampleRate) {
	order = std::max(2, std::min(16, order & ~1));
	const int n = order / 2;
	const float pi = 3.14159265358979323846f;
	for (int k = 0; k < n; ++k) {
		const float Q = 1.0f / (2.0f * std::cos(pi * (2.0f * k + 1.0f) / (2.0f * order)));
		Design(stages[k], type, f, Q, 0.0f, sampleRate);
	}
	return n;
}

inline uint32_t Signature(const Preset& p, int sampleRate) {
	const float v[] = { p.highpassHz, p.lowpassHz, static_cast<float>(p.highpassOrder), static_cast<float>(p.lowpassOrder),
		p.lowEqHz, p.lowEqDb, p.lowEqQ, p.presenceHz, p.presenceDb, p.presenceQ, static_cast<float>(sampleRate) };
	uint32_t h = 2166136261u;
	for (float f : v) { uint32_t b; std::memcpy(&b, &f, sizeof(b)); h ^= b; h *= 16777619u; }
	return h;
}

inline float Random(RuntimeState& st) {   // -1..1
	st.randomState ^= st.randomState << 13;
	st.randomState ^= st.randomState >> 17;
	st.randomState ^= st.randomState << 5;
	return static_cast<float>(st.randomState & 0xFFFFFFu) / 8388607.5f - 1.0f;
}

// mu-law (G.711) sur N bits : grain qui suit la voix et disparaît dans les silences
inline float MuLaw(float x, int bits) {
	const float mu = 255.0f;
	const float sign = x < 0.0f ? -1.0f : 1.0f;
	const float y = sign * std::log1p(mu * std::min(1.0f, std::fabs(x))) / std::log1p(mu);
	const float levels = static_cast<float>((1 << (bits - 1)) - 1);
	const float q = std::round(y * levels) / levels;
	const float qs = q < 0.0f ? -1.0f : 1.0f;
	return qs * (std::pow(1.0f + mu, std::fabs(q)) - 1.0f) / mu;
}

inline void Process(const Preset& p, int16_t* audio, int samples, int sampleRate, RuntimeState& st) {
	const float sr = static_cast<float>(sampleRate);
	const uint32_t sig = Signature(p, sampleRate);
	if (st.radioSignature != sig) {
		st.radioSignature = sig;
		st.hpStages = p.highpassHz > 0.0f ? DesignButterworth(st.hp, 1, p.highpassHz, p.highpassOrder, sr) : 0;
		st.lpStages = p.lowpassHz > 0.0f ? DesignButterworth(st.lp, 0, p.lowpassHz, p.lowpassOrder, sr) : 0;
		Design(st.eqLow, 2, p.lowEqHz > 0.0f ? p.lowEqHz : 300.0f, std::max(0.2f, p.lowEqQ), p.lowEqHz > 0.0f ? p.lowEqDb : 0.0f, sr);
		Design(st.eqPresence, 2, p.presenceHz > 0.0f ? p.presenceHz : 2000.0f, std::max(0.2f, p.presenceQ), p.presenceHz > 0.0f ? p.presenceDb : 0.0f, sr);
	}
	const float drive = ClampFloat(p.drive, 0.05f, 16.0f);
	const float clip = ClampFloat(p.clip, 0.05f, 1.0f);
	const float gain = ClampFloat(p.gain, 0.0f, 8.0f);
	const float noise = ClampFloat(p.noise, 0.0f, 0.25f);
	const int bits = p.mulawBits > 0 ? std::max(3, std::min(12, p.mulawBits)) : 0;
	const int frame = std::max(1, sampleRate / 50);                  // trame de 20 ms
	const float envAtk = 1.0f - std::exp(-1.0f / (0.002f * sr));
	const float envRel = 1.0f - std::exp(-1.0f / (0.060f * sr));
	const float gateTh = std::pow(10.0f, p.gateDb / 20.0f);
	const float gateAtk = 1.0f - std::exp(-1.0f / (0.003f * sr));
	const float gateRel = 1.0f - std::exp(-1.0f / (0.080f * sr));
	const float clickP = ClampFloat(p.clicks, 0.0f, 100.0f) / sr;
	const float k = drive * 0.5f;                                     // tanh(k x) / k : pente 1 au repos

	for (int i = 0; i < samples; ++i) {
		float x = static_cast<float>(audio[i]) / 32768.0f;
		const float ax = std::fabs(x);
		st.envelope += (ax > st.envelope ? envAtk : envRel) * (ax - st.envelope);
		const bool speaking = st.envelope > gateTh * 2.0f;

		// 1. bande du haut-parleur (coupe-bas raide) + égalisation
		for (int j = 0; j < st.hpStages; ++j) x = Run(st.hp[j], x);
		x = Run(st.eqLow, x);
		x = Run(st.eqPresence, x);

		// 2. saturation douce : pente ~1 au repos, les pics s'arrondissent puis sont écrêtés
		if (drive > 1.001f) x = std::tanh(x * k) / k;
		else if (p.softClip) x = std::tanh(x);
		x = ClampFloat(x, -clip, clip);

		// 3. erreur de trame du vocodeur : la trame garde son énergie mais perd sa netteté
		if (st.frameCounter <= 0) {
			st.frameCounter = frame;
			st.frameGlitch = speaking && p.glitch > 0.0f && (Random(st) * 0.5f + 0.5f) < p.glitch;
			st.glitchLevel = st.envelope;
		}
		--st.frameCounter;
		if (st.frameGlitch) {
			const float m = ClampFloat(p.glitchMix, 0.0f, 1.0f);
			x = x * (1.0f - m) + Random(st) * st.glitchLevel * 1.2f * m;
		}

		// 4. craquements brefs pendant la parole
		if (speaking && st.clickEnv < 0.001f && (Random(st) * 0.5f + 0.5f) < clickP) {
			st.clickEnv = 0.25f + 0.25f * (Random(st) * 0.5f + 0.5f);
			st.clickSign = Random(st) > 0.0f ? 1.0f : -1.0f;
		}
		if (st.clickEnv >= 0.001f) { x += st.clickEnv * st.clickSign * Random(st); st.clickEnv *= 0.90f; }

		// 5. souffle (limité à la bande par le filtre suivant)
		if (noise > 0.0f) x += Random(st) * noise;

		// 6. grain du codec numérique
		if (bits > 0) x = MuLaw(x, bits);

		// 7. coupe-haut raide : bande d'une radio numérique (~8 kHz d'échantillonnage)
		for (int j = 0; j < st.lpStages; ++j) x = Run(st.lp[j], x);

		// 8. porte : silence entre les mots, comme une radio numérique
		const float g = st.envelope >= gateTh ? 1.0f : 0.0f;
		st.gateGain += (g > st.gateGain ? gateAtk : gateRel) * (g - st.gateGain);
		x *= st.gateGain * gain;

		audio[i] = Clamp16(x * 32767.0f);
	}
}

} // namespace Radio

inline void ApplyPreset(const Preset& preset, int16_t* audio, int samples, int sampleRate, RuntimeState& state) {
	if (audio == nullptr || samples <= 0 || preset.id == EFF_NONE) return;
	const float pi = 3.14159265358979323846f;

	if (preset.radioModel > 0) {
		ApplyPitch(audio, samples, ClampFloat(preset.pitch, 0.5f, 2.0f), state);
		Radio::Process(preset, audio, samples, sampleRate, state);
		return;
	}

	ApplyPitch(audio, samples, ClampFloat(preset.pitch, 0.5f, 2.0f), state);

	if (preset.highpassHz > 0.0f) {
		const float cutoff = ClampFloat(preset.highpassHz, 10.0f, sampleRate * 0.45f);
		const float rc = 1.0f / (2.0f * pi * cutoff);
		const float dt = 1.0f / static_cast<float>(sampleRate);
		const float alpha = rc / (rc + dt);
		for (int i = 0; i < samples; ++i) {
			const float input = static_cast<float>(audio[i]);
			state.highpassOutput = alpha * (state.highpassOutput + input - state.highpassInput);
			state.highpassInput = input;
			audio[i] = Clamp16(state.highpassOutput);
		}
	}

	if (preset.lowpassHz > 0.0f) {
		const float cutoff = ClampFloat(preset.lowpassHz, 20.0f, sampleRate * 0.48f);
		const float rc = 1.0f / (2.0f * pi * cutoff);
		const float dt = 1.0f / static_cast<float>(sampleRate);
		const float alpha = dt / (rc + dt);
		for (int i = 0; i < samples; ++i) {
			state.lowpassOutput += alpha * (static_cast<float>(audio[i]) - state.lowpassOutput);
			audio[i] = Clamp16(state.lowpassOutput);
		}
	}

	if (preset.ringFrequency > 0.0f && preset.ringMix > 0.0f) {
		const float mix = ClampFloat(preset.ringMix, 0.0f, 1.0f);
		const float step = 2.0f * pi * preset.ringFrequency / static_cast<float>(sampleRate);
		for (int i = 0; i < samples; ++i) {
			const float dry = static_cast<float>(audio[i]);
			const float modulated = dry * std::sin(state.ringPhase);
			audio[i] = Clamp16(dry * (1.0f - mix) + modulated * mix);
			state.ringPhase += step;
			if (state.ringPhase >= 2.0f * pi) state.ringPhase -= 2.0f * pi;
		}
	}

	const int downsample = std::max(1, std::min(32, preset.downsample));
	if (downsample > 1) {
		for (int i = 0; i < samples; ++i) {
			if (state.holdRemaining <= 0) {
				state.heldSample = audio[i];
				state.holdRemaining = downsample;
			}
			audio[i] = static_cast<int16_t>(state.heldSample);
			--state.holdRemaining;
		}
	}

	if (preset.drive > 1.001f || preset.clip < 0.999f) {
		const float drive = ClampFloat(preset.drive, 0.05f, 16.0f);
		const float clip = ClampFloat(preset.clip, 0.05f, 1.0f);
		for (int i = 0; i < samples; ++i) {
			float normalized = static_cast<float>(audio[i]) / 32768.0f;
			normalized *= drive;
			if (preset.softClip) normalized = std::tanh(normalized);
			normalized = ClampFloat(normalized, -clip, clip);
			audio[i] = Clamp16(normalized * 32767.0f);
		}
	}

	if (preset.quantize > 1) {
		const int step = std::min(32767, preset.quantize);
		for (int i = 0; i < samples; ++i) {
			const int value = audio[i];
			audio[i] = static_cast<int16_t>((value / step) * step);
		}
	}

	if (preset.echoDelayMs > 0.0f && preset.echoMix > 0.0f) {
		const int wantedDelay = std::max(1, std::min(sampleRate / 2,
			static_cast<int>(preset.echoDelayMs * static_cast<float>(sampleRate) / 1000.0f)));
		if (state.delay.size() != static_cast<size_t>(wantedDelay)) {
			state.delay.assign(static_cast<size_t>(wantedDelay), 0);
			state.delayPosition = 0;
		}
		const float mix = ClampFloat(preset.echoMix, 0.0f, 0.95f);
		for (int i = 0; i < samples; ++i) {
			const int16_t delayed = state.delay[state.delayPosition];
			const int16_t dry = audio[i];
			audio[i] = Clamp16(static_cast<float>(dry) + static_cast<float>(delayed) * mix);
			state.delay[state.delayPosition] = dry;
			state.delayPosition = (state.delayPosition + 1) % state.delay.size();
		}
	}

	const float noiseAmount = ClampFloat(preset.noise, 0.0f, 0.25f);
	const float gain = ClampFloat(preset.gain, 0.0f, 8.0f);
	if (noiseAmount > 0.0f || std::fabs(gain - 1.0f) > 0.001f) {
		for (int i = 0; i < samples; ++i) {
			state.randomState ^= state.randomState << 13;
			state.randomState ^= state.randomState >> 17;
			state.randomState ^= state.randomState << 5;
			const float noise = (static_cast<float>(state.randomState & 0xFFFFu) / 32767.5f - 1.0f)
				* noiseAmount * 32767.0f;
			audio[i] = Clamp16(static_cast<float>(audio[i]) * gain + noise);
		}
	}
}

} // namespace AudioEffects
