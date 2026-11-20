#!/usr/bin/env python
import os
import sys

from SCons.Script import Default, Dir, Environment, Glob, SConscript

godot_cpp = Dir("godot-cpp")
if not godot_cpp.exists() or not os.listdir(str(godot_cpp.srcnode())):
    print("godot-cpp missing. Run: git submodule update --init --recursive", file=sys.stderr)
    sys.exit(1)

steam_audio_root = os.environ.get("STEAM_AUDIO_ROOT", "build/steam-audio/install")
steam_include = os.path.join(steam_audio_root, "include")
steam_lib = os.path.join(steam_audio_root, "lib")
if not os.path.exists(os.path.join(steam_include, "phonon.h")):
    print("Steam Audio missing. Run: scripts/build_linux.sh", file=sys.stderr)
    sys.exit(1)

env = SConscript("godot-cpp/SConstruct")
if os.environ.get("CROSS_CC"):
    env.Replace(
        CC=os.environ["CROSS_CC"],
        CXX=os.environ["CROSS_CXX"],
        AR=os.environ["CROSS_AR"],
    )
env.Append(CPPPATH=["src", steam_include])
env.Append(LIBPATH=[steam_lib])
env.Append(LIBS=["phonon"])
if env.get("platform") == "linux":
    env.Append(LINKFLAGS=["-Wl,-rpath,$ORIGIN"])
if env.get("is_msvc", False):
    env.Append(CXXFLAGS=["/W4", "/wd4100"])
else:
    env.Append(CXXFLAGS=["-Wall", "-Wextra", "-Wno-unused-parameter"])

addon_bin = os.environ.get("STEAM_SPATIAL_OUTPUT_DIR", "addons/steam_spatial/bin")
os.makedirs(addon_bin, exist_ok=True)
sources = Glob("src/*.cpp")
if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData(
            "src/gen/doc_data.gen.cpp",
            source=Glob("doc_classes/*.xml"),
        )
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference: godot-cpp predates GDExtension documentation support.")
library = env.SharedLibrary(
    f"{addon_bin}/libsteam_spatial{env['suffix']}{env['SHLIBSUFFIX']}",
    source=sources,
)
Default(library)
