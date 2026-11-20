#include "audio_effect_steam_reflection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

namespace {

struct SignalStatistics {
	float peak = 0.0f;
	float rms = 0.0f;
	bool finite = true;
};

SignalStatistics calculate_statistics(const float *samples, int32_t frame_count) {
	SignalStatistics result;
	double sum_squares = 0.0;
	for (int32_t sample = 0; sample < frame_count; ++sample) {
		const float value = samples[sample];
		if (!std::isfinite(value)) {
			result.finite = false;
			continue;
		}
		result.peak = std::max(result.peak, std::abs(value));
		sum_squares += static_cast<double>(value) * value;
	}
	result.rms = frame_count > 0 ? static_cast<float>(std::sqrt(sum_squares / frame_count)) : 0.0f;
	return result;
}

SignalStatistics calculate_stereo_statistics(const float *left, const float *right, int32_t frame_count) {
	SignalStatistics result;
	double sum_squares = 0.0;
	for (int32_t sample = 0; sample < frame_count; ++sample) {
		const float values[2] = { left[sample], right[sample] };
		for (float value : values) {
			if (!std::isfinite(value)) {
				result.finite = false;
				continue;
			}
			result.peak = std::max(result.peak, std::abs(value));
			sum_squares += static_cast<double>(value) * value;
		}
	}
	result.rms = frame_count > 0 ? static_cast<float>(std::sqrt(sum_squares / (2.0 * frame_count))) : 0.0f;
	return result;
}

template <size_t N>
void capture_samples(const float *source, int32_t frame_count, std::array<float, N> &destination) {
	if (!source || frame_count <= 0) {
		destination.fill(0.0f);
		return;
	}
	for (size_t index = 0; index < N; ++index) {
		const int32_t source_index = std::min<int32_t>(static_cast<int32_t>((index * frame_count) / N), frame_count - 1);
		destination[index] = source[source_index];
	}
}

template <size_t N>
PackedFloat32Array to_packed_array(const std::array<float, N> &values) {
	PackedFloat32Array result;
	result.resize(N);
	for (size_t index = 0; index < N; ++index) {
		result.set(index, values[index]);
	}
	return result;
}

} // namespace

Ref<AudioEffectInstance> AudioEffectSteamReflectionSend::_instantiate() {
	Ref<AudioEffectSteamReflectionSendInstance> instance;
	instance.instantiate();
	instance->set_configuration(parameters, target);
	return instance;
}

void AudioEffectSteamReflectionSend::set_enabled(bool value) {
	enabled = value;
	parameters->enabled.store(value, std::memory_order_relaxed);
}

bool AudioEffectSteamReflectionSend::is_enabled() const { return enabled; }

void AudioEffectSteamReflectionSend::set_send_gain(float value) {
	send_gain = std::max(value, 0.0f);
	parameters->send_gain.store(send_gain, std::memory_order_relaxed);
}

float AudioEffectSteamReflectionSend::get_send_gain() const { return send_gain; }

void AudioEffectSteamReflectionSend::set_reverb_times(const Vector3 &value) {
	reverb_times = Vector3(
			std::clamp(value.x, 0.1f, 20.0f),
			std::clamp(value.y, 0.1f, 20.0f),
			std::clamp(value.z, 0.1f, 20.0f));
	// Publish the three bands as one logical value. The audio callback uses the
	// revision as a seqlock, preventing a half-old/half-new RT60 preset.
	parameters->reverb_revision.fetch_add(1, std::memory_order_acq_rel);
	parameters->reverb_time_low.store(reverb_times.x, std::memory_order_relaxed);
	parameters->reverb_time_mid.store(reverb_times.y, std::memory_order_relaxed);
	parameters->reverb_time_high.store(reverb_times.z, std::memory_order_relaxed);
	parameters->reverb_revision.fetch_add(1, std::memory_order_release);
}

Vector3 AudioEffectSteamReflectionSend::get_reverb_times() const { return reverb_times; }

void AudioEffectSteamReflectionSend::rebuild_energy_field() {
	if (energy_field.is_empty()) {
		std::atomic_store_explicit(&parameters->energy_field_ir,
				std::shared_ptr<const SteamEnergyFieldIR>{}, std::memory_order_release);
		return;
	}
	const int sample_rate = static_cast<int>(AudioServer::get_singleton()->get_mix_rate());
	String error;
	auto prepared = prepare_steam_energy_field_ir(energy_field, energy_field_duration, sample_rate, &error);
	if (!prepared) {
		UtilityFunctions::push_error("AudioEffectSteamReflectionSend: ", error);
	}
	std::atomic_store_explicit(&parameters->energy_field_ir, prepared, std::memory_order_release);
}

void AudioEffectSteamReflectionSend::set_energy_field_duration(float value) {
	const float clamped = std::clamp(value, 0.01f, 10.0f);
	if (energy_field_duration == clamped) {
		return;
	}
	energy_field_duration = clamped;
	rebuild_energy_field();
}

float AudioEffectSteamReflectionSend::get_energy_field_duration() const { return energy_field_duration; }

void AudioEffectSteamReflectionSend::set_energy_field(const PackedFloat32Array &value) {
	if (energy_field == value) {
		return;
	}
	energy_field = value;
	rebuild_energy_field();
}

PackedFloat32Array AudioEffectSteamReflectionSend::get_energy_field() const { return energy_field; }

int AudioEffectSteamReflectionSend::get_energy_field_bin_count() const {
	return static_cast<int>(std::ceil(energy_field_duration / 0.01f));
}

void AudioEffectSteamReflectionSend::clear_energy_field() { set_energy_field(PackedFloat32Array()); }

void AudioEffectSteamReflectionSend::set_target(const Ref<AudioEffectSteamReflectionHRTF> &value) {
	target = value;
}

Ref<AudioEffectSteamReflectionHRTF> AudioEffectSteamReflectionSend::get_target() const { return target; }

