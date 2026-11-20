#include <phonon.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct RoomPreset {
	std::string name;
	IPLVector3 size;
	IPLVector3 source;
	IPLVector3 listener;
	float duration;
	std::array<IPLMaterial, 6> materials;
};

IPLMaterial material(float low, float mid, float high, float scattering) {
	return IPLMaterial{ { low, mid, high }, scattering, { 0.0f, 0.0f, 0.0f } };
}

RoomPreset make_preset(const std::string &name) {
	const auto plaster = material(0.12f, 0.06f, 0.04f, 0.05f);
	const auto carpet = material(0.24f, 0.69f, 0.73f, 0.05f);
	const auto concrete = material(0.05f, 0.07f, 0.08f, 0.05f);
	const auto wood = material(0.11f, 0.07f, 0.06f, 0.05f);
	const auto absorber = material(1.0f, 1.0f, 1.0f, 0.0f);
	const auto mirror = material(0.01f, 0.01f, 0.01f, 0.02f);

	if (name == "absorbent_wall") {
		RoomPreset result{ name, { 8.0f, 3.2f, 10.0f }, { 1.5f, 1.5f, 1.5f },
			{ -3.2f, 1.5f, 0.0f }, 1.2f, {} };
		result.materials.fill(plaster);
		result.materials[0] = carpet; // -X wall, close to the listener.
		result.materials[4] = carpet; // floor.
		return result;
	}
	if (name == "small_shoebox") {
		RoomPreset result{ name, { 4.0f, 2.6f, 5.0f }, { -1.0f, 1.3f, -1.2f },
			{ 1.1f, 1.3f, 1.3f }, 1.0f, {} };
		result.materials.fill(plaster);
		result.materials[4] = wood;
		return result;
	}
	if (name == "far_wall_echo") {
		RoomPreset result{ name, { 24.0f, 8.0f, 55.0f }, { -1.0f, 1.7f, -20.0f },
			{ 1.0f, 1.7f, -20.0f }, 1.5f, {} };
		result.materials.fill(absorber);
		result.materials[3] = mirror; // Distant +Z wall: about a 275 ms round trip.
		return result;
	}
	if (name == "big_auditorium") {
		RoomPreset result{ name, { 30.0f, 12.0f, 45.0f }, { 0.0f, 1.5f, -16.0f },
			{ 5.0f, 1.7f, 8.0f }, 4.0f, {} };
		result.materials.fill(concrete);
		result.materials[4] = wood; // floor.
		result.materials[5] = plaster; // ceiling.
		return result;
	}
	throw std::runtime_error("unknown preset: " + name);
}

void append_face(std::vector<IPLVector3> &vertices, std::vector<IPLTriangle> &triangles,
		std::vector<IPLint32> &material_indices, IPLVector3 a, IPLVector3 b,
		IPLVector3 c, IPLVector3 d, int material_index) {
	const auto base = static_cast<IPLint32>(vertices.size());
	vertices.insert(vertices.end(), { a, b, c, d });
	triangles.push_back({ { base, base + 1, base + 2 } });
	triangles.push_back({ { base, base + 2, base + 3 } });
	material_indices.push_back(material_index);
	material_indices.push_back(material_index);
}

void make_inward_room_mesh(const IPLVector3 &size, std::vector<IPLVector3> &vertices,
		std::vector<IPLTriangle> &triangles, std::vector<IPLint32> &material_indices) {
	const float x = size.x * 0.5f;
	const float y = size.y;
	const float z = size.z * 0.5f;
	// Winding points every surface normal into the room.
	append_face(vertices, triangles, material_indices,
			{ -x, 0, -z }, { -x, y, -z }, { -x, y, z }, { -x, 0, z }, 0); // -X, +X normal
	append_face(vertices, triangles, material_indices,
			{ x, 0, -z }, { x, 0, z }, { x, y, z }, { x, y, -z }, 1); // +X, -X normal
	append_face(vertices, triangles, material_indices,
			{ -x, 0, -z }, { x, 0, -z }, { x, y, -z }, { -x, y, -z }, 2); // -Z, +Z normal
	append_face(vertices, triangles, material_indices,
			{ -x, 0, z }, { -x, y, z }, { x, y, z }, { x, 0, z }, 3); // +Z, -Z normal
	append_face(vertices, triangles, material_indices,
			{ -x, 0, -z }, { -x, 0, z }, { x, 0, z }, { x, 0, -z }, 4); // floor, +Y normal
	append_face(vertices, triangles, material_indices,
			{ -x, y, -z }, { x, y, -z }, { x, y, z }, { -x, y, z }, 5); // ceiling, -Y normal
}

