#define NO_MALLOC_OVERRIDE
#define NOMINMAX

#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/Symbol.hpp>
#include <detouring/hook.hpp>
#include <scanning/symbolfinder.hpp>

#include <iclient.h>
#include <inetmessage.h>
#include <inetchannelinfo.h>
#include <iserver.h>
#include <protocol.h>

#include "audio_effects.h"
#include "eightbit_state.h"
#include "net.h"
#include "steam_voice.h"
#include "thirdparty.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef SYSTEM_WINDOWS
#include <windows.h>
#endif

#define STEAM_PACKET_OVERHEAD (sizeof(uint64_t) + sizeof(CRC32_t))
#define MAX_VOICE_PACKET_BYTES 8191
#define AUDIO_BUFFER_BYTES (64 * 1024)

namespace {

Net* g_net = nullptr;
EightbitState* g_eightbit = nullptr;
bool g_voiceHookAvailable = false;

#if defined(SYSTEM_WINDOWS) && defined(ARCHITECTURE_X86)
using BroadcastVoiceFn = void(__fastcall*)(IClient*, int, char*, int64);
#else
using BroadcastVoiceFn = void (*)(IClient*, int, char*, int64);
#endif

Detouring::Hook g_broadcastHook;

class VoiceNetMessage final : public INetMessage {
public:
	VoiceNetMessage(int fromClient, bool proximity, const char* data, int bytes)
		: netChannel_(nullptr), reliable_(false), fromClient_(fromClient), proximity_(proximity),
		data_(data), bits_(bytes * 8) {}