void AudioEffectSteamReflectionSend::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &AudioEffectSteamReflectionSend::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &AudioEffectSteamReflectionSend::is_enabled);
	ClassDB::bind_method(D_METHOD("set_send_gain", "gain"), &AudioEffectSteamReflectionSend::set_send_gain);
	ClassDB::bind_method(D_METHOD("get_send_gain"), &AudioEffectSteamReflectionSend::get_send_gain);
	ClassDB::bind_method(D_METHOD("set_reverb_times", "seconds"), &AudioEffectSteamReflectionSend::set_reverb_times);
	ClassDB::bind_method(D_METHOD("get_reverb_times"), &AudioEffectSteamReflectionSend::get_reverb_times);
	ClassDB::bind_method(D_METHOD("set_energy_field_duration", "seconds"), &AudioEffectSteamReflectionSend::set_energy_field_duration);
	ClassDB::bind_method(D_METHOD("get_energy_field_duration"), &AudioEffectSteamReflectionSend::get_energy_field_duration);
	ClassDB::bind_method(D_METHOD("set_energy_field", "values"), &AudioEffectSteamReflectionSend::set_energy_field);
	ClassDB::bind_method(D_METHOD("get_energy_field"), &AudioEffectSteamReflectionSend::get_energy_field);
	ClassDB::bind_method(D_METHOD("get_energy_field_bin_count"), &AudioEffectSteamReflectionSend::get_energy_field_bin_count);
	ClassDB::bind_method(D_METHOD("clear_energy_field"), &AudioEffectSteamReflectionSend::clear_energy_field);
	ClassDB::bind_method(D_METHOD("set_target", "target"), &AudioEffectSteamReflectionSend::set_target);
	ClassDB::bind_method(D_METHOD("get_target"), &AudioEffectSteamReflectionSend::get_target);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "send_gain", PROPERTY_HINT_RANGE, "0,4,0.001,or_greater"), "set_send_gain", "get_send_gain");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "reverb_times", PROPERTY_HINT_RANGE, "0.1,20,0.01,suffix:s"), "set_reverb_times", "get_reverb_times");
	ADD_GROUP("Directional Energy Field", "energy_field_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy_field_duration", PROPERTY_HINT_RANGE, "0.01,10,0.01,suffix:s"), "set_energy_field_duration", "get_energy_field_duration");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "energy_field"), "set_energy_field", "get_energy_field");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "AudioEffectSteamReflectionHRTF"), "set_target", "get_target");
}

AudioEffectSteamReflectionSendInstance::~AudioEffectSteamReflectionSendInstance() {
	if (target.is_valid()) {
		target->diagnostic_event(AudioEffectSteamReflectionHRTF::EVENT_SEND_RELEASE);
	}
	release_steam_audio();
}

void AudioEffectSteamReflectionSendInstance::release_steam_audio() {
	if (context && reverb_buffer.data) {
		iplAudioBufferFree(context, &reverb_buffer);
	}
	if (reflection_effect) {
		iplReflectionEffectRelease(&reflection_effect);
	}
	if (context) {
		iplContextRelease(&context);
	}
	input_buffer = {};
	reverb_buffer = {};
	processing_frame_size = 0;
	tail_remaining.store(false, std::memory_order_relaxed);
}

void AudioEffectSteamReflectionSendInstance::set_configuration(
		const std::shared_ptr<AudioEffectSteamReflectionSend::Parameters> &parameter_values,
		const Ref<AudioEffectSteamReflectionHRTF> &target_value) {
	parameters = parameter_values;
	target = target_value;
	sampling_rate = static_cast<int32_t>(AudioServer::get_singleton()->get_mix_rate());
	configured = sampling_rate > 0;
	if (target.is_valid()) {
		target->diagnostic_event(AudioEffectSteamReflectionHRTF::EVENT_SEND_CREATE);
	}
}

bool AudioEffectSteamReflectionSendInstance::ensure_processing_size(int32_t frame_count) {
	if (!configured || frame_count <= 0) {
		return false;
	}
	if (reflection_effect && reverb_buffer.data && processing_frame_size == frame_count) {
		return true;
	}
	if (processing_frame_size != 0) {
		print_verbose("AudioEffectSteamReflectionSend: processing frame size changed from ", processing_frame_size, " to ", frame_count, "; rebuilding Steam Audio buffers.");
		if (target.is_valid()) {
			target->diagnostic_event(AudioEffectSteamReflectionHRTF::EVENT_SEND_REBUILD);
		}
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

	IPLReflectionEffectSettings effect_settings{};
	effect_settings.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
	effect_settings.irSize = 0;
	effect_settings.numChannels = 1;
	if (iplReflectionEffectCreate(context, &audio_settings, &effect_settings, &reflection_effect) != IPL_STATUS_SUCCESS) {
		release_steam_audio();
		return false;
	}

	mono.assign(frame_count, 0.0f);
	for (auto &channel : ambisonics) {
		channel.assign(frame_count, 0.0f);
	}
	input_channels[0] = mono.data();
	input_buffer.numChannels = 1;
	input_buffer.numSamples = frame_count;
	input_buffer.data = input_channels.data();
	if (iplAudioBufferAllocate(context, 1, frame_count, &reverb_buffer) != IPL_STATUS_SUCCESS) {
		release_steam_audio();
		return false;
	}
	for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
		ambisonic_channels[channel] = ambisonics[channel].data();
	}
	processing_frame_size = frame_count;
	return true;
}

void AudioEffectSteamReflectionSendInstance::submit_reverb_frame(float gain, const ReflectionDiagnosticSendData &diagnostics) {
	if (target.is_null() || !reverb_buffer.data) {
		return;
	}
	for (auto &channel : ambisonics) {
		std::fill(channel.begin(), channel.end(), 0.0f);
	}
	for (int sample = 0; sample < processing_frame_size; ++sample) {
		ambisonics[0][sample] = reverb_buffer.data[0][sample];
	}
	IPLAudioBuffer ambisonic_buffer{};
	ambisonic_buffer.numChannels = CHANNEL_COUNT;
	ambisonic_buffer.numSamples = processing_frame_size;
	ambisonic_buffer.data = ambisonic_channels.data();
	target->mix_in(this, &ambisonic_buffer, gain, diagnostics);
}

