# Using the same minimum as the godot-cpp project
cmake_minimum_required(VERSION 3.17)

# Silence unused variable warning when specified from toolchain
if(CMAKE_C_COMPILER)
endif()

set(LIBNAME "barista_script")
option(BARISTA_TESTS "Build isolated native C++ tests" OFF)
set(GODOT_PROJECT_DIR "project" CACHE STRING "The directory of a Godot project folder")
set(GODOTCPP_BUILD_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/build_profile.json" CACHE FILEPATH "godot-cpp build profile" FORCE)

# Make sure all the dependencies are satisfied
find_package(Python3 3.4 REQUIRED)
find_program(GIT git REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/build_versions.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_config.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_config.py" --format cmake
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/barista-versions.cmake" RESULT_VARIABLE BARISTA_CONFIG_RESULT)
if(NOT BARISTA_CONFIG_RESULT EQUAL 0)
    message(FATAL_ERROR "Invalid shared BaristaScript build configuration")
endif()
include("${CMAKE_CURRENT_BINARY_DIR}/barista-versions.cmake")
foreach(SETTING API_VERSION PRECISION)
    if(SETTING STREQUAL "API_VERSION")
        set(EXPECTED_SETTING "${BARISTA_GODOT_API}")
    else()
        set(EXPECTED_SETTING "${BARISTA_PRECISION}")
    endif()
    if(DEFINED GODOTCPP_${SETTING} AND NOT "${GODOTCPP_${SETTING}}" STREQUAL "${EXPECTED_SETTING}")
        message(FATAL_ERROR "GODOTCPP_${SETTING}: expected ${EXPECTED_SETTING}, actual ${GODOTCPP_${SETTING}}")
    endif()
    set(GODOTCPP_${SETTING} "${EXPECTED_SETTING}" CACHE STRING "Shared BaristaScript ${SETTING}")
endforeach()
string(REPLACE "." "-" BARISTA_API_SUFFIX "${BARISTA_GODOT_API}")
set(BARISTA_API_FILE "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/gdextension/extension_api-${BARISTA_API_SUFFIX}.json")
if(BARISTA_TESTS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/build_profile.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/native_test_build.py")
    set(GODOTCPP_BUILD_PROFILE "${CMAKE_CURRENT_BINARY_DIR}/native-build-profile.json" CACHE FILEPATH "godot-cpp test build profile" FORCE)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/native_test_build.py"
        --profile "${GODOTCPP_BUILD_PROFILE}" RESULT_VARIABLE NATIVE_PROFILE_RESULT)
    if(NOT NATIVE_PROFILE_RESULT EQUAL 0)
        message(FATAL_ERROR "Cannot generate native test bindings profile")
    endif()
endif()

# Ensure godot-cpp submodule has been updated
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/godot-cpp/src")
    message(NOTICE "godot-cpp bindings source not found")
    message(NOTICE "initializing/updating the godot-cpp submodule...")

    # update the c++ bindings submodule to populate it with the necessary source for the library
    execute_process(
        COMMAND git submodule update --init godot-cpp
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
add_subdirectory(godot-cpp SYSTEM)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_global_api.py"
    "${BARISTA_API_FILE}")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_global_api.py"
        "${BARISTA_API_FILE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gen/bs_global_api.gen.h"
    RESULT_VARIABLE BARISTA_GLOBAL_API_RESULT)
if(NOT BARISTA_GLOBAL_API_RESULT EQUAL 0)
    message(FATAL_ERROR "Cannot generate pinned BaristaScript global API metadata")
endif()

# When compiling for emscripten, this toolchainhack needs to be included.
# set(CMAKE_PROJECT_<project-name>_INCLUDE ${godot-cpp_SOURCE_DIR}/cmake/emsdkHack.cmake)

# Create the gen directory at configure time so IDEs can see generated headers immediately after generate_bindings
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/godot-cpp/gen/include")

# Add godot-cpp's module path and include the exported functions.
# This is made available for documentation generation
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${godot-cpp_SOURCE_DIR}/cmake")
include(GodotCPPModule)

# The godot-cpp target has some of useful properties attached that can be retrieved like so.
get_target_property(GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)
get_target_property(GODOTCPP_PLATFORM godot::cpp GODOTCPP_PLATFORM)
get_target_property(GODOTCPP_ARCH godot::cpp GODOTCPP_ARCH)
get_target_property(BARISTA_ACTUAL_TARGET godot::cpp GODOTCPP_TARGET)
get_target_property(BARISTA_ACTUAL_PRECISION godot::cpp GODOTCPP_PRECISION)
if(GODOTCPP_CUSTOM_API_FILE)
    set(BARISTA_ACTUAL_API "${GODOTCPP_CUSTOM_API_FILE}")
else()
    set(BARISTA_ACTUAL_API "${GODOTCPP_GDEXTENSION_DIR}/extension_api-${BARISTA_API_SUFFIX}.json")
endif()
get_filename_component(BARISTA_ACTUAL_API "${BARISTA_ACTUAL_API}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp")
# godot-cpp trims this input at configure time. Watch the original selected file,
# including external overrides, so incremental builds cannot reuse a stale trim.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BARISTA_ACTUAL_API}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_config.py"
    --expect-api-file "${BARISTA_ACTUAL_API}" RESULT_VARIABLE BARISTA_API_CHECK OUTPUT_QUIET)
