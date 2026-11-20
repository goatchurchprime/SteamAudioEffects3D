#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <godot_cpp/classes/audio_effect.hpp>
#include <godot_cpp/classes/audio_effect_instance.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <phonon.h>

#include "steam_energy_field.hpp"

namespace godot {

class AudioEffectSteamReflectionHRTF;

class AudioEffectSteamReflectionSendInstance;

static constexpr int REFLECTION_DIAGNOSTIC_SAMPLE_COUNT = 16;

struct ReflectionDiagnosticSendData {
	float source_peak = 0.0f;
	float source_rms = 0.0f;
	float reflection_peak = 0.0f;
	float reflection_rms = 0.0f;
	bool source_finite = true;
	bool reflection_finite = true;
	bool enabled = false;
	bool had_input = false;
	bool used_apply = false;
	bool used_tail = false;
	int effect_state = 0;
	float send_gain = 0.0f;
	std::array<float, 3> reverb_times{};
	std::array<float, 3> previous_reverb_times{};
	std::array<float, 3> reverb_time_deltas{};
	bool reverb_parameters_changed = false;
	std::array<float, REFLECTION_DIAGNOSTIC_SAMPLE_COUNT> source_samples{};
	std::array<float, REFLECTION_DIAGNOSTIC_SAMPLE_COUNT> reflection_samples{};
};

struct ReflectionDiagnosticDecodeData {
	float decoded_peak = 0.0f;
	float decoded_rms = 0.0f;
	float dry_peak = 0.0f;
	float wet_peak = 0.0f;
	float final_peak = 0.0f;
	bool decoded_finite = true;
	bool final_finite = true;
	bool used_apply = false;
	bool used_tail = false;
	int effect_state = 0;
	float dry_gain = 0.0f;
	float wet_gain = 0.0f;
	std::array<float, REFLECTION_DIAGNOSTIC_SAMPLE_COUNT> decoded_left_samples{};
	std::array<float, REFLECTION_DIAGNOSTIC_SAMPLE_COUNT> decoded_right_samples{};
	std::array<float, REFLECTION_DIAGNOSTIC_SAMPLE_COUNT> final_left_samples{};
	std::array<float, REFLECTION_DIAGNOSTIC_SAMPLE_COUNT> final_right_samples{};
};

class AudioEffectSteamReflectionSend : public AudioEffect {
	GDCLASS(AudioEffectSteamReflectionSend, AudioEffect);

public:
	struct Parameters {
		std::atomic<bool> enabled{ false };
		std::atomic<float> send_gain{ 0.25f };
		std::atomic<float> reverb_time_low{ 1.0f };
		std::atomic<float> reverb_time_mid{ 0.8f };
		std::atomic<float> reverb_time_high{ 0.5f };
		std::atomic<uint32_t> reverb_revision{ 0 };
		std::shared_ptr<const SteamEnergyFieldIR> energy_field_ir;
	};

private:
	std::shared_ptr<Parameters> parameters = std::make_shared<Parameters>();
	Ref<AudioEffectSteamReflectionHRTF> target;
	bool enabled = false;
	float send_gain = 0.25f;
	Vector3 reverb_times{ 1.0f, 0.8f, 0.5f };
	float energy_field_duration = 1.0f;
	PackedFloat32Array energy_field;

	void rebuild_energy_field();

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
	void set_energy_field_duration(float value);
	float get_energy_field_duration() const;
	void set_energy_field(const PackedFloat32Array &value);
	PackedFloat32Array get_energy_field() const;
	int get_energy_field_bin_count() const;
	void clear_energy_field();
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
	SteamEnergyFieldConvolver energy_field_convolver;
	std::atomic<bool> tail_remaining{ false };
	int32_t sampling_rate = 0;
	int32_t processing_frame_size = 0;
	bool was_enabled = false;
	bool have_previous_reverb_parameters = false;
	std::array<float, 3> previous_reverb_times{};
	bool configured = false;

	void release_steam_audio();
	bool ensure_processing_size(int32_t frame_count);
	void submit_reverb_frame(float gain, const ReflectionDiagnosticSendData &diagnostics);
	std::array<float, 3> load_reverb_times_coherently();
	bool process_energy_field(int32_t frame_count, ReflectionDiagnosticSendData &diagnostics);

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
		std::atomic<float> dry_gain{ 1.0f };
		std::atomic<float> wet_gain{ 1.0f };
	};