std::array<float, 3> AudioEffectSteamReflectionSendInstance::load_reverb_times_coherently() {
	for (int attempt = 0; attempt < 3; ++attempt) {
		const uint32_t before = parameters->reverb_revision.load(std::memory_order_acquire);
		if ((before & 1u) != 0) {
			continue;
		}
		const std::array<float, 3> result = {
			parameters->reverb_time_low.load(std::memory_order_relaxed),
			parameters->reverb_time_mid.load(std::memory_order_relaxed),
			parameters->reverb_time_high.load(std::memory_order_relaxed),
		};
		const uint32_t after = parameters->reverb_revision.load(std::memory_order_acquire);
		if (before == after) {
			return result;
		}
	}
	return have_previous_reverb_parameters ? previous_reverb_times : std::array<float, 3>{ 1.0f, 0.8f, 0.5f };
}

void AudioEffectSteamReflectionSendInstance::_process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) {
	const auto *src = static_cast<const AudioFrame *>(src_buffer);
	std::copy_n(src, frame_count, dst_buffer);
	if (!parameters || !ensure_processing_size(frame_count)) {
		return;
	}

	const bool enabled = parameters->enabled.load(std::memory_order_relaxed);
	if (!enabled) {
		if (was_enabled) {
			iplReflectionEffectReset(reflection_effect);
			energy_field_convolver.reset();
			tail_remaining.store(false, std::memory_order_relaxed);
			if (target.is_valid()) {
				target->diagnostic_event(AudioEffectSteamReflectionHRTF::EVENT_SEND_RESET);
			}
		}
		was_enabled = false;
		return;
	}
	was_enabled = true;

	bool has_input = false;
	for (int32_t sample = 0; sample < frame_count; ++sample) {
		mono[sample] = 0.5f * (src[sample].left + src[sample].right);
		has_input = has_input || std::abs(mono[sample]) > 0.0000001f;
	}

	ReflectionDiagnosticSendData diagnostics;
	diagnostics.enabled = enabled;
	diagnostics.had_input = has_input;
	diagnostics.source_finite = true;
	double source_sum_squares = 0.0;
	for (int32_t sample = 0; sample < frame_count; ++sample) {
		const float value = mono[sample];
		if (!std::isfinite(value)) {
			diagnostics.source_finite = false;
		} else {
			diagnostics.source_peak = std::max(diagnostics.source_peak, std::abs(value));
			source_sum_squares += static_cast<double>(value) * value;
		}
	}
	diagnostics.source_rms = frame_count > 0 ? static_cast<float>(std::sqrt(source_sum_squares / frame_count)) : 0.0f;
	capture_samples(mono.data(), frame_count, diagnostics.source_samples);
	if (process_energy_field(frame_count, diagnostics)) {
		return;
	}

	IPLAudioEffectState effect_state = IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
	if (has_input) {
		IPLReflectionEffectParams effect_parameters{};
		effect_parameters.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
		diagnostics.reverb_times = load_reverb_times_coherently();
		effect_parameters.reverbTimes[0] = diagnostics.reverb_times[0];
		effect_parameters.reverbTimes[1] = diagnostics.reverb_times[1];
		effect_parameters.reverbTimes[2] = diagnostics.reverb_times[2];
		for (int band = 0; band < 3; ++band) {
			diagnostics.previous_reverb_times[band] = have_previous_reverb_parameters ? previous_reverb_times[band] : diagnostics.reverb_times[band];
			diagnostics.reverb_time_deltas[band] = diagnostics.reverb_times[band] - diagnostics.previous_reverb_times[band];
			diagnostics.reverb_parameters_changed = diagnostics.reverb_parameters_changed || std::abs(diagnostics.reverb_time_deltas[band]) > 0.000001f;
			previous_reverb_times[band] = diagnostics.reverb_times[band];
		}
		have_previous_reverb_parameters = true;
		effect_parameters.numChannels = 1;
		effect_state = iplReflectionEffectApply(reflection_effect, &effect_parameters, &input_buffer, &reverb_buffer, nullptr);
		diagnostics.used_apply = true;
	} else if (tail_remaining.load(std::memory_order_relaxed)) {
		diagnostics.reverb_times = load_reverb_times_coherently();
		for (int band = 0; band < 3; ++band) {
			diagnostics.previous_reverb_times[band] = have_previous_reverb_parameters ? previous_reverb_times[band] : diagnostics.reverb_times[band];
			diagnostics.reverb_time_deltas[band] = diagnostics.reverb_times[band] - diagnostics.previous_reverb_times[band];
		}
		effect_state = iplReflectionEffectGetTail(reflection_effect, &reverb_buffer, nullptr);
		diagnostics.used_tail = true;
	} else {
		return;
	}
	tail_remaining.store(effect_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING, std::memory_order_relaxed);
	diagnostics.effect_state = static_cast<int>(effect_state);
	diagnostics.send_gain = parameters->send_gain.load(std::memory_order_relaxed);
	const SignalStatistics reflection_statistics = calculate_statistics(reverb_buffer.data[0], frame_count);
	diagnostics.reflection_peak = reflection_statistics.peak;
	diagnostics.reflection_rms = reflection_statistics.rms;
	diagnostics.reflection_finite = reflection_statistics.finite;
	capture_samples(reverb_buffer.data[0], frame_count, diagnostics.reflection_samples);

	submit_reverb_frame(diagnostics.send_gain, diagnostics);
}

bool AudioEffectSteamReflectionSendInstance::process_energy_field(
		int32_t frame_count, ReflectionDiagnosticSendData &diagnostics) {
	auto prepared = std::atomic_load_explicit(&parameters->energy_field_ir, std::memory_order_acquire);
	if (prepared != energy_field_convolver.get_ir()) {
		energy_field_convolver.set_ir(prepared);
	}
	if (!prepared) {
		return false;
	}
	energy_field_convolver.process(mono.data(), frame_count, ambisonics);
	for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
		ambisonic_channels[channel] = ambisonics[channel].data();
	}
	diagnostics.used_apply = true;
	diagnostics.effect_state = energy_field_convolver.has_tail() ?
			static_cast<int>(IPL_AUDIOEFFECTSTATE_TAILREMAINING) :
			static_cast<int>(IPL_AUDIOEFFECTSTATE_TAILCOMPLETE);
	diagnostics.send_gain = parameters->send_gain.load(std::memory_order_relaxed);
	const SignalStatistics statistics = calculate_statistics(ambisonics[0].data(), frame_count);
	diagnostics.reflection_peak = statistics.peak;
	diagnostics.reflection_rms = statistics.rms;
	diagnostics.reflection_finite = statistics.finite;
	capture_samples(ambisonics[0].data(), frame_count, diagnostics.reflection_samples);
	tail_remaining.store(energy_field_convolver.has_tail(), std::memory_order_relaxed);
	IPLAudioBuffer ambisonic_buffer{};
	ambisonic_buffer.numChannels = CHANNEL_COUNT;
	ambisonic_buffer.numSamples = frame_count;
	ambisonic_buffer.data = ambisonic_channels.data();
	if (target.is_valid()) {
		target->mix_in(this, &ambisonic_buffer, diagnostics.send_gain, diagnostics);
	}
	return true;
}

