#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <godot_cpp/classes/audio_effect.hpp>
#include <godot_cpp/classes/audio_effect_instance.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <phonon.h>

namespace godot {

class AudioEffectSteamDirectInstance;

class AudioEffectSteamDirect : public AudioEffect {
	GDCLASS(AudioEffectSteamDirect, AudioEffect);

public:
	enum TransmissionType {
		TRANSMISSION_FREQUENCY_INDEPENDENT,
		TRANSMISSION_FREQUENCY_DEPENDENT,
	};

	struct Parameters {
		std::atomic<bool> distance_attenuation_enabled{ true };
		std::atomic<float> distance_attenuation{ 1.0f };
		std::atomic<bool> air_absorption_enabled{ true };
		std::atomic<float> air_absorption_low{ 1.0f };
		std::atomic<float> air_absorption_mid{ 1.0f };
		std::atomic<float> air_absorption_high{ 1.0f };
		std::atomic<bool> directivity_enabled{ true };
		std::atomic<float> directivity{ 1.0f };
		std::atomic<bool> occlusion_enabled{ false };
		std::atomic<float> occlusion{ 1.0f };
		std::atomic<bool> transmission_enabled{ false };
		std::atomic<int> transmission_type{ TRANSMISSION_FREQUENCY_DEPENDENT };
		std::atomic<float> transmission_low{ 1.0f };
		std::atomic<float> transmission_mid{ 1.0f };
		std::atomic<float> transmission_high{ 1.0f };
		std::atomic<bool> diagnostic_enabled{ true };
		std::atomic<float> diagnostic_trigger_peak{ 2.0f };
		std::atomic<bool> diagnostic_triggered{ false };
		std::atomic<float> diagnostic_input_peak{ 0.0f };
		std::atomic<float> diagnostic_output_peak{ 0.0f };
		std::atomic<bool> diagnostic_input_finite{ true };
		std::atomic<bool> diagnostic_output_finite{ true };
	};

private:
	std::shared_ptr<Parameters> parameters = std::make_shared<Parameters>();
	bool distance_attenuation_enabled = true;
	float distance_attenuation = 1.0f;
	bool air_absorption_enabled = true;
	Vector3 air_absorption{ 1.0f, 1.0f, 1.0f };
	bool directivity_enabled = true;
	float directivity = 1.0f;
	bool occlusion_enabled = false;
	float occlusion = 1.0f;
	bool transmission_enabled = false;
	TransmissionType transmission_type = TRANSMISSION_FREQUENCY_DEPENDENT;
	Vector3 transmission{ 1.0f, 1.0f, 1.0f };

protected:
	static void _bind_methods();

public:
	Ref<AudioEffectInstance> _instantiate() override;

	void set_distance_attenuation_enabled(bool value);
	bool is_distance_attenuation_enabled() const;
	void set_distance_attenuation(float value);
	float get_distance_attenuation() const;
	void set_air_absorption_enabled(bool value);
	bool is_air_absorption_enabled() const;
	void set_air_absorption(const Vector3 &value);
	Vector3 get_air_absorption() const;
	void set_directivity_enabled(bool value);
	bool is_directivity_enabled() const;
	void set_directivity(float value);
	float get_directivity() const;
	void set_occlusion_enabled(bool value);
	bool is_occlusion_enabled() const;
	void set_occlusion(float value);
	float get_occlusion() const;
	void set_transmission_enabled(bool value);
	bool is_transmission_enabled() const;
	void set_transmission_type(TransmissionType value);
	TransmissionType get_transmission_type() const;
	void set_transmission(const Vector3 &value);
	Vector3 get_transmission() const;
	void set_diagnostic_trigger_peak(float value);
	float get_diagnostic_trigger_peak() const;
	bool is_diagnostic_capture_ready() const;
	Dictionary get_diagnostic_report() const;
	void reset_diagnostic_capture();
};

class AudioEffectSteamDirectInstance : public AudioEffectInstance {
	GDCLASS(AudioEffectSteamDirectInstance, AudioEffectInstance);

	std::shared_ptr<AudioEffectSteamDirect::Parameters> parameters;
	IPLContext context = nullptr;
	IPLDirectEffect direct_effect = nullptr;
	IPLAudioBuffer input_buffer{};
	IPLAudioBuffer output_buffer{};
	std::vector<float> input_left;
	std::vector<float> input_right;
	std::array<float *, 2> input_channels{};
	int32_t sampling_rate = 0;
	int32_t processing_frame_size = 0;
	bool configured = false;

	void release_steam_audio();
	bool ensure_processing_size(int32_t frame_count);

protected:
	static void _bind_methods();

public:
	~AudioEffectSteamDirectInstance() override;
	void set_parameters(const std::shared_ptr<AudioEffectSteamDirect::Parameters> &value);
	void _process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) override;
	bool _process_silence() const override;
	bool is_ready() const;
};

} // namespace godot

VARIANT_ENUM_CAST(godot::AudioEffectSteamDirect::TransmissionType)
