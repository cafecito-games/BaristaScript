#!/usr/bin/env python
# SConstruct
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

import os
import sys
import runpy

sys.path.insert(0, os.path.abspath("scripts"))
from build_config import load_config, api_path, validate_selection, validate_api_file
from build_metadata import create_metadata, write_header as write_build_info_header

from methods import print_error

versions = load_config()


libname = "barista_script"
projectdir = "project"

localEnv = Environment(tools=["default"], PLATFORM="")
localEnv["api_version"] = versions["godot_api"]
localEnv["precision"] = versions["precision"]
localEnv["build_profile"] = "build_profile.json"

# Build profiles can be used to decrease compile times.
# You can either specify "disabled_classes", OR
# explicitly specify "enabled_classes" which disables all other classes.
# Modify the example file as needed and uncomment the line below or
# manually specify the build_profile parameter when running SCons.

customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Add(BoolVariable("barista_tests", "Build isolated native C++ tests", False))
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()
if env["barista_tests"]:
    env["build_profile"] = "build/native-scons/build_profile.json"
    runpy.run_path("scripts/native_test_build.py")["write_profile"](env["build_profile"])

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    sys.exit(1)

# The extension consumes this option; do not forward it as an unknown godot-cpp option.
ARGUMENTS.pop("barista_tests", None)
if env["barista_tests"]:
    # The test-only SceneTree profile must not delete/reuse ordinary generated bindings.
    env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs},
                    variant_dir="build/native-scons/godot-cpp", duplicate=False)
else:
    env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

actual_api = env.get("custom_api_file") or os.path.join(env.get("gdextension_dir") or "godot-cpp/gdextension",
                                                           api_path(versions).name)
validate_api_file(versions, actual_api)
env.Append(CPPPATH=["src/"])

# Share the pinned engine metadata generator with CMake; unchanged content is not rewritten.
runpy.run_path("scripts/generate_global_api.py")["write_header"](
    str(api_path(versions)), "src/gen/bs_global_api.gen.h"
)

# The warning registry's message switch has no `default:` label on purpose, so an unhandled warning
# code must stop the build rather than fall through. Promote the compiler's unhandled-enumerator
# diagnostic to an error for this extension's own sources only; godot-cpp is already built by the
# time this runs. MSVC's C4062 is off by default, so it is enabled as an error rather than promoted.
if env.get("is_msvc", False):
    env.Append(CXXFLAGS=["/we4062"])
else:
    env.Append(CXXFLAGS=["-Werror=switch"])

selection = validate_selection(versions, platform=env["platform"], architecture=env["arch"],
                               target=env["target"], api=env["api_version"], precision=env["precision"])
info_mode = "native" if env["barista_tests"] else "ordinary"
info_directory = "build/build-info/" + ".".join((info_mode, env["platform"], env["target"], env["arch"], env["precision"]))
info_header = info_directory + "/bs_build_info.gen.h"
write_build_info_header(info_header, create_metadata(os.getcwd(), versions, selection, native_tests=bool(env["barista_tests"])))
env.Append(CPPPATH=[info_directory])

sources = Glob("src/*.cpp")
if env["barista_tests"]:
    if env["target"] != "template_debug":
        print_error("barista_tests=yes requires target=template_debug")
        sys.exit(1)
    native_dir = "build/native-scons"
    native_build = runpy.run_path("scripts/native_test_build.py")
    native_header = native_dir + "/gen/native_build_id.h"
    native_build["write_header"](native_header)
    env.Append(CPPDEFINES=["BARISTA_TESTS", "DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS"])
    env.Append(CPPPATH=["tests/native", "thirdparty/doctest", native_dir + "/gen"])
    sources += Glob("tests/native/*.cpp")

if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

# .dev doesn't inhibit compatibility, so we don't need to key it.
# .universal just means "compatible with all relevant arches" so we don't need to key it.
suffix = env['suffix'].replace(".dev", "").replace(".universal", "")

lib_filename = "{}{}{}{}".format(env.subst('$SHLIBPREFIX'), libname, suffix, env.subst('$SHLIBSUFFIX'))

if env["barista_tests"]:
    # Explicit object targets keep test-on/test-off compilations disjoint.
    sources = [env.SharedObject(native_dir + "/obj/" + os.path.splitext(str(source))[0] + env.subst("$SHOBJSUFFIX"), source) for source in sources]

library = env.SharedLibrary(
    (native_dir + "/bin/" + lib_filename) if env["barista_tests"] else "bin/{}/{}".format(env['platform'], lib_filename),
    source=sources,
)

if env["barista_tests"]:
    def record_native(target, source, env):
        native_build["record"](str(source[0]), native_header, str(target[0]))
    artifact = env.Command(native_dir + "/native-artifact.json", library, record_native)
    default_args = [library, artifact]
else:
    copy = env.Install("{}/bin/{}/".format(projectdir, env["platform"]), library)
    default_args = [library, copy]
Default(*default_args)
