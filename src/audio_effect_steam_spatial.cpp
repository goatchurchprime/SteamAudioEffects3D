#include "audio_effect_steam_spatial.hpp"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/print_string.hpp>

namespace godot {

Ref<AudioEffectInstance> AudioEffectSteamSpatial::_instantiate() {
	Ref<AudioEffectSteamSpatialInstance> instance;
	instance.instantiate();
	instance->set_parameters(parameters);
	return instance;
}

void AudioEffectSteamSpatial::set_direction(const Vector3 &value) {
	direction = value.length_squared() > 0.000001f ? value.normalized() : Vector3(0.0f, 0.0f, -1.0f);
	parameters->direction_x.store(direction.x, std::memory_order_relaxed);
	parameters->direction_y.store(direction.y, std::memory_order_relaxed);
	parameters->direction_z.store(direction.z, std::memory_order_release);
}

Vector3 AudioEffectSteamSpatial::get_direction() const { return direction; }

void AudioEffectSteamSpatial::set_gain(float value) {
	gain = std::max(value, 0.0f);
	parameters->gain.store(gain, std::memory_order_relaxed);
}

float AudioEffectSteamSpatial::get_gain() const { return gain; }

void AudioEffectSteamSpatial::set_spatial_blend(float value) {
	spatial_blend = std::clamp(value, 0.0f, 1.0f);
	parameters->spatial_blend.store(spatial_blend, std::memory_order_relaxed);
}

float AudioEffectSteamSpatial::get_spatial_blend() const { return spatial_blend; }

void AudioEffectSteamSpatial::set_interpolation(Interpolation value) {
	if (value != INTERPOLATION_NEAREST && value != INTERPOLATION_BILINEAR) {
		return;
	}
	interpolation = value;
	parameters->interpolation.store(value, std::memory_order_relaxed);
}

AudioEffectSteamSpatial::Interpolation AudioEffectSteamSpatial::get_interpolation() const { return interpolation; }

void AudioEffectSteamSpatial::set_source_parameters(const Vector3 &source_direction, float source_gain, float source_spatial_blend) {
	set_direction(source_direction);
	set_gain(source_gain);
	set_spatial_blend(source_spatial_blend);
}

void AudioEffectSteamSpatial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_direction", "direction"), &AudioEffectSteamSpatial::set_direction);
	ClassDB::bind_method(D_METHOD("get_direction"), &AudioEffectSteamSpatial::get_direction);
	ClassDB::bind_method(D_METHOD("set_gain", "gain"), &AudioEffectSteamSpatial::set_gain);
	ClassDB::bind_method(D_METHOD("get_gain"), &AudioEffectSteamSpatial::get_gain);
	ClassDB::bind_method(D_METHOD("set_spatial_blend", "spatial_blend"), &AudioEffectSteamSpatial::set_spatial_blend);
	ClassDB::bind_method(D_METHOD("get_spatial_blend"), &AudioEffectSteamSpatial::get_spatial_blend);
	ClassDB::bind_method(D_METHOD("set_interpolation", "interpolation"), &AudioEffectSteamSpatial::set_interpolation);
	ClassDB::bind_method(D_METHOD("get_interpolation"), &AudioEffectSteamSpatial::get_interpolation);
	ClassDB::bind_method(D_METHOD("set_source_parameters", "direction", "gain", "spatial_blend"), &AudioEffectSteamSpatial::set_source_parameters, DEFVAL(1.0f));

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "direction"), "set_direction", "get_direction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gain", PROPERTY_HINT_RANGE, "0,4,0.001,or_greater"), "set_gain", "get_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spatial_blend", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_spatial_blend", "get_spatial_blend");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "interpolation", PROPERTY_HINT_ENUM, "Nearest,Bilinear"), "set_interpolation", "get_interpolation");

	BIND_ENUM_CONSTANT(INTERPOLATION_NEAREST);
	BIND_ENUM_CONSTANT(INTERPOLATION_BILINEAR);
}

AudioEffectSteamSpatialInstance::~AudioEffectSteamSpatialInstance() { release_steam_audio(); }

