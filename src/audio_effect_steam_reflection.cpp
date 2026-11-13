#include "audio_effect_steam_reflection.hpp"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/print_string.hpp>

namespace godot {

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
	parameters->reverb_time_low.store(reverb_times.x, std::memory_order_relaxed);
	parameters->reverb_time_mid.store(reverb_times.y, std::memory_order_relaxed);
	parameters->reverb_time_high.store(reverb_times.z, std::memory_order_relaxed);
}

Vector3 AudioEffectSteamReflectionSend::get_reverb_times() const { return reverb_times; }

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
	ClassDB::bind_method(D_METHOD("set_target", "target"), &AudioEffectSteamReflectionSend::set_target);
	ClassDB::bind_method(D_METHOD("get_target"), &AudioEffectSteamReflectionSend::get_target);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "send_gain", PROPERTY_HINT_RANGE, "0,4,0.001,or_greater"), "set_send_gain", "get_send_gain");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "reverb_times", PROPERTY_HINT_RANGE, "0.1,20,0.01,suffix:s"), "set_reverb_times", "get_reverb_times");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "AudioEffectSteamReflectionHRTF"), "set_target", "get_target");
}

AudioEffectSteamReflectionSendInstance::~AudioEffectSteamReflectionSendInstance() { release_steam_audio(); }

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

void AudioEffectSteamReflectionSendInstance::submit_reverb_frame(float gain) {
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
	target->mix_in(&ambisonic_buffer, gain);
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
			tail_remaining.store(false, std::memory_order_relaxed);
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

	IPLAudioEffectState effect_state = IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
	if (has_input) {
		IPLReflectionEffectParams effect_parameters{};
		effect_parameters.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
		effect_parameters.reverbTimes[0] = parameters->reverb_time_low.load(std::memory_order_relaxed);
		effect_parameters.reverbTimes[1] = parameters->reverb_time_mid.load(std::memory_order_relaxed);
		effect_parameters.reverbTimes[2] = parameters->reverb_time_high.load(std::memory_order_relaxed);
		effect_parameters.numChannels = 1;
		effect_state = iplReflectionEffectApply(reflection_effect, &effect_parameters, &input_buffer, &reverb_buffer, nullptr);
	} else if (tail_remaining.load(std::memory_order_relaxed)) {
		effect_state = iplReflectionEffectGetTail(reflection_effect, &reverb_buffer, nullptr);
	} else {
		return;
	}
	tail_remaining.store(effect_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING, std::memory_order_relaxed);

	submit_reverb_frame(parameters->send_gain.load(std::memory_order_relaxed));
}

bool AudioEffectSteamReflectionSendInstance::_process_silence() const {
	return tail_remaining.load(std::memory_order_relaxed);
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

void AudioEffectSteamReflectionHRTF::set_wet_gain(float value) {
	wet_gain = std::max(value, 0.0f);
	parameters->wet_gain.store(wet_gain, std::memory_order_relaxed);
}

float AudioEffectSteamReflectionHRTF::get_wet_gain() const { return wet_gain; }

void AudioEffectSteamReflectionHRTF::set_listener_parameters(const Basis &orientation, float gain) {
	set_listener_orientation(orientation);
	set_wet_gain(gain);
}

void AudioEffectSteamReflectionHRTF::mix_in(const IPLAudioBuffer *buffer, float gain) {
	if (!buffer || !buffer->data || buffer->numChannels < CHANNEL_COUNT || buffer->numSamples <= 0 || gain <= 0.0f) {
		return;
	}

	if (!ambisonics_pending || mixed_frame_count != buffer->numSamples) {
		mixed_frame_count = buffer->numSamples;
		for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
			mixed_ambisonics[channel].assign(mixed_frame_count, 0.0f);
			mixed_ambisonic_channels[channel] = mixed_ambisonics[channel].data();
		}
	}

	for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
		for (int sample = 0; sample < mixed_frame_count; ++sample) {
			mixed_ambisonics[channel][sample] += gain * buffer->data[channel][sample];
		}
	}
	ambisonics_pending = true;
}

bool AudioEffectSteamReflectionHRTF::consume_ambisonics(IPLAudioBuffer *buffer, int32_t frame_count) {
	if (!buffer || !ambisonics_pending || mixed_frame_count != frame_count) {
		return false;
	}
	buffer->numChannels = CHANNEL_COUNT;
	buffer->numSamples = mixed_frame_count;
	buffer->data = mixed_ambisonic_channels.data();
	ambisonics_pending = false;
	return true;
}

bool AudioEffectSteamReflectionHRTF::has_pending_ambisonics() const { return ambisonics_pending; }

void AudioEffectSteamReflectionHRTF::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_listener_orientation", "orientation"), &AudioEffectSteamReflectionHRTF::set_listener_orientation);
	ClassDB::bind_method(D_METHOD("get_listener_orientation"), &AudioEffectSteamReflectionHRTF::get_listener_orientation);
	ClassDB::bind_method(D_METHOD("set_wet_gain", "gain"), &AudioEffectSteamReflectionHRTF::set_wet_gain);
	ClassDB::bind_method(D_METHOD("get_wet_gain"), &AudioEffectSteamReflectionHRTF::get_wet_gain);
	ClassDB::bind_method(D_METHOD("set_listener_parameters", "orientation", "wet_gain"), &AudioEffectSteamReflectionHRTF::set_listener_parameters, DEFVAL(1.0f));

	ADD_PROPERTY(PropertyInfo(Variant::BASIS, "listener_orientation"), "set_listener_orientation", "get_listener_orientation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wet_gain", PROPERTY_HINT_RANGE, "0,4,0.001,or_greater"), "set_wet_gain", "get_wet_gain");
}

AudioEffectSteamReflectionHRTFInstance::~AudioEffectSteamReflectionHRTFInstance() { release_steam_audio(); }

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
		float gain) {
	for (int32_t sample = 0; sample < frame_count; ++sample) {
		destination[sample].left = dry[sample].left + gain * output_buffer.data[0][sample];
		destination[sample].right = dry[sample].right + gain * output_buffer.data[1][sample];
	}
}

void AudioEffectSteamReflectionHRTFInstance::_process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) {
	const auto *src = static_cast<const AudioFrame *>(src_buffer);
	if (!parameters || !receiver || !ensure_processing_size(frame_count)) {
		std::copy_n(src, frame_count, dst_buffer);
		return;
	}

	const bool has_submission = receiver->consume_ambisonics(&input_buffer, frame_count);
	IPLAudioEffectState effect_state = IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
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
	} else if (tail_remaining.load(std::memory_order_relaxed)) {
		effect_state = iplAmbisonicsDecodeEffectGetTail(decode_effect, &output_buffer);
	} else {
		std::copy_n(src, frame_count, dst_buffer);
		return;
	}
	tail_remaining.store(effect_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING, std::memory_order_relaxed);
	mix_wet_output(src, dst_buffer, frame_count, parameters->wet_gain.load(std::memory_order_relaxed));
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