bool AudioEffectSteamReflectionSendInstance::_process_silence() const {
	return tail_remaining.load(std::memory_order_relaxed) || energy_field_convolver.has_tail();
}

bool AudioEffectSteamReflectionSendInstance::is_ready() const { return configured; }

void AudioEffectSteamReflectionSendInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_ready"), &AudioEffectSteamReflectionSendInstance::is_ready);
}

Ref<AudioEffectInstance> AudioEffectSteamReflectionHRTF::_instantiate() {
	Ref<AudioEffectSteamReflectionHRTFInstance> instance;
	instance.instantiate();
	instance->set_configuration(parameters, this);
	return instance;
}

void AudioEffectSteamReflectionHRTF::set_listener_orientation(const Basis &value) {
	listener_orientation = value.orthonormalized();
	const Vector3 right = listener_orientation.get_column(0);
	const Vector3 up = listener_orientation.get_column(1);
	const Vector3 ahead = -listener_orientation.get_column(2);
	parameters->right_x.store(right.x, std::memory_order_relaxed);
	parameters->right_y.store(right.y, std::memory_order_relaxed);
	parameters->right_z.store(right.z, std::memory_order_relaxed);
	parameters->up_x.store(up.x, std::memory_order_relaxed);
	parameters->up_y.store(up.y, std::memory_order_relaxed);
	parameters->up_z.store(up.z, std::memory_order_relaxed);
	parameters->ahead_x.store(ahead.x, std::memory_order_relaxed);
	parameters->ahead_y.store(ahead.y, std::memory_order_relaxed);
	parameters->ahead_z.store(ahead.z, std::memory_order_release);
}

Basis AudioEffectSteamReflectionHRTF::get_listener_orientation() const { return listener_orientation; }

void AudioEffectSteamReflectionHRTF::set_dry_gain(float value) {
	dry_gain = std::max(value, 0.0f);
	parameters->dry_gain.store(dry_gain, std::memory_order_relaxed);
}

float AudioEffectSteamReflectionHRTF::get_dry_gain() const { return dry_gain; }

void AudioEffectSteamReflectionHRTF::set_wet_gain(float value) {
	wet_gain = std::max(value, 0.0f);
	parameters->wet_gain.store(wet_gain, std::memory_order_relaxed);
}

float AudioEffectSteamReflectionHRTF::get_wet_gain() const { return wet_gain; }

void AudioEffectSteamReflectionHRTF::set_listener_parameters(const Basis &orientation, float gain) {
	set_listener_orientation(orientation);
	set_wet_gain(gain);
}

AudioEffectSteamReflectionHRTF::DiagnosticBlock *AudioEffectSteamReflectionHRTF::begin_diagnostic_block(
		int32_t frame_count, uint64_t sender_identity, bool began_new_chunk, bool repeated_sender) {
	if (!diagnostic_enabled.load(std::memory_order_relaxed) || diagnostic_frozen.load(std::memory_order_acquire)) {
		return nullptr;
	}
	const uint64_t block_id = diagnostic_next_block_id++;
	const uint64_t trigger_id = diagnostic_trigger_block_id.load(std::memory_order_relaxed);
	if (trigger_id != 0 && block_id > trigger_id + DIAGNOSTIC_POST_TRIGGER_BLOCKS) {
		diagnostic_frozen.store(true, std::memory_order_release);
		return nullptr;
	}
	DiagnosticBlock &block = diagnostic_blocks[block_id % DIAGNOSTIC_BLOCK_COUNT];
	block = DiagnosticBlock{};
	block.block_id = block_id;
	block.sender_identity = sender_identity;
	block.source_identity = diagnostic_source_identity.load(std::memory_order_relaxed);
	block.events = pending_diagnostic_events.exchange(0, std::memory_order_acq_rel);
	block.frame_count = frame_count;
	block.began_new_chunk = began_new_chunk;
	block.repeated_sender = repeated_sender;
	current_diagnostic_block_id = block_id;
	return &block;
}

AudioEffectSteamReflectionHRTF::DiagnosticBlock *AudioEffectSteamReflectionHRTF::find_diagnostic_block(uint64_t block_id) {
	if (block_id == 0) {
		return nullptr;
	}
	DiagnosticBlock &block = diagnostic_blocks[block_id % DIAGNOSTIC_BLOCK_COUNT];
	return block.block_id == block_id ? &block : nullptr;
}

void AudioEffectSteamReflectionHRTF::evaluate_diagnostic_trigger(DiagnosticBlock &block) {
	const float threshold = diagnostic_trigger_peak.load(std::memory_order_relaxed);
	const float source_scale = std::max(block.send.source_peak, 0.000001f);
	const float ambisonic_peak = *std::max_element(block.ambisonic_peaks.begin(), block.ambisonic_peaks.end());
	const float ambisonic_scale = std::max(ambisonic_peak, 0.000001f);
	const bool non_finite = !block.send.source_finite || !block.send.reflection_finite ||
			!block.ambisonics_finite || !block.decode.decoded_finite || !block.decode.final_finite;
	const bool excessive_peak = block.send.source_peak > threshold || block.send.reflection_peak > threshold ||
			ambisonic_peak > threshold || block.decode.decoded_peak > threshold || block.decode.final_peak > threshold;
	// Ratios become meaningless near silence: a perfectly ordinary reverb tail
	// can be hundreds of times larger than a -84 dB input while remaining quiet.
	// Require a material absolute level as well as extreme growth.
	const bool excessive_growth = (block.send.had_input && block.send.source_peak > 0.000001f &&
			block.send.reflection_peak > 0.25f && block.send.reflection_peak / source_scale > 64.0f) ||
			(block.decode.used_apply && ambisonic_peak > 0.000001f &&
			block.decode.decoded_peak > 0.25f && block.decode.decoded_peak / ambisonic_scale > 64.0f);
	if (!(non_finite || excessive_peak || excessive_growth)) {
		return;
	}
	bool expected = false;
	if (diagnostic_triggered.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
		diagnostic_trigger_block_id.store(block.block_id, std::memory_order_release);
		block.triggered = true;
		// Freeze on the trigger block itself. Waiting for post-trigger blocks made
		// diagnostics unreliable while effects were being toggled: Godot can
		// detach and destroy the offending effect instance before another block is
		// processed, so the report never became ready. The ring already contains
		// the useful pre-trigger history and this block contains every reflection
		// stage through the final stereo output.
		diagnostic_frozen.store(true, std::memory_order_release);
	}
}