	void SetNetChannel(INetChannel* netChannel) override { netChannel_ = netChannel; }
	void SetReliable(bool reliable) override { reliable_ = reliable; }
	bool Process() override { return false; }
	bool ReadFromBuffer(bf_read&) override { return false; }
	bool WriteToBuffer(bf_write& buffer) override {
		buffer.WriteUBitLong(svc_VoiceData, 6);
		buffer.WriteByte(fromClient_);
		buffer.WriteByte(proximity_ ? 1 : 0);
		buffer.WriteWord(bits_);
		return buffer.WriteBits(data_, bits_);
	}
	bool IsReliable() const override { return reliable_; }
	int GetType() const override { return svc_VoiceData; }
	int GetGroup() const override { return INetChannelInfo::VOICE; }
	const char* GetName() const override { return "svc_VoiceData"; }
	INetChannel* GetNetChannel() const override { return netChannel_; }
	const char* ToString() const override {
		static thread_local char text[96];
		std::snprintf(text, sizeof(text), "svc_VoiceData: client %d, bytes %d", fromClient_, bits_ / 8);
		return text;
	}

private:
	INetChannel* netChannel_;
	bool reliable_;
	int fromClient_;
	bool proximity_;
	const char* data_;
	int bits_;
};

bool ResolvePreset(int effectId, AudioEffects::Preset& preset) {
	if (effectId >= AudioEffects::EFF_NONE && effectId <= AudioEffects::EFF_COMBO) {
		preset = AudioEffects::MakeBuiltin(effectId, g_eightbit->gainFactor,
			g_eightbit->crushFactor, g_eightbit->desampleRate);
		return true;
	}
	return g_eightbit->presets.Find(effectId, preset);
}

bool HasRouteForSpeaker(int speakerUserId) {
	const uint32_t target = static_cast<uint32_t>(speakerUserId);
	for (const auto& route : g_eightbit->receiverEffects) {
		if (static_cast<uint32_t>(route.first & 0xFFFFFFFFu) == target) return true;
	}
	return false;
}

int EffectForReceiver(int listenerUserId, int speakerUserId, int defaultEffect) {
	const auto found = g_eightbit->receiverEffects.find(EightbitState::RouteKey(listenerUserId, speakerUserId));
	return found == g_eightbit->receiverEffects.end() ? defaultEffect : found->second;
}

SpeakerVoiceState& SpeakerState(int speakerUserId) {
	auto found = g_eightbit->speakers.find(speakerUserId);
	if (found == g_eightbit->speakers.end()) {
		std::unique_ptr<SpeakerVoiceState> created(new SpeakerVoiceState());
		found = g_eightbit->speakers.emplace(speakerUserId, std::move(created)).first;
	}
	return *found->second;
}

EffectEncoderStream& EncoderStream(SpeakerVoiceState& speaker, int effectId) {
	auto found = speaker.encoders.find(effectId);
	if (found == speaker.encoders.end()) {
		std::unique_ptr<EffectEncoderStream> created(new EffectEncoderStream());
		found = speaker.encoders.emplace(effectId, std::move(created)).first;
	}
	return *found->second;
}

struct ReceiverSelection {
	IClient* client = nullptr;
	int effect = AudioEffects::EFF_NONE;
	bool proximity = false;
};

bool BroadcastRouted(IClient* speakerClient, int nBytes, char* originalData) {
	if (speakerClient == nullptr || nBytes < static_cast<int>(STEAM_PACKET_OVERHEAD)
		|| nBytes > MAX_VOICE_PACKET_BYTES || originalData == nullptr) return false;

	IServer* server = speakerClient->GetServer();
	if (server == nullptr) return false;

	const int speakerSlot = speakerClient->GetPlayerSlot();
	const int speakerUserId = speakerClient->GetUserID();
	SpeakerVoiceState& speaker = SpeakerState(speakerUserId);
	std::vector<ReceiverSelection> receivers;
	bool needsModifiedAudio = false;

	const int clientCount = server->GetClientCount();
	for (int index = 0; index < clientCount; ++index) {
		IClient* receiver = server->GetClient(index);
		if (receiver == nullptr || !receiver->IsActive() || receiver->IsFakeClient()) continue;
		if (!receiver->IsHearingClient(speakerSlot)) continue;

		ReceiverSelection selected;
		selected.client = receiver;
		selected.effect = EffectForReceiver(receiver->GetUserID(), speakerUserId, speaker.defaultEffect);
		selected.proximity = receiver->IsProximityHearingClient(speakerSlot);

		AudioEffects::Preset check;
		if (selected.effect != AudioEffects::EFF_NONE && !ResolvePreset(selected.effect, check)) {
			selected.effect = AudioEffects::EFF_NONE;
		}
		needsModifiedAudio = needsModifiedAudio || selected.effect != AudioEffects::EFF_NONE;
		receivers.push_back(selected);
	}

	if (receivers.empty()) return true;

	std::unordered_map<int, std::vector<char>> encoded;
	if (needsModifiedAudio) {
		std::vector<char> pcm(AUDIO_BUFFER_BYTES);
		const int decodedBytes = SteamVoice::DecompressIntoBuffer(speaker.decoder.get(), originalData, nBytes,
			pcm.data(), static_cast<int>(pcm.size()));
		if (decodedBytes <= 0 || (decodedBytes & 1) != 0) return false;

		for (const ReceiverSelection& selected : receivers) {
			if (selected.effect == AudioEffects::EFF_NONE || encoded.find(selected.effect) != encoded.end()) continue;

			AudioEffects::Preset preset;
			if (!ResolvePreset(selected.effect, preset)) continue;
			std::vector<char> effected(pcm.begin(), pcm.begin() + decodedBytes);
			EffectEncoderStream& stream = EncoderStream(speaker, selected.effect);
			AudioEffects::ApplyPreset(preset, reinterpret_cast<int16_t*>(effected.data()), decodedBytes / 2,
				24000, stream.runtime);

			std::vector<char> packet(AUDIO_BUFFER_BYTES);
			uint64_t steamId = 0;
			std::memcpy(&steamId, originalData, sizeof(steamId));
			const int written = SteamVoice::CompressIntoBuffer(steamId, stream.codec.get(), effected.data(), decodedBytes,
				packet.data(), static_cast<int>(packet.size()), 24000);
			if (written > 0 && written <= MAX_VOICE_PACKET_BYTES) {
				packet.resize(static_cast<size_t>(written));
				encoded.emplace(selected.effect, std::move(packet));
			}
		}
	}

	for (const ReceiverSelection& selected : receivers) {
		const char* sendData = originalData;
		int sendBytes = nBytes;
		if (selected.effect != AudioEffects::EFF_NONE) {
			const auto variant = encoded.find(selected.effect);
			if (variant != encoded.end()) {
				sendData = variant->second.data();
				sendBytes = static_cast<int>(variant->second.size());
			}
		}
		VoiceNetMessage message(speakerSlot, selected.proximity, sendData, sendBytes);
		selected.client->SendNetMsg(message, false);
	}

	return true;
}

#if defined(SYSTEM_WINDOWS) && defined(ARCHITECTURE_X86)
extern "C" void __fastcall HookBroadcastVoice(IClient* client, int nBytes, char* data, int64 xuid) {
#else
extern "C" void HookBroadcastVoice(IClient* client, int nBytes, char* data, int64 xuid) {
#endif
	BroadcastVoiceFn original = g_broadcastHook.GetTrampoline<BroadcastVoiceFn>();
	if (!g_voiceHookAvailable || client == nullptr || g_eightbit == nullptr) {
		return original(client, nBytes, data, xuid);
	}

#ifdef THIRDPARTY_LINK
	if (checkIfMuted(client->GetPlayerSlot() + 1)) return original(client, nBytes, data, xuid);
#endif

	if (g_eightbit->broadcastPackets && g_net != nullptr && data != nullptr && nBytes > 0) {
		g_net->SendPacket(g_eightbit->ip.c_str(), g_eightbit->port, data, static_cast<uint32_t>(nBytes));
	}

	const int speakerUserId = client->GetUserID();
	const auto speaker = g_eightbit->speakers.find(speakerUserId);
	const int defaultEffect = speaker == g_eightbit->speakers.end()
		? AudioEffects::EFF_NONE : speaker->second->defaultEffect;
	if (defaultEffect == AudioEffects::EFF_NONE && !HasRouteForSpeaker(speakerUserId)) {
		return original(client, nBytes, data, xuid);
	}

	if (!BroadcastRouted(client, nBytes, data)) return original(client, nBytes, data, xuid);
}

void RemoveRoutesForPlayer(int userId) {
	for (auto it = g_eightbit->receiverEffects.begin(); it != g_eightbit->receiverEffects.end();) {
		const int listener = static_cast<int>(static_cast<uint32_t>(it->first >> 32));
		const int speaker = static_cast<int>(static_cast<uint32_t>(it->first & 0xFFFFFFFFu));
		if (listener == userId || speaker == userId) it = g_eightbit->receiverEffects.erase(it);
		else ++it;
	}
	g_eightbit->speakers.erase(userId);
}

void PushField(GarrysMod::Lua::ILuaBase* lua, const char* key, const std::string& value) {
	lua->PushString(key); lua->PushString(value.c_str()); lua->SetTable(-3);
}

void PushField(GarrysMod::Lua::ILuaBase* lua, const char* key, double value) {
	lua->PushString(key); lua->PushNumber(value); lua->SetTable(-3);
}

void PushField(GarrysMod::Lua::ILuaBase* lua, const char* key, bool value) {
	lua->PushString(key); lua->PushBool(value); lua->SetTable(-3);
}

void PushPreset(GarrysMod::Lua::ILuaBase* lua, const AudioEffects::Preset& preset) {
	lua->CreateTable();
	PushField(lua, "id", static_cast<double>(preset.id));
	PushField(lua, "key", preset.key);
	PushField(lua, "name", preset.name);
	PushField(lua, "description", preset.description);
	PushField(lua, "custom", preset.custom);
	PushField(lua, "file", preset.sourceFile);
}

void AddFunction(GarrysMod::Lua::ILuaBase* lua, const char* name, GarrysMod::Lua::CFunc function) {
	lua->PushString(name); lua->PushCFunction(function); lua->SetTable(-3);
}

void AddNumber(GarrysMod::Lua::ILuaBase* lua, const char* name, double number) {
	lua->PushString(name); lua->PushNumber(number); lua->SetTable(-3);
}

} // namespace

LUA_FUNCTION_STATIC(eightbit_set_crush) {
	g_eightbit->crushFactor = std::max(1, std::min(32767, static_cast<int>(LUA->GetNumber(1))));
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_get_crush) { LUA->PushNumber(g_eightbit->crushFactor); return 1; }
LUA_FUNCTION_STATIC(eightbit_set_gain) { g_eightbit->gainFactor = static_cast<float>(LUA->GetNumber(1)); return 0; }
LUA_FUNCTION_STATIC(eightbit_set_desample) { g_eightbit->desampleRate = std::max(1, std::min(32, static_cast<int>(LUA->GetNumber(1)))); return 0; }
LUA_FUNCTION_STATIC(eightbit_set_broadcast_ip) { g_eightbit->ip = LUA->GetString(1); return 0; }
LUA_FUNCTION_STATIC(eightbit_set_broadcast_port) { g_eightbit->port = static_cast<uint16_t>(LUA->GetNumber(1)); return 0; }
LUA_FUNCTION_STATIC(eightbit_broadcast) { g_eightbit->broadcastPackets = LUA->GetBool(1); return 0; }

LUA_FUNCTION_STATIC(eightbit_enable_effect) {
	const int userId = static_cast<int>(LUA->GetNumber(1));
	const int effect = static_cast<int>(LUA->GetNumber(2));
	AudioEffects::Preset preset;
	if (!ResolvePreset(effect, preset)) { LUA->PushBool(false); return 1; }

	if (effect == AudioEffects::EFF_NONE) {
		const auto found = g_eightbit->speakers.find(userId);
		if (found != g_eightbit->speakers.end()) {
			found->second->defaultEffect = AudioEffects::EFF_NONE;
			if (!HasRouteForSpeaker(userId)) g_eightbit->speakers.erase(found);
		}
	} else {
		SpeakerState(userId).defaultEffect = effect;
	}
	LUA->PushBool(true);
	return 1;
}

LUA_FUNCTION_STATIC(eightbit_set_receiver_effect) {
	const int listener = static_cast<int>(LUA->GetNumber(1));
	const int speaker = static_cast<int>(LUA->GetNumber(2));
	const int effect = static_cast<int>(LUA->GetNumber(3));
	AudioEffects::Preset preset;
	if (!ResolvePreset(effect, preset)) { LUA->PushBool(false); return 1; }
	g_eightbit->receiverEffects[EightbitState::RouteKey(listener, speaker)] = effect;
	if (effect != AudioEffects::EFF_NONE) SpeakerState(speaker);
	LUA->PushBool(true);
	return 1;
}

LUA_FUNCTION_STATIC(eightbit_set_should_hear_effect) {
	const int listener = static_cast<int>(LUA->GetNumber(1));
	const int speaker = static_cast<int>(LUA->GetNumber(2));
	if (LUA->GetBool(3)) g_eightbit->receiverEffects.erase(EightbitState::RouteKey(listener, speaker));
	else g_eightbit->receiverEffects[EightbitState::RouteKey(listener, speaker)] = AudioEffects::EFF_NONE;
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_clear_receiver_effect) {
	const int listener = static_cast<int>(LUA->GetNumber(1));
	const int speaker = static_cast<int>(LUA->GetNumber(2));
	g_eightbit->receiverEffects.erase(EightbitState::RouteKey(listener, speaker));
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_get_receiver_effect) {
	const int listener = static_cast<int>(LUA->GetNumber(1));
	const int speakerId = static_cast<int>(LUA->GetNumber(2));
	int defaultEffect = AudioEffects::EFF_NONE;
	const auto speaker = g_eightbit->speakers.find(speakerId);
	if (speaker != g_eightbit->speakers.end()) defaultEffect = speaker->second->defaultEffect;
	LUA->PushNumber(EffectForReceiver(listener, speakerId, defaultEffect));
	return 1;
}

LUA_FUNCTION_STATIC(eightbit_clear_player) { RemoveRoutesForPlayer(static_cast<int>(LUA->GetNumber(1))); return 0; }

LUA_FUNCTION_STATIC(eightbit_clear_all_routes) {
	g_eightbit->receiverEffects.clear();
	for (auto it = g_eightbit->speakers.begin(); it != g_eightbit->speakers.end();) {
		if (it->second->defaultEffect == AudioEffects::EFF_NONE) it = g_eightbit->speakers.erase(it);
		else ++it;
	}
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_reload_presets) {
	int count = 0;
	if (LUA->GetType(1) == GarrysMod::Lua::Type::Table) {
		std::vector<std::pair<std::string, std::string>> files;
		files.reserve(128);
		LUA->PushNil();
		while (LUA->Next(1) != 0) {
			if (files.size() < 128 && LUA->GetType(-2) == GarrysMod::Lua::Type::String
				&& LUA->GetType(-1) == GarrysMod::Lua::Type::String) {
				unsigned int nameLength = 0;
				unsigned int contentLength = 0;
				const char* name = LUA->GetString(-2, &nameLength);
				const char* content = LUA->GetString(-1, &contentLength);
				if (name != nullptr && content != nullptr && nameLength > 5 && nameLength <= 255
					&& contentLength > 0 && contentLength <= 65536) {
					files.emplace_back(std::string(name, nameLength), std::string(content, contentLength));
				}
			}
			LUA->Pop(1);
		}
		count = g_eightbit->presets.ReloadFromMemory(files);
	} else {
		std::string directory;
		if (LUA->GetType(1) == GarrysMod::Lua::Type::String) directory = LUA->GetString(1);
		count = g_eightbit->presets.Reload(directory);
	}
	for (const std::string& error : g_eightbit->presets.Errors()) std::cerr << "[eightbit] preset error: " << error << "\n";
	LUA->PushNumber(count);
	return 1;
}

LUA_FUNCTION_STATIC(eightbit_write_preset_file) {
	if (LUA->GetType(1) != GarrysMod::Lua::Type::String
		|| LUA->GetType(2) != GarrysMod::Lua::Type::String) {
		LUA->PushBool(false);
		LUA->PushString("expected filename and preset content strings");
		return 2;
	}

	unsigned int nameLength = 0;
	unsigned int contentLength = 0;
	const char* name = LUA->GetString(1, &nameLength);
	const char* content = LUA->GetString(2, &contentLength);
	if (name == nullptr || content == nullptr || nameLength == 0 || nameLength > 128
		|| contentLength == 0 || contentLength > 65536) {
		LUA->PushBool(false);
		LUA->PushString("invalid filename or preset content size");
		return 2;
	}

	std::string storedPath;
	std::string error;
	const bool success = g_eightbit->presets.WritePresetFile(
		std::string(name, nameLength), std::string(content, contentLength), storedPath, error);
	LUA->PushBool(success);
	LUA->PushString((success ? storedPath : error).c_str());
	return 2;
}

LUA_FUNCTION_STATIC(eightbit_get_presets) {
	LUA->CreateTable();
	int index = 1;
	for (int id = AudioEffects::EFF_NONE; id <= AudioEffects::EFF_COMBO; ++id) {
		LUA->PushNumber(index++);
		PushPreset(LUA, AudioEffects::MakeBuiltin(id, g_eightbit->gainFactor,
			g_eightbit->crushFactor, g_eightbit->desampleRate));
		LUA->SetTable(-3);
	}
	for (const AudioEffects::Preset& preset : g_eightbit->presets.CustomPresets()) {
		LUA->PushNumber(index++); PushPreset(LUA, preset); LUA->SetTable(-3);
	}
	return 1;
}

LUA_FUNCTION_STATIC(eightbit_get_preset_name) {
	AudioEffects::Preset preset;
	if (!ResolvePreset(static_cast<int>(LUA->GetNumber(1)), preset)) { LUA->PushNil(); return 1; }
	LUA->PushString(preset.name.c_str());
	return 1;
}

LUA_FUNCTION_STATIC(eightbit_is_valid_preset) {
	AudioEffects::Preset preset;
	LUA->PushBool(ResolvePreset(static_cast<int>(LUA->GetNumber(1)), preset));
	return 1;
}

LUA_FUNCTION_STATIC(eightbit_get_preset_directory) { LUA->PushString(g_eightbit->presets.Directory().c_str()); return 1; }
LUA_FUNCTION_STATIC(eightbit_is_hook_available) { LUA->PushBool(g_voiceHookAvailable); return 1; }
LUA_FUNCTION_STATIC(eightbit_get_version) { LUA->PushString("2.0.2-fbradio1"); return 1; }
LUA_FUNCTION_STATIC(eightbit_has_radio_model) { LUA->PushBool(true); return 1; }   // FB Radio : radio_model=1 disponible

GMOD_MODULE_OPEN() {
	g_eightbit = new EightbitState();
	g_eightbit->presets.Reload();
	g_net = new Net();

	SourceSDK::ModuleLoader engineLoader("engine");
	SymbolFinder symbolFinder;
	void* target = nullptr;

#ifdef SYSTEM_WINDOWS
	HMODULE engine = reinterpret_cast<HMODULE>(engineLoader.GetModule());
	if (engine != nullptr) {
#if defined(ARCHITECTURE_X86)
		const std::vector<Symbol> symbols = {
			Symbol::FromSignature("\x55\x8B\xEC\xA1****\x83\xEC\x50\x83\x78\x30\x00\x0F\x84****\x53\x8D\x4D\xD8\xC6\x45\xB4\x01\xC7\x45*****"),
			Symbol::FromSignature("\x55\x8B\xEC\x8B\x0D****\x83\xEC\x58\x81\xF9****")
		};
#else
		const std::vector<Symbol> symbols = {
			Symbol::FromSignature("\x48\x89\x5C\x24*\x56\x57\x41\x56\x48\x81\xEC****\x8B\xF2\x4C\x8B\xF1")
		};
#endif
		for (const Symbol& symbol : symbols) {
			target = symbolFinder.Resolve(engineLoader.GetModule(), symbol.name.c_str(), symbol.length);
			if (target != nullptr) break;
		}
	}
#endif

#ifdef SYSTEM_LINUX
	const std::vector<Symbol> symbols = {
		Symbol::FromName("_Z21SV_BroadcastVoiceDataP7IClientiPcx"),
#if defined(ARCHITECTURE_X86_64)
		Symbol::FromSignature("\x55\x48\x8D\x05****\x48\x89\xE5\x41\x57\x41\x56\x41\x89\xF6\x41\x55\x49\x89\xFD\x41\x54\x49\x89\xD4\x53\x48\x89\xCB\x48\x81\xEC****\x48\x8B\x3D****\x48\x39\xC7\x74\x25")
#endif
	};
	for (const Symbol& symbol : symbols) {
		target = symbolFinder.Resolve(engineLoader.GetModule(), symbol.name.c_str(), symbol.length);
		if (target != nullptr) break;
	}
#endif

	if (target != nullptr) {
		g_broadcastHook.Create(Detouring::Hook::Target(target), reinterpret_cast<void*>(&HookBroadcastVoice));
		if (g_broadcastHook.IsValid()) { g_broadcastHook.Enable(); g_voiceHookAvailable = true; }
	}

	if (!g_voiceHookAvailable) std::cerr << "[eightbit] SV_BroadcastVoiceData not found; effects are disabled\n";
	else std::cout << "[eightbit] voice router enabled; custom presets: "
		<< g_eightbit->presets.CustomPresets().size() << "\n";

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->PushString("eightbit");
	LUA->CreateTable();

	AddFunction(LUA, "SetCrushFactor", eightbit_set_crush);
	AddFunction(LUA, "GetCrushFactor", eightbit_get_crush);
	AddFunction(LUA, "SetGainFactor", eightbit_set_gain);
	AddFunction(LUA, "SetDesampleRate", eightbit_set_desample);
	AddFunction(LUA, "EnableBroadcast", eightbit_broadcast);
	AddFunction(LUA, "SetBroadcastIP", eightbit_set_broadcast_ip);
	AddFunction(LUA, "SetBroadcastPort", eightbit_set_broadcast_port);
	AddFunction(LUA, "EnableEffect", eightbit_enable_effect);
	AddFunction(LUA, "SetReceiverEffect", eightbit_set_receiver_effect);
	AddFunction(LUA, "HasRadioModel", eightbit_has_radio_model);
	AddFunction(LUA, "SetShouldHearEffect", eightbit_set_should_hear_effect);
	AddFunction(LUA, "ClearReceiverEffect", eightbit_clear_receiver_effect);
	AddFunction(LUA, "GetReceiverEffect", eightbit_get_receiver_effect);
	AddFunction(LUA, "ClearPlayer", eightbit_clear_player);
	AddFunction(LUA, "ClearAllRoutes", eightbit_clear_all_routes);
	AddFunction(LUA, "ReloadPresets", eightbit_reload_presets);
	AddFunction(LUA, "WritePresetFile", eightbit_write_preset_file);
	AddFunction(LUA, "GetPresets", eightbit_get_presets);
	AddFunction(LUA, "GetPresetName", eightbit_get_preset_name);
	AddFunction(LUA, "IsValidPreset", eightbit_is_valid_preset);
	AddFunction(LUA, "GetPresetDirectory", eightbit_get_preset_directory);
	AddFunction(LUA, "IsHookAvailable", eightbit_is_hook_available);
	AddFunction(LUA, "GetVersion", eightbit_get_version);

	AddNumber(LUA, "EFF_NONE", AudioEffects::EFF_NONE);
	AddNumber(LUA, "EFF_BITCRUSH", AudioEffects::EFF_BITCRUSH);
	AddNumber(LUA, "EFF_DESAMPLE", AudioEffects::EFF_DESAMPLE);
	AddNumber(LUA, "EFF_COMB", AudioEffects::EFF_COMB);
	AddNumber(LUA, "EFF_DARTHVADER", AudioEffects::EFF_DARTHVADER);
	AddNumber(LUA, "EFF_RADIO", AudioEffects::EFF_RADIO);
	AddNumber(LUA, "EFF_ROBOT", AudioEffects::EFF_ROBOT);
	AddNumber(LUA, "EFF_ALIEN", AudioEffects::EFF_ALIEN);
	AddNumber(LUA, "EFF_OVERDRIVE", AudioEffects::EFF_OVERDRIVE);
	AddNumber(LUA, "EFF_DISTORTION", AudioEffects::EFF_DISTORTION);
	AddNumber(LUA, "EFF_TELEPHONE", AudioEffects::EFF_TELEPHONE);
	AddNumber(LUA, "EFF_MEGAPHONE", AudioEffects::EFF_MEGAPHONE);
	AddNumber(LUA, "EFF_CHIPMUNK", AudioEffects::EFF_CHIPMUNK);
	AddNumber(LUA, "EFF_SLOWMOTION", AudioEffects::EFF_SLOWMOTION);
	AddNumber(LUA, "EFF_COMBO", AudioEffects::EFF_COMBO);
	AddNumber(LUA, "CUSTOM_ID_MIN", AudioEffects::CUSTOM_ID_MIN);

	LUA->SetTable(-3);
	LUA->Pop();

#ifdef THIRDPARTY_LINK
	linkMutedFunc();
#endif
	return 0;
}

GMOD_MODULE_CLOSE() {
	if (g_voiceHookAvailable) g_broadcastHook.Destroy();
	g_voiceHookAvailable = false;
	delete g_net;
	delete g_eightbit;
	g_net = nullptr;
	g_eightbit = nullptr;
	return 0;
}
