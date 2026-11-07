#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <godot_cpp/classes/audio_effect.hpp>
#include <godot_cpp/classes/audio_effect_instance.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <phonon.h>

namespace godot {

class AudioEffectSteamSpatialInstance;

class AudioEffectSteamSpatial : public AudioEffect {
	GDCLASS(AudioEffectSteamSpatial, AudioEffect);

public:
	enum Interpolation {
		INTERPOLATION_NEAREST,
		INTERPOLATION_BILINEAR,
	};

	struct Parameters {
		std::atomic<float> direction_x{ 0.0f };
		std::atomic<float> direction_y{ 0.0f };
		std::atomic<float> direction_z{ -1.0f };
		std::atomic<float> gain{ 1.0f };
		std::atomic<float> spatial_blend{ 1.0f };
		std::atomic<int> interpolation{ INTERPOLATION_BILINEAR };
	};

private:
	std::shared_ptr<Parameters> parameters = std::make_shared<Parameters>();
	Vector3 direction{ 0.0f, 0.0f, -1.0f };
	float gain = 1.0f;
	float spatial_blend = 1.0f;
	Interpolation interpolation = INTERPOLATION_BILINEAR;

protected:
	static void _bind_methods();

public:
	Ref<AudioEffectInstance> _instantiate() override;

	void set_direction(const Vector3 &value);
	Vector3 get_direction() const;
	void set_gain(float value);
	float get_gain() const;
	void set_spatial_blend(float value);
	float get_spatial_blend() const;
	void set_interpolation(Interpolation value);
	Interpolation get_interpolation() const;
	void set_source_parameters(const Vector3 &source_direction, float source_gain, float source_spatial_blend = 1.0f);
};

class AudioEffectSteamSpatialInstance : public AudioEffectInstance {
	GDCLASS(AudioEffectSteamSpatialInstance, AudioEffectInstance);

	static constexpr int FRAME_SIZE = 512;

	std::shared_ptr<AudioEffectSteamSpatial::Parameters> parameters;
	IPLContext context = nullptr;
	IPLHRTF hrtf = nullptr;
	IPLBinauralEffect binaural_effect = nullptr;
	IPLAudioBuffer input_buffer{};
	IPLAudioBuffer output_buffer{};
	std::array<float, FRAME_SIZE> mono{};
	std::array<float *, 1> input_channels{};
	bool ready = false;

	void release_steam_audio();

protected:
	static void _bind_methods();

public:
	~AudioEffectSteamSpatialInstance() override;
	void set_parameters(const std::shared_ptr<AudioEffectSteamSpatial::Parameters> &value);
	void _process(const void *src_buffer, AudioFrame *dst_buffer, int32_t frame_count) override;
	bool _process_silence() const override;
	bool is_ready() const;
};

} // namespace godot

VARIANT_ENUM_CAST(godot::AudioEffectSteamSpatial::Interpolation)
