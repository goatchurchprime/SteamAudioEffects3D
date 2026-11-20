#pragma once

#include <array>
#include <complex>
#include <memory>
#include <vector>

#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// Immutable, main-thread-prepared convolution kernels reconstructed from a
// Steam Audio first-order Ambisonic energy field.
struct SteamEnergyFieldIR {
	static constexpr int CHANNEL_COUNT = 4;
	static constexpr int PARTITION_SIZE = 2048;
	static constexpr int FFT_SIZE = 2 * PARTITION_SIZE;

	int sample_rate = 0;
	int bin_count = 0;
	int sample_count = 0;
	float duration = 0.0f;
	std::array<std::vector<std::vector<std::complex<float>>>, CHANNEL_COUNT> kernels;
};

std::shared_ptr<const SteamEnergyFieldIR> prepare_steam_energy_field_ir(
		const PackedFloat32Array &data, float duration, int sample_rate, String *error_message = nullptr);

// A streaming uniform-partitioned convolver. Its fixed 2048-sample internal
// partition accepts arbitrary Godot callback sizes and adds 42.7 ms at 48 kHz
// to a path that already represents delayed reflections. The larger partition
// keeps multi-second, four-channel fields practical in an unoptimized build.
class SteamEnergyFieldConvolver {
	std::shared_ptr<const SteamEnergyFieldIR> ir;
	std::vector<std::vector<std::complex<float>>> input_history;
	std::array<std::vector<float>, SteamEnergyFieldIR::CHANNEL_COUNT> overlap;
	std::array<std::vector<float>, SteamEnergyFieldIR::CHANNEL_COUNT> output_block;
	std::vector<float> input_block;
	std::vector<std::complex<float>> accumulated_spectrum;
	int input_fill = 0;
	int output_position = SteamEnergyFieldIR::PARTITION_SIZE;
	int history_position = 0;
	int tail_blocks_remaining = 0;
	bool partial_input_nonzero = false;

	void process_block();

public:
	void set_ir(const std::shared_ptr<const SteamEnergyFieldIR> &value);
	const std::shared_ptr<const SteamEnergyFieldIR> &get_ir() const { return ir; }
	void reset();
	void process(const float *input, int frame_count,
			std::array<std::vector<float>, SteamEnergyFieldIR::CHANNEL_COUNT> &output);
	bool has_tail() const;
};

} // namespace godot
