#include "steam_energy_field.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>

#include <phonon.h>

namespace godot {

namespace {

constexpr float PI = 3.14159265358979323846f;

void fft(std::vector<std::complex<float>> &values, bool inverse) {
	const int count = static_cast<int>(values.size());
	for (int i = 1, j = 0; i < count; ++i) {
		int bit = count >> 1;
		for (; j & bit; bit >>= 1) {
			j ^= bit;
		}
		j ^= bit;
		if (i < j) {
			std::swap(values[i], values[j]);
		}
	}
	for (int length = 2; length <= count; length <<= 1) {
		const float angle = (inverse ? 2.0f : -2.0f) * PI / static_cast<float>(length);
		const std::complex<float> step(std::cos(angle), std::sin(angle));
		for (int start = 0; start < count; start += length) {
			std::complex<float> weight(1.0f, 0.0f);
			for (int offset = 0; offset < length / 2; ++offset) {
				const auto even = values[start + offset];
				const auto odd = values[start + offset + length / 2] * weight;
				values[start + offset] = even + odd;
				values[start + offset + length / 2] = even - odd;
				weight *= step;
			}
		}
	}
	if (inverse) {
		const float scale = 1.0f / static_cast<float>(count);
		for (auto &value : values) {
			value *= scale;
		}
	}
}

void set_error(String *destination, const char *message) {
	if (destination) {
		*destination = message;
	}
}

} // namespace

std::shared_ptr<const SteamEnergyFieldIR> prepare_steam_energy_field_ir(
		const PackedFloat32Array &data, float duration, int sample_rate, String *error_message) {
	if (data.is_empty()) {
		return {};
	}
	if (!std::isfinite(duration) || duration < 0.01f || duration > 10.0f || sample_rate <= 0) {
		set_error(error_message, "Energy field duration must be between 0.01 and 10 seconds and the mix rate must be valid.");
		return {};
	}

	IPLContext context = nullptr;
	IPLEnergyField energy_field = nullptr;
	IPLImpulseResponse impulse_response = nullptr;
	IPLReconstructor reconstructor = nullptr;
	auto release_all = [&]() {
		if (reconstructor) {
			iplReconstructorRelease(&reconstructor);
		}
		if (impulse_response) {
			iplImpulseResponseRelease(&impulse_response);
		}
		if (energy_field) {
			iplEnergyFieldRelease(&energy_field);
		}
		if (context) {
			iplContextRelease(&context);
		}
	};

	IPLContextSettings context_settings{};
	context_settings.version = STEAMAUDIO_VERSION;
	if (iplContextCreate(&context_settings, &context) != IPL_STATUS_SUCCESS) {
		set_error(error_message, "Steam Audio could not create a context for energy-field reconstruction.");
		return {};
	}
	IPLEnergyFieldSettings field_settings{};
	field_settings.duration = duration;
	field_settings.order = 1;
	if (iplEnergyFieldCreate(context, &field_settings, &energy_field) != IPL_STATUS_SUCCESS) {
		set_error(error_message, "Steam Audio could not create the first-order energy field.");
		release_all();
		return {};
	}
	const int channels = iplEnergyFieldGetNumChannels(energy_field);
	const int bins = iplEnergyFieldGetNumBins(energy_field);
	const int expected_values = channels * IPL_NUM_BANDS * bins;
	if (channels != SteamEnergyFieldIR::CHANNEL_COUNT || data.size() != expected_values) {
		if (error_message) {
			*error_message = "Energy field requires exactly " + String::num_int64(expected_values) +
					" values: 4 channels x 3 bands x " + String::num_int64(bins) + " 10 ms bins.";
		}
		release_all();
		return {};
	}
	float *field_data = iplEnergyFieldGetData(energy_field);
	for (int index = 0; index < expected_values; ++index) {
		const float value = data[index];
		if (!std::isfinite(value)) {
			set_error(error_message, "Energy field contains a non-finite value.");
			release_all();
			return {};
		}
		field_data[index] = value;
	}

	IPLImpulseResponseSettings response_settings{};
	response_settings.duration = duration;
	response_settings.order = 1;
	response_settings.samplingRate = sample_rate;
	if (iplImpulseResponseCreate(context, &response_settings, &impulse_response) != IPL_STATUS_SUCCESS) {
		set_error(error_message, "Steam Audio could not allocate the reconstructed impulse response.");
		release_all();
		return {};
	}
	IPLReconstructorSettings reconstructor_settings{};
	reconstructor_settings.maxDuration = duration;
	reconstructor_settings.maxOrder = 1;
	reconstructor_settings.samplingRate = sample_rate;
	if (iplReconstructorCreate(context, &reconstructor_settings, &reconstructor) != IPL_STATUS_SUCCESS) {
		set_error(error_message, "Steam Audio could not create its energy-field reconstructor.");
		release_all();
		return {};
	}
	IPLReconstructorInputs inputs{};
	inputs.energyField = energy_field;
	IPLReconstructorSharedInputs shared_inputs{};
	shared_inputs.duration = duration;
	shared_inputs.order = 1;
	IPLReconstructorOutputs outputs{};
	outputs.impulseResponse = impulse_response;
	iplReconstructorReconstruct(reconstructor, 1, &inputs, &shared_inputs, &outputs);

	auto prepared = std::make_shared<SteamEnergyFieldIR>();
	prepared->sample_rate = sample_rate;
	prepared->bin_count = bins;
	prepared->sample_count = iplImpulseResponseGetNumSamples(impulse_response);
	prepared->duration = duration;
	const int partition_count = (prepared->sample_count + SteamEnergyFieldIR::PARTITION_SIZE - 1) /
			SteamEnergyFieldIR::PARTITION_SIZE;
	for (int channel = 0; channel < SteamEnergyFieldIR::CHANNEL_COUNT; ++channel) {
		const float *samples = iplImpulseResponseGetChannel(impulse_response, channel);
		prepared->kernels[channel].resize(partition_count);
		for (int partition = 0; partition < partition_count; ++partition) {
			auto &kernel = prepared->kernels[channel][partition];
			kernel.assign(SteamEnergyFieldIR::FFT_SIZE, {});
			const int first_sample = partition * SteamEnergyFieldIR::PARTITION_SIZE;
			const int copy_count = std::min(SteamEnergyFieldIR::PARTITION_SIZE,
					prepared->sample_count - first_sample);
			for (int sample = 0; sample < copy_count; ++sample) {
				kernel[sample] = samples[first_sample + sample];
			}
			fft(kernel, false);
		}
	}
	release_all();
	return prepared;
}

void SteamEnergyFieldConvolver::set_ir(const std::shared_ptr<const SteamEnergyFieldIR> &value) {
	if (ir == value) {
		return;
	}
	ir = value;
	const int partitions = ir && !ir->kernels[0].empty() ? static_cast<int>(ir->kernels[0].size()) : 0;
	input_history.assign(partitions,
			std::vector<std::complex<float>>(SteamEnergyFieldIR::FFT_SIZE, std::complex<float>{}));
	input_block.assign(SteamEnergyFieldIR::PARTITION_SIZE, 0.0f);
	accumulated_spectrum.assign(SteamEnergyFieldIR::FFT_SIZE, {});
	for (int channel = 0; channel < SteamEnergyFieldIR::CHANNEL_COUNT; ++channel) {
		overlap[channel].assign(SteamEnergyFieldIR::PARTITION_SIZE, 0.0f);
		output_block[channel].assign(SteamEnergyFieldIR::PARTITION_SIZE, 0.0f);
	}
	reset();
}

void SteamEnergyFieldConvolver::reset() {
	for (auto &spectrum : input_history) {
		std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{});
	}
	std::fill(input_block.begin(), input_block.end(), 0.0f);
	for (int channel = 0; channel < SteamEnergyFieldIR::CHANNEL_COUNT; ++channel) {
		std::fill(overlap[channel].begin(), overlap[channel].end(), 0.0f);
		std::fill(output_block[channel].begin(), output_block[channel].end(), 0.0f);
	}
	input_fill = 0;
	output_position = SteamEnergyFieldIR::PARTITION_SIZE;
	history_position = 0;
	tail_blocks_remaining = 0;
	partial_input_nonzero = false;
}