void check(IPLerror result, const char *operation) {
	if (result != IPL_STATUS_SUCCESS) {
		throw std::runtime_error(std::string(operation) + " failed with Steam Audio status " +
				std::to_string(static_cast<int>(result)));
	}
}

void IPLCALL progress(float value, void *) {
	const int percent = static_cast<int>(std::round(value * 100.0f));
	std::cerr << "\rBaking: " << std::setw(3) << percent << "%" << std::flush;
}

void write_json(const std::string &path, const RoomPreset &preset, int rays, int diffuse_samples,
		int bounces, IPLEnergyField energy_field, const float *reverb_times) {
	const int channels = iplEnergyFieldGetNumChannels(energy_field);
	const int bins = iplEnergyFieldGetNumBins(energy_field);
	const int value_count = channels * IPL_NUM_BANDS * bins;
	const float *values = iplEnergyFieldGetData(energy_field);
	std::ofstream output(path);
	if (!output) {
		throw std::runtime_error("could not open output: " + path);
	}
	output << std::setprecision(9);
	output << "{\n";
	output << "  \"format\": \"steam_audio_energy_field_v1\",\n";
	output << "  \"preset\": \"" << preset.name << "\",\n";
	output << "  \"duration\": " << preset.duration << ",\n";
	output << "  \"order\": 1,\n";
	output << "  \"channels\": " << channels << ",\n";
	output << "  \"bands\": " << IPL_NUM_BANDS << ",\n";
	output << "  \"bin_duration\": 0.01,\n";
	output << "  \"bins\": " << bins << ",\n";
	output << "  \"layout\": \"channel_band_time\",\n";
	output << "  \"source\": [" << preset.source.x << ", " << preset.source.y << ", " << preset.source.z << "],\n";
	output << "  \"listener\": [" << preset.listener.x << ", " << preset.listener.y << ", " << preset.listener.z << "],\n";
	output << "  \"room_size\": [" << preset.size.x << ", " << preset.size.y << ", " << preset.size.z << "],\n";
	output << "  \"bake\": {\"rays\": " << rays << ", \"diffuse_samples\": " << diffuse_samples
			<< ", \"bounces\": " << bounces << "},\n";
	output << "  \"reverb_times\": [" << reverb_times[0] << ", " << reverb_times[1] << ", " << reverb_times[2] << "],\n";
	output << "  \"energy_field\": [\n    ";
	for (int i = 0; i < value_count; ++i) {
		if (i > 0) {
			output << ",";
			if (i % 8 == 0) {
				output << "\n    ";
			} else {
				output << " ";
			}
		}
		output << values[i];
	}
	output << "\n  ]\n}\n";
}