void AudioEffectSteamSpatialInstance::release_steam_audio() {
	if (context && output_buffer.data) {
		iplAudioBufferFree(context, &output_buffer);
	}
	if (binaural_effect) {
		iplBinauralEffectRelease(&binaural_effect);
	}
	if (hrtf) {
		iplHRTFRelease(&hrtf);
	}
	if (context) {
		iplContextRelease(&context);
	}
	input_buffer = {};
	output_buffer = {};
	processing_frame_size = 0;
}

void AudioEffectSteamSpatialInstance::set_parameters(const std::shared_ptr<AudioEffectSteamSpatial::Parameters> &value) {
	parameters = value;
	sampling_rate = static_cast<int32_t>(AudioServer::get_singleton()->get_mix_rate());
	configured = sampling_rate > 0;
}

bool AudioEffectSteamSpatialInstance::ensure_processing_size(int32_t frame_count) {
	if (!configured || frame_count <= 0) {
		return false;
	}
	if (binaural_effect && output_buffer.data && processing_frame_size == frame_count) {
		return true;
	}
	if (processing_frame_size != 0) {
		print_verbose("AudioEffectSteamSpatial: processing frame size changed from ", processing_frame_size, " to ", frame_count, "; rebuilding Steam Audio buffers.");
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

	IPLHRTFSettings hrtf_settings{};
	hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
	hrtf_settings.volume = 1.0f;
	hrtf_settings.normType = IPL_HRTFNORMTYPE_NONE;
	if (iplHRTFCreate(context, &audio_settings, &hrtf_settings, &hrtf) != IPL_STATUS_SUCCESS) {
		release_steam_audio();
		return false;
	}

	IPLBinauralEffectSettings effect_settings{};
	effect_settings.hrtf = hrtf;
	if (iplBinauralEffectCreate(context, &audio_settings, &effect_settings, &binaural_effect) != IPL_STATUS_SUCCESS) {
		release_steam_audio();
		return false;
	}

	mono.assign(frame_count, 0.0f);
	input_channels[0] = mono.data();
	input_buffer.numChannels = 1;
	input_buffer.numSamples = frame_count;
	input_buffer.data = input_channels.data();
	if (iplAudioBufferAllocate(context, 2, frame_count, &output_buffer) != IPL_STATUS_SUCCESS) {
		release_steam_audio();
		return false;
	}
	processing_frame_size = frame_count;
	return true;
}

void AudioEffectSteamSpatialInstance::_process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) {
	const auto *src = static_cast<const AudioFrame *>(src_buffer);
	if (!parameters || !ensure_processing_size(frame_count)) {
		std::copy_n(src, frame_count, dst_buffer);
		return;
	}

	const float gain = parameters->gain.load(std::memory_order_relaxed);
	for (int32_t i = 0; i < frame_count; ++i) {
		mono[i] = 0.5f * (src[i].left + src[i].right) * gain;
	}

	IPLVector3 direction{
		parameters->direction_x.load(std::memory_order_relaxed),
		parameters->direction_y.load(std::memory_order_relaxed),
		parameters->direction_z.load(std::memory_order_acquire),
	};
	const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
	if (length > 0.000001f) {
		direction.x /= length;
		direction.y /= length;
		direction.z /= length;
	} else {
		direction = { 0.0f, 0.0f, -1.0f };
	}

	IPLBinauralEffectParams effect_parameters{};
	effect_parameters.direction = direction;
	effect_parameters.interpolation = parameters->interpolation.load(std::memory_order_relaxed) == AudioEffectSteamSpatial::INTERPOLATION_NEAREST
			? IPL_HRTFINTERPOLATION_NEAREST
			: IPL_HRTFINTERPOLATION_BILINEAR;
	effect_parameters.spatialBlend = parameters->spatial_blend.load(std::memory_order_relaxed);
	effect_parameters.hrtf = hrtf;
	effect_parameters.peakDelays = nullptr;
	iplBinauralEffectApply(binaural_effect, &effect_parameters, &input_buffer, &output_buffer);

	for (int32_t i = 0; i < frame_count; ++i) {
		dst_buffer[i].left = output_buffer.data[0][i];
		dst_buffer[i].right = output_buffer.data[1][i];
	}
}

bool AudioEffectSteamSpatialInstance::_process_silence() const { return false; }
bool AudioEffectSteamSpatialInstance::is_ready() const { return configured; }

void AudioEffectSteamSpatialInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_ready"), &AudioEffectSteamSpatialInstance::is_ready);
}

} // namespace godot