if(NOT BARISTA_API_CHECK EQUAL 0)
    message(FATAL_ERROR "Actual bindings API differs from shared configuration")
endif()

# Now we can specify our own project which will inherit any global cmake properties or variables that have been defined.
project(barista_script
    VERSION ${BARISTA_EXTENSION_VERSION}
    DESCRIPTION "BaristaScript language support for Godot ${BARISTA_GODOT_API}"
    HOMEPAGE_URL "https://github.com/cafecito-games/BaristaScript"
    LANGUAGES CXX
)

# The GDExtension descriptor uses Godot's standard architecture-agnostic macOS
# library name. CMake's godot-cpp suffix includes the build architecture, unlike
# the template's SCons build, so normalize it before naming our library.
set(BARISTA_SCRIPT_SUFFIX "${GODOTCPP_SUFFIX}")
string(REPLACE ".dev" "" BARISTA_SCRIPT_SUFFIX "${BARISTA_SCRIPT_SUFFIX}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    string(REPLACE "$<1:.${GODOTCPP_ARCH}>" "" BARISTA_SCRIPT_SUFFIX "${BARISTA_SCRIPT_SUFFIX}")
endif()

add_library(${LIBNAME} SHARED)

target_sources(${LIBNAME}
    PRIVATE
    src/bs_build_info.cpp
    src/bs_build_info.h
    src/bs_platform.h
    src/bs_platform_seam.cpp
    src/bs_warning.cpp
    src/bs_warning.h
    src/bs_platform_shims.cpp
    src/bs_tokenizer.cpp
    src/bs_tokenizer.h
    src/bs_tokenizer_buffer.cpp
    src/bs_tokenizer_buffer.h
    src/register_types.cpp
    src/register_types.h
    src/barista_script.cpp
    src/barista_script.h
    src/barista_script_language.cpp
    src/barista_script_language.h
    src/barista_script_resource_loader.cpp
    src/barista_script_resource_loader.h
    src/bs_cache.cpp
    src/bs_cache.h
    src/bs_conformance_registry.cpp
    src/bs_conformance_registry.h
    src/bs_analyzer.cpp
    src/bs_analyzer.h
    src/bs_analyzer_call_validation.cpp
	src/bs_utility_functions.cpp
    src/bs_analyzer_flow_finality.cpp
    src/bs_analyzer_surface.cpp
	src/bs_analyzer_finalization.cpp
	src/bs_analyzer_conformance.cpp
	src/bs_trait_utils.cpp
	src/bs_trait_utils.h
	src/bs_analyzer_probe.cpp
	src/bs_analyzer_probe.h
    src/bs_type.cpp
    src/bs_type.h
    src/bs_type_info.h
    src/bs_builtin_sources.cpp
    src/bs_builtin_sources.h
    src/bs_function.h
    src/bs_script_server.h
    src/bs_core_constants.h
    src/bs_native_db.h
    src/bs_declaration_index.cpp
    src/bs_declaration_index.h
    src/bs_declaration_index_probe.cpp
    src/bs_declaration_index_probe.h
    src/barista_script_parse_cache.cpp
    src/barista_script_parse_cache.h
    src/bs_diagnostic_names.cpp
    src/bs_diagnostic_names.h
    src/bs_parser.cpp
    src/bs_parser.h
    src/bs_parser_data_type.cpp
    src/bs_parser_probe.cpp
    src/bs_parser_probe.h
    src/bs_corpus_sentinels.cpp
    src/bs_corpus_sentinels.h
    src/bs_global_class.cpp
    src/bs_global_class.h
)

# Fetch a list of the xml files to use for documentation and add to our target
file(GLOB_RECURSE DOC_XML LIST_DIRECTORIES NO CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/doc_classes/*.xml")

# conditionally add doc data to compile output
if(DOC_XML)
    if(GODOTCPP_TARGET MATCHES "editor|template_debug")
        target_doc_sources(${LIBNAME} "${DOC_XML}")
    endif()
endif()

if(BARISTA_TESTS)
    if(NOT GODOTCPP_TARGET STREQUAL "template_debug")
        message(FATAL_ERROR "BARISTA_TESTS requires GODOTCPP_TARGET=template_debug")
    endif()
    set(NATIVE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/native-gen/native_build_id.h")
    file(GLOB NATIVE_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/native/*.cpp")
    # Refresh before compilation on every build: the shared identity also covers
    # non-C++ inputs and the current submodule HEAD. The helper only writes changes.
    add_custom_target(native_build_identity
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/native_test_build.py"
            --header "${NATIVE_HEADER}"
        BYPRODUCTS "${NATIVE_HEADER}"
        VERBATIM)
    add_dependencies(${LIBNAME} native_build_identity)
    target_sources(${LIBNAME} PRIVATE ${NATIVE_SOURCES})
    target_include_directories(${LIBNAME} PRIVATE src tests/native thirdparty/doctest "${CMAKE_CURRENT_BINARY_DIR}/native-gen")
    target_compile_definitions(${LIBNAME} PRIVATE BARISTA_TESTS DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
endif()

# GODOTCPP_TARGET is fixed for this build tree. Keep BYPRODUCTS free of generator
# expressions so header generation also works at the declared CMake 3.17 minimum.
set(BARISTA_INFO_DIR "${CMAKE_CURRENT_BINARY_DIR}/build-info")
set(BARISTA_INFO_NATIVE)
if(BARISTA_TESTS)
    set(BARISTA_INFO_NATIVE --native-tests)
endif()
add_custom_target(barista_build_identity
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build_metadata.py"
        --header "${BARISTA_INFO_DIR}/bs_build_info.gen.h"
        --platform "${GODOTCPP_PLATFORM}" --architecture "${GODOTCPP_ARCH}"
        --target "${BARISTA_ACTUAL_TARGET}" --api "${GODOTCPP_API_VERSION}" --precision "${BARISTA_ACTUAL_PRECISION}"
        ${BARISTA_INFO_NATIVE}
    BYPRODUCTS "${BARISTA_INFO_DIR}/bs_build_info.gen.h"
    VERBATIM)
add_dependencies(${LIBNAME} barista_build_identity)
target_include_directories(${LIBNAME} PRIVATE "${BARISTA_INFO_DIR}")

target_link_libraries(${LIBNAME} PRIVATE godot-cpp)

# See the SConstruct note: an unhandled warning code in bs_warning.cpp's message switch is a build
# error, not a diagnostic that scrolls past.
target_compile_options(${LIBNAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/we4062>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror=switch>
)

# Require at least C++17 for this target
set_property(TARGET ${LIBNAME} PROPERTY CXX_STANDARD 17)

if(BARISTA_TESTS)
    set(BARISTA_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/native-bin")
else()
    set(BARISTA_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}")
endif()
set_target_properties(${LIBNAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${BARISTA_OUTPUT_DIR}>"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${BARISTA_OUTPUT_DIR}>"
    OUTPUT_NAME "${LIBNAME}${BARISTA_SCRIPT_SUFFIX}")

if(BARISTA_TESTS)
    add_custom_target(native_artifact ALL
        DEPENDS ${LIBNAME}
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/native_test_build.py"
            --header "${NATIVE_HEADER}" --library "$<TARGET_FILE:${LIBNAME}>"
            --artifact "${CMAKE_CURRENT_BINARY_DIR}/native-artifact.json")
else()
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/native-artifact.json")
    set(GODOT_PROJECT_BINARY_DIR "${PROJECT_SOURCE_DIR}/${GODOT_PROJECT_DIR}/bin/${GODOTCPP_PLATFORM}")
    add_custom_command(TARGET ${LIBNAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GODOT_PROJECT_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${LIBNAME}>" "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>")
endif()