Dictionary AudioEffectSteamReflectionHRTF::diagnostic_block_to_dictionary(const DiagnosticBlock &block) {
	Dictionary result;
	result["block_id"] = static_cast<int64_t>(block.block_id);
	result["source_identity"] = static_cast<int64_t>(block.source_identity);
	result["sender_identity"] = static_cast<int64_t>(block.sender_identity);
	result["return_instance_identity"] = static_cast<int64_t>(block.return_instance_identity);
	result["events"] = static_cast<int64_t>(block.events);
	result["frame_count"] = block.frame_count;
	result["sender_count"] = block.sender_count;
	Array senders;
	for (int index = 0; index < block.sender_count; ++index) {
		senders.push_back(static_cast<int64_t>(block.sender_identities[index]));
	}
	result["sender_identities"] = senders;
	result["began_new_chunk"] = block.began_new_chunk;
	result["repeated_sender"] = block.repeated_sender;
	result["triggered"] = block.triggered;
	result["ambisonic_peaks"] = to_packed_array(block.ambisonic_peaks);
	result["ambisonics_finite"] = block.ambisonics_finite;

	Dictionary send;
	send["source_peak"] = block.send.source_peak;
	send["source_rms"] = block.send.source_rms;
	send["reflection_peak"] = block.send.reflection_peak;
	send["reflection_rms"] = block.send.reflection_rms;
	send["source_finite"] = block.send.source_finite;
	send["reflection_finite"] = block.send.reflection_finite;
	send["enabled"] = block.send.enabled;
	send["had_input"] = block.send.had_input;
	send["used_apply"] = block.send.used_apply;
	send["used_tail"] = block.send.used_tail;
	send["effect_state"] = block.send.effect_state;
	send["send_gain"] = block.send.send_gain;
	send["reverb_times"] = to_packed_array(block.send.reverb_times);
	send["previous_reverb_times"] = to_packed_array(block.send.previous_reverb_times);
	send["reverb_time_deltas"] = to_packed_array(block.send.reverb_time_deltas);
	send["reverb_parameters_changed"] = block.send.reverb_parameters_changed;
	send["source_samples"] = to_packed_array(block.send.source_samples);
	send["reflection_samples"] = to_packed_array(block.send.reflection_samples);
	result["send"] = send;

	Dictionary decode;
	decode["decoded_peak"] = block.decode.decoded_peak;
	decode["decoded_rms"] = block.decode.decoded_rms;
	decode["dry_peak"] = block.decode.dry_peak;
	decode["wet_peak"] = block.decode.wet_peak;
	decode["final_peak"] = block.decode.final_peak;
	decode["decoded_finite"] = block.decode.decoded_finite;
	decode["final_finite"] = block.decode.final_finite;
	decode["used_apply"] = block.decode.used_apply;
	decode["used_tail"] = block.decode.used_tail;
	decode["effect_state"] = block.decode.effect_state;
	decode["dry_gain"] = block.decode.dry_gain;
	decode["wet_gain"] = block.decode.wet_gain;
	decode["decoded_left_samples"] = to_packed_array(block.decode.decoded_left_samples);
	decode["decoded_right_samples"] = to_packed_array(block.decode.decoded_right_samples);
	decode["final_left_samples"] = to_packed_array(block.decode.final_left_samples);
	decode["final_right_samples"] = to_packed_array(block.decode.final_right_samples);
	result["decode"] = decode;
	return result;
}

void AudioEffectSteamReflectionHRTF::mix_in(const void *sender, const IPLAudioBuffer *buffer, float gain,
		const ReflectionDiagnosticSendData &diagnostics) {
	if (!buffer || !buffer->data || buffer->numChannels < CHANNEL_COUNT || buffer->numSamples <= 0 || gain <= 0.0f) {
		return;
	}

	bool sender_already_submitted = false;
	for (int index = 0; index < pending_sender_count; ++index) {
		if (pending_senders[index] == sender) {
			sender_already_submitted = true;
			break;
		}
	}

	// Normally consume_ambisonics() clears pending after every bus chunk. If the
	// return effect is bypassed or rebuilt, the same send instance can arrive for
	// a later callback while pending is still true. That repeated sender marks a
	// new chunk, so discard the stale accumulator instead of adding across time.
	const bool begin_new_chunk = !ambisonics_pending ||
			mixed_frame_count != buffer->numSamples ||
			sender_already_submitted ||
			pending_sender_count >= MAX_PENDING_SENDERS;
	if (begin_new_chunk) {
		mixed_frame_count = buffer->numSamples;
		for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
			mixed_ambisonics[channel].assign(mixed_frame_count, 0.0f);
			mixed_ambisonic_channels[channel] = mixed_ambisonics[channel].data();
		}
		pending_sender_count = 0;
	}
	DiagnosticBlock *diagnostic_block = begin_new_chunk ?
			begin_diagnostic_block(buffer->numSamples, reinterpret_cast<uintptr_t>(sender), true, sender_already_submitted) :
			find_diagnostic_block(current_diagnostic_block_id);
	if (diagnostic_block && sender_already_submitted) {
		diagnostic_block->events |= EVENT_STALE_CHUNK_REPLACED;
	}

	for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
		for (int sample = 0; sample < mixed_frame_count; ++sample) {
			mixed_ambisonics[channel][sample] += gain * buffer->data[channel][sample];
		}
	}
	pending_senders[pending_sender_count++] = sender;
	ambisonics_pending = true;
	if (diagnostic_block) {
		const int sender_index = std::min(diagnostic_block->sender_count, MAX_PENDING_SENDERS - 1);
		diagnostic_block->sender_identities[sender_index] = reinterpret_cast<uintptr_t>(sender);
		diagnostic_block->sender_count = std::min(diagnostic_block->sender_count + 1, MAX_PENDING_SENDERS);
		diagnostic_block->send = diagnostics;
		for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
			const SignalStatistics statistics = calculate_statistics(mixed_ambisonics[channel].data(), mixed_frame_count);
			diagnostic_block->ambisonic_peaks[channel] = statistics.peak;
			diagnostic_block->ambisonics_finite = diagnostic_block->ambisonics_finite && statistics.finite;
		}
		evaluate_diagnostic_trigger(*diagnostic_block);
	}
}

