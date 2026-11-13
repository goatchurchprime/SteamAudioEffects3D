#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <godot_cpp/classes/audio_effect.hpp>
#include <godot_cpp/classes/audio_effect_instance.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <phonon.h>

namespace godot {

class AudioEffectSteamReflectionHRTF;

class AudioEffectSteamReflectionSendInstance;

class AudioEffectSteamReflectionSend : public AudioEffect {
	GDCLASS(AudioEffectSteamReflectionSend, AudioEffect);

public:
	struct Parameters {
		std::atomic<bool> enabled{ false };
		std::atomic<float> send_gain{ 0.25f };
		std::atomic<float> reverb_time_low{ 1.0f };
		std::atomic<float> reverb_time_mid{ 0.8f };
		std::atomic<float> reverb_time_high{ 0.5f };
	};

private:
	std::shared_ptr<Parameters> parameters = std::make_shared<Parameters>();
	Ref<AudioEffectSteamReflectionHRTF> target;
	bool enabled = false;
	float send_gain = 0.25f;
	Vector3 reverb_times{ 1.0f, 0.8f, 0.5f };

protected:
	static void _bind_methods();

public:
	Ref<AudioEffectInstance> _instantiate() override;

	void set_enabled(bool value);
	bool is_enabled() const;
	void set_send_gain(float value);
	float get_send_gain() const;
	void set_reverb_times(const Vector3 &value);
	Vector3 get_reverb_times() const;
	void set_target(const Ref<AudioEffectSteamReflectionHRTF> &value);
	Ref<AudioEffectSteamReflectionHRTF> get_target() const;
};

class AudioEffectSteamReflectionSendInstance : public AudioEffectInstance {
	GDCLASS(AudioEffectSteamReflectionSendInstance, AudioEffectInstance);

	static constexpr int CHANNEL_COUNT = 4;

	std::shared_ptr<AudioEffectSteamReflectionSend::Parameters> parameters;
	Ref<AudioEffectSteamReflectionHRTF> target;
	IPLContext context = nullptr;
	IPLReflectionEffect reflection_effect = nullptr;
	IPLAudioBuffer input_buffer{};
	IPLAudioBuffer reverb_buffer{};
	std::vector<float> mono;
	std::array<float *, 1> input_channels{};
	std::array<std::vector<float>, CHANNEL_COUNT> ambisonics;
	std::array<float *, CHANNEL_COUNT> ambisonic_channels{};
	std::atomic<bool> tail_remaining{ false };
	int32_t sampling_rate = 0;
	int32_t processing_frame_size = 0;
	bool was_enabled = false;
	bool configured = false;

	void release_steam_audio();
	bool ensure_processing_size(int32_t frame_count);
	void submit_reverb_frame(float gain);

protected:
	static void _bind_methods();

public:
	~AudioEffectSteamReflectionSendInstance() override;
	void set_configuration(const std::shared_ptr<AudioEffectSteamReflectionSend::Parameters> &parameter_values,
			const Ref<AudioEffectSteamReflectionHRTF> &target_value);
	void _process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) override;
	bool _process_silence() const override;
	bool is_ready() const;
};

class AudioEffectSteamReflectionHRTFInstance;

class AudioEffectSteamReflectionHRTF : public AudioEffect {
	GDCLASS(AudioEffectSteamReflectionHRTF, AudioEffect);

public:
	struct Parameters {
		std::atomic<float> right_x{ 1.0f };
		std::atomic<float> right_y{ 0.0f };
		std::atomic<float> right_z{ 0.0f };
		std::atomic<float> up_x{ 0.0f };
		std::atomic<float> up_y{ 1.0f };
		std::atomic<float> up_z{ 0.0f };
		std::atomic<float> ahead_x{ 0.0f };
		std::atomic<float> ahead_y{ 0.0f };
		std::atomic<float> ahead_z{ -1.0f };
		std::atomic<float> wet_gain{ 1.0f };
	};

private:
	static constexpr int CHANNEL_COUNT = 4;

	std::shared_ptr<Parameters> parameters = std::make_shared<Parameters>();
	// mix_in() and consume_ambisonics() are called serially by Godot's audio thread.
	std::array<std::vector<float>, CHANNEL_COUNT> mixed_ambisonics;
	std::array<float *, CHANNEL_COUNT> mixed_ambisonic_channels{};
	bool ambisonics_pending = false;
	int32_t mixed_frame_count = 0;
	Basis listener_orientation;
	float wet_gain = 1.0f;

protected:
	static void _bind_methods();

public:
	Ref<AudioEffectInstance> _instantiate() override;

	void set_listener_orientation(const Basis &value);
	Basis get_listener_orientation() const;
	void set_wet_gain(float value);
	float get_wet_gain() const;
	void set_listener_parameters(const Basis &orientation, float gain = 1.0f);
	void mix_in(const IPLAudioBuffer *buffer, float gain = 1.0f);
	bool consume_ambisonics(IPLAudioBuffer *buffer, int32_t frame_count);
	bool has_pending_ambisonics() const;
};

class AudioEffectSteamReflectionHRTFInstance : public AudioEffectInstance {
	GDCLASS(AudioEffectSteamReflectionHRTFInstance, AudioEffectInstance);

	std::shared_ptr<AudioEffectSteamReflectionHRTF::Parameters> parameters;
	AudioEffectSteamReflectionHRTF *receiver = nullptr;
	IPLContext context = nullptr;
	IPLHRTF hrtf = nullptr;
	IPLAmbisonicsDecodeEffect decode_effect = nullptr;
	IPLAudioBuffer input_buffer{};
	IPLAudioBuffer output_buffer{};
	std::atomic<bool> tail_remaining{ false };
	int32_t sampling_rate = 0;
	int32_t processing_frame_size = 0;
	bool configured = false;

	void release_steam_audio();
	bool ensure_processing_size(int32_t frame_count);
	void mix_wet_output(const AudioFrame *dry, AudioFrame *destination, int32_t frame_count, float gain);

protected:
	static void _bind_methods();

public:
	~AudioEffectSteamReflectionHRTFInstance() override;
	void set_configuration(const std::shared_ptr<AudioEffectSteamReflectionHRTF::Parameters> &parameter_values,
			AudioEffectSteamReflectionHRTF *receiver_value);
	void _process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) override;
	bool _process_silence() const override;
	bool is_ready() const;
};

} // namespace godot