	enum DiagnosticEvent : uint32_t {
		EVENT_NONE = 0,
		EVENT_SEND_CREATE = 1u << 0,
		EVENT_SEND_RELEASE = 1u << 1,
		EVENT_SEND_REBUILD = 1u << 2,
		EVENT_SEND_RESET = 1u << 3,
		EVENT_RETURN_CREATE = 1u << 4,
		EVENT_RETURN_RELEASE = 1u << 5,
		EVENT_RETURN_REBUILD = 1u << 6,
		EVENT_STALE_CHUNK_REPLACED = 1u << 7,
		EVENT_AMBISONICS_CONSUMED = 1u << 8,
		EVENT_AMBISONICS_CLEARED = 1u << 9,
		EVENT_BUS_ATTACHED = 1u << 10,
		EVENT_PLAY = 1u << 11,
		EVENT_STOP = 1u << 12,
		EVENT_RETURN_TAIL = 1u << 13,
	};

private:
	static constexpr int CHANNEL_COUNT = 4;
	static constexpr int MAX_PENDING_SENDERS = 64;
	static constexpr int DIAGNOSTIC_BLOCK_COUNT = 256;
	static constexpr int DIAGNOSTIC_POST_TRIGGER_BLOCKS = 32;

	struct DiagnosticBlock {
		uint64_t block_id = 0;
		uint64_t sender_identity = 0;
		uint64_t return_instance_identity = 0;
		uint64_t source_identity = 0;
		uint32_t events = 0;
		int32_t frame_count = 0;
		int32_t sender_count = 0;
		std::array<uint64_t, MAX_PENDING_SENDERS> sender_identities{};
		bool began_new_chunk = false;
		bool repeated_sender = false;
		bool triggered = false;
		std::array<float, CHANNEL_COUNT> ambisonic_peaks{};
		bool ambisonics_finite = true;
		ReflectionDiagnosticSendData send;
		ReflectionDiagnosticDecodeData decode;
	};

	std::shared_ptr<Parameters> parameters = std::make_shared<Parameters>();
	// One fixed-size chunk passed serially from Reflection Send to Reflection HRTF
	// on Godot's audio thread. It is zeroed for the first pending submission,
	// copy-added for any other sends in that chunk, and then consumed. No queue.
	std::array<std::vector<float>, CHANNEL_COUNT> mixed_ambisonics;
	std::array<float *, CHANNEL_COUNT> mixed_ambisonic_channels{};
	std::array<const void *, MAX_PENDING_SENDERS> pending_senders{};
	int pending_sender_count = 0;
	bool ambisonics_pending = false;
	int32_t mixed_frame_count = 0;
	std::array<DiagnosticBlock, DIAGNOSTIC_BLOCK_COUNT> diagnostic_blocks{};
	uint64_t diagnostic_next_block_id = 1;
	uint64_t current_diagnostic_block_id = 0;
	std::atomic<uint32_t> pending_diagnostic_events{ 0 };
	std::atomic<uint64_t> diagnostic_source_identity{ 0 };
	std::atomic<float> diagnostic_trigger_peak{ 2.0f };
	std::atomic<bool> diagnostic_enabled{ true };
	std::atomic<bool> diagnostic_triggered{ false };
	std::atomic<bool> diagnostic_frozen{ false };
	std::atomic<uint64_t> diagnostic_trigger_block_id{ 0 };
	Basis listener_orientation;
	float dry_gain = 1.0f;
	float wet_gain = 1.0f;

	DiagnosticBlock *begin_diagnostic_block(int32_t frame_count, uint64_t sender_identity, bool began_new_chunk, bool repeated_sender);
	DiagnosticBlock *find_diagnostic_block(uint64_t block_id);
	void evaluate_diagnostic_trigger(DiagnosticBlock &block);
	static Dictionary diagnostic_block_to_dictionary(const DiagnosticBlock &block);

protected:
	static void _bind_methods();

public:
	Ref<AudioEffectInstance> _instantiate() override;

	void set_listener_orientation(const Basis &value);
	Basis get_listener_orientation() const;
	void set_dry_gain(float value);
	float get_dry_gain() const;
	void set_wet_gain(float value);
	float get_wet_gain() const;
	void set_listener_parameters(const Basis &orientation, float gain = 1.0f);
	void mix_in(const void *sender, const IPLAudioBuffer *buffer, float gain, const ReflectionDiagnosticSendData &diagnostics);
	bool consume_ambisonics(IPLAudioBuffer *buffer, int32_t frame_count, uint64_t *block_id);
	void clear_ambisonics();
	bool has_pending_ambisonics() const;
	uint64_t begin_return_tail_diagnostic(int32_t frame_count, const void *return_instance);
	void record_decode_diagnostic(uint64_t block_id, const void *return_instance, const ReflectionDiagnosticDecodeData &diagnostics);
	void diagnostic_event(uint32_t event);
	void set_diagnostic_source_identity(uint64_t identity);
	uint64_t get_diagnostic_source_identity() const;
	void set_diagnostic_trigger_peak(float peak);
	float get_diagnostic_trigger_peak() const;
	void set_diagnostic_enabled(bool enabled);
	bool is_diagnostic_enabled() const;
	bool is_diagnostic_capture_ready() const;
	Dictionary get_diagnostic_report() const;
	void reset_diagnostic_capture();
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
	void mix_wet_output(const AudioFrame *dry, AudioFrame *destination, int32_t frame_count, float dry_gain, float wet_gain);

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
