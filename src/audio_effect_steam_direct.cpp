#include "audio_effect_steam_direct.hpp"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/print_string.hpp>

namespace godot {

Ref<AudioEffectInstance> AudioEffectSteamDirect::_instantiate() {
	Ref<AudioEffectSteamDirectInstance> instance;
	instance.instantiate();
	instance->set_parameters(parameters);
	return instance;
}

void AudioEffectSteamDirect::set_distance_attenuation_enabled(bool value) {
	distance_attenuation_enabled = value;
	parameters->distance_attenuation_enabled.store(value, std::memory_order_relaxed);
}

bool AudioEffectSteamDirect::is_distance_attenuation_enabled() const { return distance_attenuation_enabled; }

void AudioEffectSteamDirect::set_distance_attenuation(float value) {
	distance_attenuation = std::clamp(value, 0.0f, 1.0f);
	parameters->distance_attenuation.store(distance_attenuation, std::memory_order_relaxed);
}

float AudioEffectSteamDirect::get_distance_attenuation() const { return distance_attenuation; }

void AudioEffectSteamDirect::set_air_absorption_enabled(bool value) {
	air_absorption_enabled = value;
	parameters->air_absorption_enabled.store(value, std::memory_order_relaxed);
}

bool AudioEffectSteamDirect::is_air_absorption_enabled() const { return air_absorption_enabled; }

void AudioEffectSteamDirect::set_air_absorption(const Vector3 &value) {
	air_absorption = Vector3(
			std::clamp(value.x, 0.0f, 1.0f),
			std::clamp(value.y, 0.0f, 1.0f),
			std::clamp(value.z, 0.0f, 1.0f));
	parameters->air_absorption_low.store(air_absorption.x, std::memory_order_relaxed);
	parameters->air_absorption_mid.store(air_absorption.y, std::memory_order_relaxed);
	parameters->air_absorption_high.store(air_absorption.z, std::memory_order_relaxed);
}

Vector3 AudioEffectSteamDirect::get_air_absorption() const { return air_absorption; }

void AudioEffectSteamDirect::set_directivity_enabled(bool value) {
	directivity_enabled = value;
	parameters->directivity_enabled.store(value, std::memory_order_relaxed);
}

bool AudioEffectSteamDirect::is_directivity_enabled() const { return directivity_enabled; }

void AudioEffectSteamDirect::set_directivity(float value) {
	directivity = std::clamp(value, 0.0f, 1.0f);
	parameters->directivity.store(directivity, std::memory_order_relaxed);
}

float AudioEffectSteamDirect::get_directivity() const { return directivity; }

void AudioEffectSteamDirect::set_occlusion_enabled(bool value) {
	occlusion_enabled = value;
	parameters->occlusion_enabled.store(value, std::memory_order_relaxed);
}

bool AudioEffectSteamDirect::is_occlusion_enabled() const { return occlusion_enabled; }

void AudioEffectSteamDirect::set_occlusion(float value) {
	occlusion = std::clamp(value, 0.0f, 1.0f);
	parameters->occlusion.store(occlusion, std::memory_order_relaxed);
}

float AudioEffectSteamDirect::get_occlusion() const { return occlusion; }

void AudioEffectSteamDirect::set_transmission_enabled(bool value) {
	transmission_enabled = value;
	parameters->transmission_enabled.store(value, std::memory_order_relaxed);
}

bool AudioEffectSteamDirect::is_transmission_enabled() const { return transmission_enabled; }

void AudioEffectSteamDirect::set_transmission_type(TransmissionType value) {
	if (value != TRANSMISSION_FREQUENCY_INDEPENDENT && value != TRANSMISSION_FREQUENCY_DEPENDENT) {
		return;
	}
	transmission_type = value;
	parameters->transmission_type.store(value, std::memory_order_relaxed);
}

AudioEffectSteamDirect::TransmissionType AudioEffectSteamDirect::get_transmission_type() const { return transmission_type; }

void AudioEffectSteamDirect::set_transmission(const Vector3 &value) {
	transmission = Vector3(
			std::clamp(value.x, 0.0f, 1.0f),
			std::clamp(value.y, 0.0f, 1.0f),
			std::clamp(value.z, 0.0f, 1.0f));
	parameters->transmission_low.store(transmission.x, std::memory_order_relaxed);
	parameters->transmission_mid.store(transmission.y, std::memory_order_relaxed);
	parameters->transmission_high.store(transmission.z, std::memory_order_relaxed);
}

Vector3 AudioEffectSteamDirect::get_transmission() const { return transmission; }

void AudioEffectSteamDirect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_distance_attenuation_enabled", "enabled"), &AudioEffectSteamDirect::set_distance_attenuation_enabled);
	ClassDB::bind_method(D_METHOD("is_distance_attenuation_enabled"), &AudioEffectSteamDirect::is_distance_attenuation_enabled);
	ClassDB::bind_method(D_METHOD("set_distance_attenuation", "distance_attenuation"), &AudioEffectSteamDirect::set_distance_attenuation);
	ClassDB::bind_method(D_METHOD("get_distance_attenuation"), &AudioEffectSteamDirect::get_distance_attenuation);
	ClassDB::bind_method(D_METHOD("set_air_absorption_enabled", "enabled"), &AudioEffectSteamDirect::set_air_absorption_enabled);
	ClassDB::bind_method(D_METHOD("is_air_absorption_enabled"), &AudioEffectSteamDirect::is_air_absorption_enabled);
	ClassDB::bind_method(D_METHOD("set_air_absorption", "coefficients"), &AudioEffectSteamDirect::set_air_absorption);
	ClassDB::bind_method(D_METHOD("get_air_absorption"), &AudioEffectSteamDirect::get_air_absorption);
	ClassDB::bind_method(D_METHOD("set_directivity_enabled", "enabled"), &AudioEffectSteamDirect::set_directivity_enabled);
	ClassDB::bind_method(D_METHOD("is_directivity_enabled"), &AudioEffectSteamDirect::is_directivity_enabled);
	ClassDB::bind_method(D_METHOD("set_directivity", "directivity"), &AudioEffectSteamDirect::set_directivity);
	ClassDB::bind_method(D_METHOD("get_directivity"), &AudioEffectSteamDirect::get_directivity);
	ClassDB::bind_method(D_METHOD("set_occlusion_enabled", "enabled"), &AudioEffectSteamDirect::set_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("is_occlusion_enabled"), &AudioEffectSteamDirect::is_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("set_occlusion", "occlusion"), &AudioEffectSteamDirect::set_occlusion);
	ClassDB::bind_method(D_METHOD("get_occlusion"), &AudioEffectSteamDirect::get_occlusion);
	ClassDB::bind_method(D_METHOD("set_transmission_enabled", "enabled"), &AudioEffectSteamDirect::set_transmission_enabled);
	ClassDB::bind_method(D_METHOD("is_transmission_enabled"), &AudioEffectSteamDirect::is_transmission_enabled);
	ClassDB::bind_method(D_METHOD("set_transmission_type", "type"), &AudioEffectSteamDirect::set_transmission_type);
	ClassDB::bind_method(D_METHOD("get_transmission_type"), &AudioEffectSteamDirect::get_transmission_type);
	ClassDB::bind_method(D_METHOD("set_transmission", "coefficients"), &AudioEffectSteamDirect::set_transmission);
	ClassDB::bind_method(D_METHOD("get_transmission"), &AudioEffectSteamDirect::get_transmission);
	ADD_GROUP("Distance Attenuation", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "distance_attenuation_enabled"), "set_distance_attenuation_enabled", "is_distance_attenuation_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_attenuation", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_distance_attenuation", "get_distance_attenuation");
	ADD_GROUP("Air Absorption", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "air_absorption_enabled"), "set_air_absorption_enabled", "is_air_absorption_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "air_absorption", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_air_absorption", "get_air_absorption");
	ADD_GROUP("Directivity", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "directivity_enabled"), "set_directivity_enabled", "is_directivity_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "directivity", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_directivity", "get_directivity");
	ADD_GROUP("Occlusion", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "occlusion_enabled"), "set_occlusion_enabled", "is_occlusion_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_occlusion", "get_occlusion");
	ADD_GROUP("Transmission", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "transmission_enabled"), "set_transmission_enabled", "is_transmission_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "transmission_type", PROPERTY_HINT_ENUM, "Frequency Independent,Frequency Dependent"), "set_transmission_type", "get_transmission_type");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "transmission", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_transmission", "get_transmission");

	BIND_ENUM_CONSTANT(TRANSMISSION_FREQUENCY_INDEPENDENT);
	BIND_ENUM_CONSTANT(TRANSMISSION_FREQUENCY_DEPENDENT);
}