bool AudioEffectSteamReflectionHRTF::consume_ambisonics(IPLAudioBuffer *buffer, int32_t frame_count, uint64_t *block_id) {
	if (!buffer || !ambisonics_pending || mixed_frame_count != frame_count) {
		if (block_id) {
			*block_id = 0;
		}
		return false;
	}
	buffer->numChannels = CHANNEL_COUNT;
	buffer->numSamples = mixed_frame_count;
	buffer->data = mixed_ambisonic_channels.data();
	if (block_id) {
		*block_id = current_diagnostic_block_id;
	}
	if (DiagnosticBlock *block = find_diagnostic_block(current_diagnostic_block_id)) {
		block->events |= EVENT_AMBISONICS_CONSUMED;
	}
	return true;
}

void AudioEffectSteamReflectionHRTF::clear_ambisonics() {
	if (DiagnosticBlock *block = find_diagnostic_block(current_diagnostic_block_id)) {
		block->events |= EVENT_AMBISONICS_CLEARED;
	}
	for (auto &channel : mixed_ambisonics) {
		std::fill(channel.begin(), channel.end(), 0.0f);
	}
	ambisonics_pending = false;
	pending_sender_count = 0;
	current_diagnostic_block_id = 0;
}

bool AudioEffectSteamReflectionHRTF::has_pending_ambisonics() const { return ambisonics_pending; }

uint64_t AudioEffectSteamReflectionHRTF::begin_return_tail_diagnostic(int32_t frame_count, const void *return_instance) {
	DiagnosticBlock *block = begin_diagnostic_block(frame_count, 0, true, false);
	if (!block) {
		return 0;
	}
	block->return_instance_identity = reinterpret_cast<uintptr_t>(return_instance);
	block->events |= EVENT_RETURN_TAIL;
	return block->block_id;
}

void AudioEffectSteamReflectionHRTF::record_decode_diagnostic(uint64_t block_id, const void *return_instance,
		const ReflectionDiagnosticDecodeData &diagnostics) {
	if (DiagnosticBlock *block = find_diagnostic_block(block_id)) {
		block->return_instance_identity = reinterpret_cast<uintptr_t>(return_instance);
		block->decode = diagnostics;
		evaluate_diagnostic_trigger(*block);
	}
}

void AudioEffectSteamReflectionHRTF::diagnostic_event(uint32_t event) {
	pending_diagnostic_events.fetch_or(event, std::memory_order_relaxed);
}

void AudioEffectSteamReflectionHRTF::set_diagnostic_source_identity(uint64_t identity) {
	diagnostic_source_identity.store(identity, std::memory_order_relaxed);
}

uint64_t AudioEffectSteamReflectionHRTF::get_diagnostic_source_identity() const {
	return diagnostic_source_identity.load(std::memory_order_relaxed);
}

void AudioEffectSteamReflectionHRTF::set_diagnostic_trigger_peak(float peak) {
	diagnostic_trigger_peak.store(std::max(peak, 0.001f), std::memory_order_relaxed);
}

float AudioEffectSteamReflectionHRTF::get_diagnostic_trigger_peak() const {
	return diagnostic_trigger_peak.load(std::memory_order_relaxed);
}

void AudioEffectSteamReflectionHRTF::set_diagnostic_enabled(bool enabled) {
	diagnostic_enabled.store(enabled, std::memory_order_relaxed);
}

bool AudioEffectSteamReflectionHRTF::is_diagnostic_enabled() const {
	return diagnostic_enabled.load(std::memory_order_relaxed);
}

bool AudioEffectSteamReflectionHRTF::is_diagnostic_capture_ready() const {
	return diagnostic_frozen.load(std::memory_order_acquire);
}

Dictionary AudioEffectSteamReflectionHRTF::get_diagnostic_report() const {
	Dictionary report;
	if (!diagnostic_frozen.load(std::memory_order_acquire)) {
		return report;
	}
	const uint64_t trigger_id = diagnostic_trigger_block_id.load(std::memory_order_acquire);
	report["format"] = "steam_reflection_diagnostic_v1";
	report["source_identity"] = static_cast<int64_t>(diagnostic_source_identity.load(std::memory_order_relaxed));
	report["trigger_block_id"] = static_cast<int64_t>(trigger_id);
	report["trigger_peak"] = diagnostic_trigger_peak.load(std::memory_order_relaxed);
	Array blocks;
	const uint64_t newest = diagnostic_next_block_id > 0 ? diagnostic_next_block_id - 1 : 0;
	const uint64_t first = newest >= DIAGNOSTIC_BLOCK_COUNT ? newest - DIAGNOSTIC_BLOCK_COUNT + 1 : 1;
	for (uint64_t block_id = first; block_id <= newest; ++block_id) {
		const DiagnosticBlock &block = diagnostic_blocks[block_id % DIAGNOSTIC_BLOCK_COUNT];
		if (block.block_id == block_id) {
			blocks.push_back(diagnostic_block_to_dictionary(block));
		}
	}
	report["blocks"] = blocks;
	return report;
}

