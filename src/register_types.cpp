#include "audio_effect_steam_direct.hpp"
#include "audio_effect_steam_spatial.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static void initialize_steam_spatial(ModuleInitializationLevel level) {
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	ClassDB::register_class<AudioEffectSteamDirect>();
	ClassDB::register_class<AudioEffectSteamDirectInstance>();
	ClassDB::register_class<AudioEffectSteamSpatial>();
	ClassDB::register_class<AudioEffectSteamSpatialInstance>();
}

static void uninitialize_steam_spatial(ModuleInitializationLevel level) {}

extern "C" GDExtensionBool GDE_EXPORT steam_spatial_library_init(
		GDExtensionInterfaceGetProcAddress get_proc_address,
		GDExtensionClassLibraryPtr library,
		GDExtensionInitialization *initialization) {
	GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
	init.register_initializer(initialize_steam_spatial);
	init.register_terminator(uninitialize_steam_spatial);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