AudioEffectSteamDirectInstance::~AudioEffectSteamDirectInstance() { release_steam_audio(); }

void AudioEffectSteamDirectInstance::release_steam_audio() {
	if (context && output_buffer.data) {
		iplAudioBufferFree(context, &output_buffer);
	}
	if (direct_effect) {
		iplDirectEffectRelease(&direct_effect);
	}
	if (context) {
		iplContextRelease(&context);
	}
	input_buffer = {};
	output_buffer = {};
	processing_frame_size = 0;
}

void AudioEffectSteamDirectInstance::set_parameters(const std::shared_ptr<AudioEffectSteamDirect::Parameters> &value) {
	parameters = value;
	sampling_rate = static_cast<int32_t>(AudioServer::get_singleton()->get_mix_rate());
	configured = sampling_rate > 0;
}

bool AudioEffectSteamDirectInstance::ensure_processing_size(int32_t frame_count) {
	if (!configured || frame_count <= 0) {
		return false;
	}
	if (direct_effect && output_buffer.data && processing_frame_size == frame_count) {
		return true;
	}
	if (processing_frame_size != 0) {
		print_verbose("AudioEffectSteamDirect: processing frame size changed from ", processing_frame_size, " to ", frame_count, "; rebuilding Steam Audio buffers.");
	}

	release_steam_audio();

	IPLContextSettings context_settings{};
	context_settings.version = STEAMAUDIO_VERSION;
	if (iplContextCreate(&context_settings, &context) != IPL_STATUS_SUCCESS) {
		return false;
	}

	IPLAudioSettings audio_settings{};
	audio_settings.samplingRate = sampling_rate;
	audio_settings.frameSize = frame_count;

	IPLDirectEffectSettings effect_settings{};
	effect_settings.numChannels = 2;
	if (iplDirectEffectCreate(context, &audio_settings, &effect_settings, &direct_effect) != IPL_STATUS_SUCCESS) {
		release_steam_audio();
		return false;
	}

	input_left.assign(frame_count, 0.0f);
	input_right.assign(frame_count, 0.0f);
	input_channels[0] = input_left.data();
	input_channels[1] = input_right.data();
	input_buffer.numChannels = 2;
	input_buffer.numSamples = frame_count;
	input_buffer.data = input_channels.data();
	if (iplAudioBufferAllocate(context, 2, frame_count, &output_buffer) != IPL_STATUS_SUCCESS) {
		release_steam_audio();
		return false;
	}
	processing_frame_size = frame_count;
	return true;
}