void AudioEffectSteamReflectionHRTF::reset_diagnostic_capture() {
	const bool was_enabled = diagnostic_enabled.exchange(false, std::memory_order_acq_rel);
	diagnostic_blocks.fill(DiagnosticBlock{});
	diagnostic_next_block_id = 1;
	current_diagnostic_block_id = 0;
	pending_diagnostic_events.store(0, std::memory_order_relaxed);
	diagnostic_triggered.store(false, std::memory_order_relaxed);
	diagnostic_frozen.store(false, std::memory_order_relaxed);
	diagnostic_trigger_block_id.store(0, std::memory_order_relaxed);
	diagnostic_enabled.store(was_enabled, std::memory_order_release);
}

void AudioEffectSteamReflectionHRTF::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_listener_orientation", "orientation"), &AudioEffectSteamReflectionHRTF::set_listener_orientation);
	ClassDB::bind_method(D_METHOD("get_listener_orientation"), &AudioEffectSteamReflectionHRTF::get_listener_orientation);
	ClassDB::bind_method(D_METHOD("set_dry_gain", "gain"), &AudioEffectSteamReflectionHRTF::set_dry_gain);
	ClassDB::bind_method(D_METHOD("get_dry_gain"), &AudioEffectSteamReflectionHRTF::get_dry_gain);
	ClassDB::bind_method(D_METHOD("set_wet_gain", "gain"), &AudioEffectSteamReflectionHRTF::set_wet_gain);
	ClassDB::bind_method(D_METHOD("get_wet_gain"), &AudioEffectSteamReflectionHRTF::get_wet_gain);
	ClassDB::bind_method(D_METHOD("set_listener_parameters", "orientation", "wet_gain"), &AudioEffectSteamReflectionHRTF::set_listener_parameters, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("diagnostic_event", "event"), &AudioEffectSteamReflectionHRTF::diagnostic_event);
	ClassDB::bind_method(D_METHOD("set_diagnostic_source_identity", "identity"), &AudioEffectSteamReflectionHRTF::set_diagnostic_source_identity);
	ClassDB::bind_method(D_METHOD("get_diagnostic_source_identity"), &AudioEffectSteamReflectionHRTF::get_diagnostic_source_identity);
	ClassDB::bind_method(D_METHOD("set_diagnostic_trigger_peak", "peak"), &AudioEffectSteamReflectionHRTF::set_diagnostic_trigger_peak);
	ClassDB::bind_method(D_METHOD("get_diagnostic_trigger_peak"), &AudioEffectSteamReflectionHRTF::get_diagnostic_trigger_peak);
	ClassDB::bind_method(D_METHOD("set_diagnostic_enabled", "enabled"), &AudioEffectSteamReflectionHRTF::set_diagnostic_enabled);
	ClassDB::bind_method(D_METHOD("is_diagnostic_enabled"), &AudioEffectSteamReflectionHRTF::is_diagnostic_enabled);
	ClassDB::bind_method(D_METHOD("is_diagnostic_capture_ready"), &AudioEffectSteamReflectionHRTF::is_diagnostic_capture_ready);
	ClassDB::bind_method(D_METHOD("get_diagnostic_report"), &AudioEffectSteamReflectionHRTF::get_diagnostic_report);
	ClassDB::bind_method(D_METHOD("reset_diagnostic_capture"), &AudioEffectSteamReflectionHRTF::reset_diagnostic_capture);

	ADD_PROPERTY(PropertyInfo(Variant::BASIS, "listener_orientation"), "set_listener_orientation", "get_listener_orientation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dry_gain", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater"), "set_dry_gain", "get_dry_gain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wet_gain", PROPERTY_HINT_RANGE, "0,4,0.001,or_greater"), "set_wet_gain", "get_wet_gain");
	ADD_GROUP("Diagnostics", "diagnostic_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "diagnostic_enabled"), "set_diagnostic_enabled", "is_diagnostic_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "diagnostic_trigger_peak", PROPERTY_HINT_RANGE, "0.001,100,0.001,or_greater"), "set_diagnostic_trigger_peak", "get_diagnostic_trigger_peak");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "diagnostic_source_identity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_diagnostic_source_identity", "get_diagnostic_source_identity");
}

AudioEffectSteamReflectionHRTFInstance::~AudioEffectSteamReflectionHRTFInstance() {
	if (receiver) {
		receiver->diagnostic_event(AudioEffectSteamReflectionHRTF::EVENT_RETURN_RELEASE);
	}
	release_steam_audio();
}

