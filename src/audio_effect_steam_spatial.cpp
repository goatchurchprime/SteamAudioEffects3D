#include "audio_effect_steam_spatial.hpp"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/variant/basis.hpp>

namespace godot {

Ref<AudioEffectInstance> AudioEffectSteamSpatial::_instantiate() {
	Ref<AudioEffectSteamSpatialInstance> instance;
	instance.instantiate();
	instance->set_parameters(parameters);
	return instance;
}

static void store_spatial_transform(const std::shared_ptr<AudioEffectSteamSpatial::Parameters> &parameters,
		const Vector3 &source, const Vector3 &receiver, const Quaternion &orientation) {
	parameters->transform_sequence.fetch_add(1, std::memory_order_acq_rel);
	parameters->source_x.store(source.x, std::memory_order_relaxed);
	parameters->source_y.store(source.y, std::memory_order_relaxed);
	parameters->source_z.store(source.z, std::memory_order_relaxed);
	parameters->receiver_x.store(receiver.x, std::memory_order_relaxed);
	parameters->receiver_y.store(receiver.y, std::memory_order_relaxed);
	parameters->receiver_z.store(receiver.z, std::memory_order_relaxed);
	parameters->receiver_qx.store(orientation.x, std::memory_order_relaxed);
	parameters->receiver_qy.store(orientation.y, std::memory_order_relaxed);
	parameters->receiver_qz.store(orientation.z, std::memory_order_relaxed);
	parameters->receiver_qw.store(orientation.w, std::memory_order_relaxed);
	parameters->transform_sequence.fetch_add(1, std::memory_order_release);
}

void AudioEffectSteamSpatial::set_source_position(const Vector3 &value) {
	source_position = value;
	store_spatial_transform(parameters, source_position, receiver_position, receiver_orientation);
}

Vector3 AudioEffectSteamSpatial::get_source_position() const { return source_position; }

void AudioEffectSteamSpatial::set_receiver_position(const Vector3 &value) {
	receiver_position = value;
	store_spatial_transform(parameters, source_position, receiver_position, receiver_orientation);
}

Vector3 AudioEffectSteamSpatial::get_receiver_position() const { return receiver_position; }

void AudioEffectSteamSpatial::set_receiver_orientation(const Quaternion &value) {
	receiver_orientation = value.length_squared() > 0.000001f ? value.normalized() : Quaternion();
	store_spatial_transform(parameters, source_position, receiver_position, receiver_orientation);
}

Quaternion AudioEffectSteamSpatial::get_receiver_orientation() const { return receiver_orientation; }

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

void AudioEffectSteamSpatial::set_spatial_parameters(const Vector3 &source_global_position, const Vector3 &receiver_global_position,
		const Quaternion &receiver_global_orientation, float source_gain, float source_spatial_blend) {
	source_position = source_global_position;
	receiver_position = receiver_global_position;
	receiver_orientation = receiver_global_orientation.length_squared() > 0.000001f ? receiver_global_orientation.normalized() : Quaternion();
	store_spatial_transform(parameters, source_position, receiver_position, receiver_orientation);
	set_gain(source_gain);
	set_spatial_blend(source_spatial_blend);
}

void AudioEffectSteamSpatial::set_diagnostic_trigger_peak(float value) {
	parameters->diagnostic_trigger_peak.store(std::max(value, 0.001f), std::memory_order_relaxed);
}

float AudioEffectSteamSpatial::get_diagnostic_trigger_peak() const {
	return parameters->diagnostic_trigger_peak.load(std::memory_order_relaxed);
}

bool AudioEffectSteamSpatial::is_diagnostic_capture_ready() const {
	return parameters->diagnostic_triggered.load(std::memory_order_acquire);
}

Dictionary AudioEffectSteamSpatial::get_diagnostic_report() const {
	Dictionary report;
	if (!is_diagnostic_capture_ready()) {
		return report;
	}
	report["stage"] = "hrtf_spatializer";
	report["input_peak"] = parameters->diagnostic_input_peak.load(std::memory_order_relaxed);
	report["mono_peak_after_gain"] = parameters->diagnostic_mono_peak.load(std::memory_order_relaxed);
	report["output_peak"] = parameters->diagnostic_output_peak.load(std::memory_order_relaxed);
	report["input_finite"] = parameters->diagnostic_input_finite.load(std::memory_order_relaxed);
	report["output_finite"] = parameters->diagnostic_output_finite.load(std::memory_order_relaxed);
	report["gain"] = parameters->gain.load(std::memory_order_relaxed);
	report["spatial_blend"] = parameters->spatial_blend.load(std::memory_order_relaxed);
	report["source_position"] = source_position;
	report["receiver_position"] = receiver_position;
	report["receiver_orientation"] = receiver_orientation;
	return report;
}

void AudioEffectSteamSpatial::reset_diagnostic_capture() {
	parameters->diagnostic_triggered.store(false, std::memory_order_release);
}

void AudioEffectSteamSpatial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source_position", "source_position"), &AudioEffectSteamSpatial::set_source_position);
	ClassDB::bind_method(D_METHOD("get_source_position"), &AudioEffectSteamSpatial::get_source_position);
	ClassDB::bind_method(D_METHOD("set_receiver_position", "receiver_position"), &AudioEffectSteamSpatial::set_receiver_position);
	ClassDB::bind_method(D_METHOD("get_receiver_position"), &AudioEffectSteamSpatial::get_receiver_position);
	ClassDB::bind_method(D_METHOD("set_receiver_orientation", "receiver_orientation"), &AudioEffectSteamSpatial::set_receiver_orientation);
	ClassDB::bind_method(D_METHOD("get_receiver_orientation"), &AudioEffectSteamSpatial::get_receiver_orientation);
	ClassDB::bind_method(D_METHOD("set_gain", "gain"), &AudioEffectSteamSpatial::set_gain);
	ClassDB::bind_method(D_METHOD("get_gain"), &AudioEffectSteamSpatial::get_gain);
	ClassDB::bind_method(D_METHOD("set_spatial_blend", "spatial_blend"), &AudioEffectSteamSpatial::set_spatial_blend);
	ClassDB::bind_method(D_METHOD("get_spatial_blend"), &AudioEffectSteamSpatial::get_spatial_blend);
	ClassDB::bind_method(D_METHOD("set_interpolation", "interpolation"), &AudioEffectSteamSpatial::set_interpolation);
	ClassDB::bind_method(D_METHOD("get_interpolation"), &AudioEffectSteamSpatial::get_interpolation);
	ClassDB::bind_method(D_METHOD("set_spatial_parameters", "source_position", "receiver_position", "receiver_orientation", "gain", "spatial_blend"), &AudioEffectSteamSpatial::set_spatial_parameters, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("set_diagnostic_trigger_peak", "peak"), &AudioEffectSteamSpatial::set_diagnostic_trigger_peak);
	ClassDB::bind_method(D_METHOD("get_diagnostic_trigger_peak"), &AudioEffectSteamSpatial::get_diagnostic_trigger_peak);
	ClassDB::bind_method(D_METHOD("is_diagnostic_capture_ready"), &AudioEffectSteamSpatial::is_diagnostic_capture_ready);
	ClassDB::bind_method(D_METHOD("get_diagnostic_report"), &AudioEffectSteamSpatial::get_diagnostic_report);
	ClassDB::bind_method(D_METHOD("reset_diagnostic_capture"), &AudioEffectSteamSpatial::reset_diagnostic_capture);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "source_position"), "set_source_position", "get_source_position");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "receiver_position"), "set_receiver_position", "get_receiver_position");
	ADD_PROPERTY(PropertyInfo(Variant::QUATERNION, "receiver_orientation"), "set_receiver_orientation", "get_receiver_orientation");
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
	float input_peak = 0.0f;
	float mono_peak = 0.0f;
	bool input_finite = true;
	for (int32_t i = 0; i < frame_count; ++i) {
		mono[i] = 0.5f * (src[i].left + src[i].right) * gain;
		const float input_values[2] = { src[i].left, src[i].right };
		for (float value : input_values) {
			input_finite = input_finite && std::isfinite(value);
			if (std::isfinite(value)) input_peak = std::max(input_peak, std::abs(value));
		}
		if (std::isfinite(mono[i])) mono_peak = std::max(mono_peak, std::abs(mono[i]));
	}

	// Never spin or lock on the audio thread. If the main thread is in the
	// middle of publishing a transform, retain the last coherent snapshot for
	// one block and pick up the new state on the next callback.
	const uint32_t sequence_before = parameters->transform_sequence.load(std::memory_order_acquire);
	if (!(sequence_before & 1U)) {
		const Vector3 source_candidate(parameters->source_x.load(std::memory_order_relaxed), parameters->source_y.load(std::memory_order_relaxed), parameters->source_z.load(std::memory_order_relaxed));
		const Vector3 receiver_candidate(parameters->receiver_x.load(std::memory_order_relaxed), parameters->receiver_y.load(std::memory_order_relaxed), parameters->receiver_z.load(std::memory_order_relaxed));
		const Quaternion orientation_candidate(parameters->receiver_qx.load(std::memory_order_relaxed), parameters->receiver_qy.load(std::memory_order_relaxed), parameters->receiver_qz.load(std::memory_order_relaxed), parameters->receiver_qw.load(std::memory_order_relaxed));
		if (parameters->transform_sequence.load(std::memory_order_acquire) == sequence_before) {
			last_source_position = source_candidate;
			last_receiver_position = receiver_candidate;
			last_receiver_orientation = orientation_candidate;
		}
	}
	Vector3 listener_direction = last_source_position - last_receiver_position;
	if (listener_direction.length_squared() > 0.000001f) {
		listener_direction = Basis(last_receiver_orientation.inverse()).xform(listener_direction.normalized());
	} else {
		listener_direction = Vector3(0.0f, 0.0f, -1.0f);
	}
	IPLVector3 direction{ listener_direction.x, listener_direction.y, listener_direction.z };

	IPLBinauralEffectParams effect_parameters{};
	effect_parameters.direction = direction;
	effect_parameters.interpolation = parameters->interpolation.load(std::memory_order_relaxed) == AudioEffectSteamSpatial::INTERPOLATION_NEAREST
			? IPL_HRTFINTERPOLATION_NEAREST
			: IPL_HRTFINTERPOLATION_BILINEAR;
	effect_parameters.spatialBlend = parameters->spatial_blend.load(std::memory_order_relaxed);
	effect_parameters.hrtf = hrtf;
	effect_parameters.peakDelays = nullptr;
	iplBinauralEffectApply(binaural_effect, &effect_parameters, &input_buffer, &output_buffer);

	float output_peak = 0.0f;
	bool output_finite = true;
	for (int32_t i = 0; i < frame_count; ++i) {
		dst_buffer[i].left = output_buffer.data[0][i];
		dst_buffer[i].right = output_buffer.data[1][i];
		const float output_values[2] = { dst_buffer[i].left, dst_buffer[i].right };
		for (float value : output_values) {
			output_finite = output_finite && std::isfinite(value);
			if (std::isfinite(value)) output_peak = std::max(output_peak, std::abs(value));
		}
	}
	const float threshold = parameters->diagnostic_trigger_peak.load(std::memory_order_relaxed);
	if ((!input_finite || !output_finite || input_peak > threshold || mono_peak > threshold || output_peak > threshold) &&
			!parameters->diagnostic_triggered.load(std::memory_order_relaxed)) {
		parameters->diagnostic_input_peak.store(input_peak, std::memory_order_relaxed);
		parameters->diagnostic_mono_peak.store(mono_peak, std::memory_order_relaxed);
		parameters->diagnostic_output_peak.store(output_peak, std::memory_order_relaxed);
		parameters->diagnostic_input_finite.store(input_finite, std::memory_order_relaxed);
		parameters->diagnostic_output_finite.store(output_finite, std::memory_order_relaxed);
		parameters->diagnostic_triggered.store(true, std::memory_order_release);
	}
}

bool AudioEffectSteamSpatialInstance::_process_silence() const { return false; }
bool AudioEffectSteamSpatialInstance::is_ready() const { return configured; }

void AudioEffectSteamSpatialInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_ready"), &AudioEffectSteamSpatialInstance::is_ready);
}

} // namespace godot