void AudioEffectSteamDirectInstance::_process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) {
	const auto *src = static_cast<const AudioFrame *>(src_buffer);
	if (!parameters || !ensure_processing_size(frame_count)) {
		std::copy_n(src, frame_count, dst_buffer);
		return;
	}

	for (int32_t i = 0; i < frame_count; ++i) {
		input_left[i] = src[i].left;
		input_right[i] = src[i].right;
	}

	IPLDirectEffectParams effect_parameters{};
	effect_parameters.flags = static_cast<IPLDirectEffectFlags>(0);
	if (parameters->distance_attenuation_enabled.load(std::memory_order_relaxed)) {
		effect_parameters.flags = static_cast<IPLDirectEffectFlags>(effect_parameters.flags | IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION);
	}
	if (parameters->air_absorption_enabled.load(std::memory_order_relaxed)) {
		effect_parameters.flags = static_cast<IPLDirectEffectFlags>(effect_parameters.flags | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION);
	}
	if (parameters->directivity_enabled.load(std::memory_order_relaxed)) {
		effect_parameters.flags = static_cast<IPLDirectEffectFlags>(effect_parameters.flags | IPL_DIRECTEFFECTFLAGS_APPLYDIRECTIVITY);
	}
	if (parameters->occlusion_enabled.load(std::memory_order_relaxed)) {
		effect_parameters.flags = static_cast<IPLDirectEffectFlags>(effect_parameters.flags | IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION);
	}
	if (parameters->transmission_enabled.load(std::memory_order_relaxed)) {
		effect_parameters.flags = static_cast<IPLDirectEffectFlags>(effect_parameters.flags | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION);
	}
	effect_parameters.transmissionType = parameters->transmission_type.load(std::memory_order_relaxed) == AudioEffectSteamDirect::TRANSMISSION_FREQUENCY_INDEPENDENT
			? IPL_TRANSMISSIONTYPE_FREQINDEPENDENT
			: IPL_TRANSMISSIONTYPE_FREQDEPENDENT;
	effect_parameters.distanceAttenuation = parameters->distance_attenuation.load(std::memory_order_relaxed);
	effect_parameters.airAbsorption[0] = parameters->air_absorption_low.load(std::memory_order_relaxed);
	effect_parameters.airAbsorption[1] = parameters->air_absorption_mid.load(std::memory_order_relaxed);
	effect_parameters.airAbsorption[2] = parameters->air_absorption_high.load(std::memory_order_relaxed);
	effect_parameters.directivity = parameters->directivity.load(std::memory_order_relaxed);
	effect_parameters.occlusion = parameters->occlusion.load(std::memory_order_relaxed);
	effect_parameters.transmission[0] = parameters->transmission_low.load(std::memory_order_relaxed);
	effect_parameters.transmission[1] = parameters->transmission_mid.load(std::memory_order_relaxed);
	effect_parameters.transmission[2] = parameters->transmission_high.load(std::memory_order_relaxed);
	iplDirectEffectApply(direct_effect, &effect_parameters, &input_buffer, &output_buffer);

	for (int32_t i = 0; i < frame_count; ++i) {
		dst_buffer[i].left = output_buffer.data[0][i];
		dst_buffer[i].right = output_buffer.data[1][i];
	}
}

bool AudioEffectSteamDirectInstance::_process_silence() const { return false; }
bool AudioEffectSteamDirectInstance::is_ready() const { return configured; }

void AudioEffectSteamDirectInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_ready"), &AudioEffectSteamDirectInstance::is_ready);
}

} // namespace godot