void bake(const RoomPreset &preset, const std::string &output_path, bool quick) {
	IPLContext context = nullptr;
	IPLScene scene = nullptr;
	IPLStaticMesh mesh = nullptr;
	IPLProbeBatch probes = nullptr;
	IPLEnergyField energy_field = nullptr;
	auto cleanup = [&]() {
		if (energy_field) iplEnergyFieldRelease(&energy_field);
		if (probes) iplProbeBatchRelease(&probes);
		if (mesh) iplStaticMeshRelease(&mesh);
		if (scene) iplSceneRelease(&scene);
		if (context) iplContextRelease(&context);
	};

	try {
		IPLContextSettings context_settings{};
		context_settings.version = STEAMAUDIO_VERSION;
		check(iplContextCreate(&context_settings, &context), "iplContextCreate");

		IPLSceneSettings scene_settings{};
		scene_settings.type = IPL_SCENETYPE_DEFAULT;
		check(iplSceneCreate(context, &scene_settings, &scene), "iplSceneCreate");

		std::vector<IPLVector3> vertices;
		std::vector<IPLTriangle> triangles;
		std::vector<IPLint32> material_indices;
		make_inward_room_mesh(preset.size, vertices, triangles, material_indices);
		IPLStaticMeshSettings mesh_settings{};
		mesh_settings.numVertices = static_cast<IPLint32>(vertices.size());
		mesh_settings.numTriangles = static_cast<IPLint32>(triangles.size());
		mesh_settings.numMaterials = static_cast<IPLint32>(preset.materials.size());
		mesh_settings.vertices = vertices.data();
		mesh_settings.triangles = triangles.data();
		mesh_settings.materialIndices = material_indices.data();
		mesh_settings.materials = const_cast<IPLMaterial *>(preset.materials.data());
		check(iplStaticMeshCreate(scene, &mesh_settings, &mesh), "iplStaticMeshCreate");
		iplStaticMeshAdd(mesh, scene);
		iplSceneCommit(scene);

		check(iplProbeBatchCreate(context, &probes), "iplProbeBatchCreate");
		iplProbeBatchAddProbe(probes, IPLSphere{ preset.listener, 0.5f });
		iplProbeBatchCommit(probes);

		IPLBakedDataIdentifier identifier{};
		identifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
		identifier.variation = IPL_BAKEDDATAVARIATION_STATICSOURCE;
		identifier.endpointInfluence = IPLSphere{ preset.source, 1000.0f };

		const int rays = quick ? 4096 : 32768;
		const int diffuse_samples = quick ? 32 : 1024;
		const int bounces = quick ? 16 : 64;
		IPLReflectionsBakeParams params{};
		params.scene = scene;
		params.probeBatch = probes;
		params.sceneType = IPL_SCENETYPE_DEFAULT;
		params.identifier = identifier;
		params.bakeFlags = static_cast<IPLReflectionsBakeFlags>(
				IPL_REFLECTIONSBAKEFLAGS_BAKECONVOLUTION | IPL_REFLECTIONSBAKEFLAGS_BAKEPARAMETRIC);
		params.numRays = rays;
		params.numDiffuseSamples = diffuse_samples;
		params.numBounces = bounces;
		params.simulatedDuration = preset.duration;
		params.savedDuration = preset.duration;
		params.order = 1;
		params.numThreads = quick ? 2 : 8;
		params.rayBatchSize = 1;
		params.irradianceMinDistance = 1.0f;
		params.bakeBatchSize = 1;
		iplReflectionsBakerBake(context, &params, progress, nullptr);
		std::cerr << "\n";

		IPLEnergyFieldSettings field_settings{};
		field_settings.duration = preset.duration;
		field_settings.order = 1;
		check(iplEnergyFieldCreate(context, &field_settings, &energy_field), "iplEnergyFieldCreate");
		iplProbeBatchGetEnergyField(probes, &identifier, 0, energy_field);
		float reverb_times[IPL_NUM_BANDS]{};
		iplProbeBatchGetReverb(probes, &identifier, 0, reverb_times);
		write_json(output_path, preset, rays, diffuse_samples, bounces, energy_field, reverb_times);
		std::cout << preset.name << ": RT60 = [" << reverb_times[0] << ", " << reverb_times[1]
				<< ", " << reverb_times[2] << "] s -> " << output_path << "\n";
	} catch (...) {
		cleanup();
		throw;
	}
	cleanup();
}

} // namespace

int main(int argc, char **argv) {
	if (argc < 3 || argc > 4) {
		std::cerr << "Usage: " << argv[0]
				<< " <absorbent_wall|small_shoebox|far_wall_echo|big_auditorium> <output.json> [--quick]\n";
		return 2;
	}
	try {
		bake(make_preset(argv[1]), argv[2], argc == 4 && std::string(argv[3]) == "--quick");
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "error: " << error.what() << "\n";
		return 1;
	}
}