void SteamEnergyFieldConvolver::process_block() {
	if (!ir || input_history.empty()) {
		return;
	}
	auto &spectrum = input_history[history_position];
	std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{});
	for (int sample = 0; sample < SteamEnergyFieldIR::PARTITION_SIZE; ++sample) {
		spectrum[sample] = input_block[sample];
	}
	fft(spectrum, false);
	const int partitions = static_cast<int>(input_history.size());
	for (int channel = 0; channel < SteamEnergyFieldIR::CHANNEL_COUNT; ++channel) {
		std::fill(accumulated_spectrum.begin(), accumulated_spectrum.end(), std::complex<float>{});
		for (int partition = 0; partition < partitions; ++partition) {
			const int history_index = (history_position - partition + partitions) % partitions;
			const auto &input_spectrum = input_history[history_index];
			const auto &kernel = ir->kernels[channel][partition];
			for (int bin = 0; bin < SteamEnergyFieldIR::FFT_SIZE; ++bin) {
				accumulated_spectrum[bin] += input_spectrum[bin] * kernel[bin];
			}
		}
		fft(accumulated_spectrum, true);
		for (int sample = 0; sample < SteamEnergyFieldIR::PARTITION_SIZE; ++sample) {
			output_block[channel][sample] = accumulated_spectrum[sample].real() + overlap[channel][sample];
			overlap[channel][sample] = accumulated_spectrum[sample + SteamEnergyFieldIR::PARTITION_SIZE].real();
		}
	}
	history_position = (history_position + 1) % partitions;
	if (partial_input_nonzero) {
		tail_blocks_remaining = partitions + 1;
	} else if (tail_blocks_remaining > 0) {
		--tail_blocks_remaining;
	}
}

void SteamEnergyFieldConvolver::process(const float *input, int frame_count,
		std::array<std::vector<float>, SteamEnergyFieldIR::CHANNEL_COUNT> &output) {
	for (auto &channel : output) {
		channel.assign(frame_count, 0.0f);
	}
	if (!ir || input_history.empty()) {
		return;
	}
	for (int sample = 0; sample < frame_count; ++sample) {
		if (output_position < SteamEnergyFieldIR::PARTITION_SIZE) {
			for (int channel = 0; channel < SteamEnergyFieldIR::CHANNEL_COUNT; ++channel) {
				output[channel][sample] = output_block[channel][output_position];
			}
			++output_position;
		}
		const float input_sample = input ? input[sample] : 0.0f;
		input_block[input_fill++] = input_sample;
		partial_input_nonzero = partial_input_nonzero || std::abs(input_sample) > 1e-8f;
		if (input_fill == SteamEnergyFieldIR::PARTITION_SIZE) {
			process_block();
			input_fill = 0;
			partial_input_nonzero = false;
			output_position = 0;
		}
	}
}

bool SteamEnergyFieldConvolver::has_tail() const {
	return ir && (partial_input_nonzero || output_position < SteamEnergyFieldIR::PARTITION_SIZE || tail_blocks_remaining > 0);
}

} // namespace godot