void AudioEffectSteamReflectionHRTFInstance::release_steam_audio() {
	if (context && output_buffer.data) {
		iplAudioBufferFree(context, &output_buffer);
	}
	if (decode_effect) {
		iplAmbisonicsDecodeEffectRelease(&decode_effect);
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
	tail_remaining.store(false, std::memory_order_relaxed);
}

void AudioEffectSteamReflectionHRTFInstance::set_configuration(
		const std::shared_ptr<AudioEffectSteamReflectionHRTF::Parameters> &parameter_values,
		AudioEffectSteamReflectionHRTF *receiver_value) {
	parameters = parameter_values;
	receiver = receiver_value;
	sampling_rate = static_cast<int32_t>(AudioServer::get_singleton()->get_mix_rate());
	configured = receiver && sampling_rate > 0;
	if (receiver) {
		receiver->diagnostic_event(AudioEffectSteamReflectionHRTF::EVENT_RETURN_CREATE);
	}
}

bool AudioEffectSteamReflectionHRTFInstance::ensure_processing_size(int32_t frame_count) {
	if (!configured || frame_count <= 0) {
		return false;
	}
	if (decode_effect && output_buffer.data && processing_frame_size == frame_count) {
		return true;
	}
	if (processing_frame_size != 0) {
		print_verbose("AudioEffectSteamReflectionHRTF: processing frame size changed from ", processing_frame_size, " to ", frame_count, "; rebuilding Steam Audio buffers.");
		if (receiver) {
			receiver->diagnostic_event(AudioEffectSteamReflectionHRTF::EVENT_RETURN_REBUILD);
		}
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

	IPLAmbisonicsDecodeEffectSettings effect_settings{};
	effect_settings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
	effect_settings.hrtf = hrtf;
	effect_settings.maxOrder = 1;
	if (iplAmbisonicsDecodeEffectCreate(context, &audio_settings, &effect_settings, &decode_effect) != IPL_STATUS_SUCCESS) {
		release_steam_audio();
		return false;
	}

	if (iplAudioBufferAllocate(context, 2, frame_count, &output_buffer) != IPL_STATUS_SUCCESS) {
		release_steam_audio();
		return false;
	}
	processing_frame_size = frame_count;
	return true;
}

void AudioEffectSteamReflectionHRTFInstance::mix_wet_output(
		const AudioFrame *dry,
		AudioFrame *destination,
		int32_t frame_count,
		float dry_gain,
		float wet_gain) {
	for (int32_t sample = 0; sample < frame_count; ++sample) {
		destination[sample].left = dry_gain * dry[sample].left + wet_gain * output_buffer.data[0][sample];
		destination[sample].right = dry_gain * dry[sample].right + wet_gain * output_buffer.data[1][sample];
	}
}

void AudioEffectSteamReflectionHRTFInstance::_process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) {
	const auto *src = static_cast<const AudioFrame *>(src_buffer);
	if (!parameters || !receiver || !ensure_processing_size(frame_count)) {
		std::copy_n(src, frame_count, dst_buffer);
		return;
	}

	uint64_t diagnostic_block_id = 0;
	const bool has_submission = receiver->consume_ambisonics(&input_buffer, frame_count, &diagnostic_block_id);
	IPLAudioEffectState effect_state = IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
	bool used_apply = false;
	bool used_tail = false;
	if (has_submission) {
		IPLAmbisonicsDecodeEffectParams effect_parameters{};
		effect_parameters.order = 1;
		effect_parameters.hrtf = hrtf;
		effect_parameters.orientation.right = {
			parameters->right_x.load(std::memory_order_relaxed),
			parameters->right_y.load(std::memory_order_relaxed),
			parameters->right_z.load(std::memory_order_relaxed),
		};
		effect_parameters.orientation.up = {
			parameters->up_x.load(std::memory_order_relaxed),
			parameters->up_y.load(std::memory_order_relaxed),
			parameters->up_z.load(std::memory_order_relaxed),
		};
		effect_parameters.orientation.ahead = {
			parameters->ahead_x.load(std::memory_order_relaxed),
			parameters->ahead_y.load(std::memory_order_relaxed),
			parameters->ahead_z.load(std::memory_order_acquire),
		};
		effect_parameters.binaural = IPL_TRUE;
		effect_state = iplAmbisonicsDecodeEffectApply(decode_effect, &effect_parameters, &input_buffer, &output_buffer);
		used_apply = true;
	} else if (tail_remaining.load(std::memory_order_relaxed)) {
		diagnostic_block_id = receiver->begin_return_tail_diagnostic(frame_count, this);
		effect_state = iplAmbisonicsDecodeEffectGetTail(decode_effect, &output_buffer);
		used_tail = true;
	} else {
		const float dry_gain_value = parameters->dry_gain.load(std::memory_order_relaxed);
		for (int32_t sample = 0; sample < frame_count; ++sample) {
			dst_buffer[sample].left = dry_gain_value * src[sample].left;
			dst_buffer[sample].right = dry_gain_value * src[sample].right;
		}
		return;
	}
	tail_remaining.store(effect_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING, std::memory_order_relaxed);
	const float dry_gain_value = parameters->dry_gain.load(std::memory_order_relaxed);
	const float wet_gain_value = parameters->wet_gain.load(std::memory_order_relaxed);
	mix_wet_output(src, dst_buffer, frame_count, dry_gain_value, wet_gain_value);

	ReflectionDiagnosticDecodeData diagnostics;
	const SignalStatistics decoded_statistics = calculate_stereo_statistics(output_buffer.data[0], output_buffer.data[1], frame_count);
	diagnostics.decoded_peak = decoded_statistics.peak;
	diagnostics.decoded_rms = decoded_statistics.rms;
	diagnostics.decoded_finite = decoded_statistics.finite;
	diagnostics.used_apply = used_apply;
	diagnostics.used_tail = used_tail;
	diagnostics.effect_state = static_cast<int>(effect_state);
	diagnostics.dry_gain = dry_gain_value;
	diagnostics.wet_gain = wet_gain_value;
	for (int32_t sample = 0; sample < frame_count; ++sample) {
		diagnostics.dry_peak = std::max(diagnostics.dry_peak, std::max(std::abs(src[sample].left), std::abs(src[sample].right)));
		diagnostics.wet_peak = std::max(diagnostics.wet_peak, wet_gain_value *
				std::max(std::abs(output_buffer.data[0][sample]), std::abs(output_buffer.data[1][sample])));
		const float final_values[2] = { dst_buffer[sample].left, dst_buffer[sample].right };
		for (float value : final_values) {
			if (!std::isfinite(value)) {
				diagnostics.final_finite = false;
			} else {
				diagnostics.final_peak = std::max(diagnostics.final_peak, std::abs(value));
			}
		}
	}
	capture_samples(output_buffer.data[0], frame_count, diagnostics.decoded_left_samples);
	capture_samples(output_buffer.data[1], frame_count, diagnostics.decoded_right_samples);
	for (int sample = 0; sample < REFLECTION_DIAGNOSTIC_SAMPLE_COUNT; ++sample) {
		const int32_t source_index = std::min<int32_t>((sample * frame_count) / REFLECTION_DIAGNOSTIC_SAMPLE_COUNT, frame_count - 1);
		diagnostics.final_left_samples[sample] = dst_buffer[source_index].left;
		diagnostics.final_right_samples[sample] = dst_buffer[source_index].right;
	}
	receiver->record_decode_diagnostic(diagnostic_block_id, this, diagnostics);
	// Decode consumes the handoff synchronously. Clear it after diagnostics have
	// observed both sides of this exact block; no sample survives into the next.
	if (has_submission) {
		receiver->clear_ambisonics();
	}
}

bool AudioEffectSteamReflectionHRTFInstance::_process_silence() const {
	return (receiver && receiver->has_pending_ambisonics()) ||
			tail_remaining.load(std::memory_order_relaxed);
}

bool AudioEffectSteamReflectionHRTFInstance::is_ready() const { return configured; }

void AudioEffectSteamReflectionHRTFInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_ready"), &AudioEffectSteamReflectionHRTFInstance::is_ready);
}

} // namespace godot
