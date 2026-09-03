# Using the same minimum as the godot-cpp project
cmake_minimum_required(VERSION 3.17)

# Silence unused variable warning when specified from toolchain
if(CMAKE_C_COMPILER)
endif()

set(LIBNAME "barista_script")
set(GODOT_PROJECT_DIR "project" CACHE STRING "The directory of a Godot project folder")
set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Godot API version" FORCE)
set(GODOTCPP_BUILD_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/build_profile.json" CACHE FILEPATH "godot-cpp build profile" FORCE)

# Make sure all the dependencies are satisfied
find_package(Python3 3.4 REQUIRED)
find_program(GIT git REQUIRED)

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

# Now we can specify our own project which will inherit any global cmake properties or variables that have been defined.
project(barista_script
    VERSION 0.1.0
    DESCRIPTION "BaristaScript language support for Godot 4.7"
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
    src/bs_platform.h
    src/bs_platform_seam.cpp
    src/bs_warning.cpp
    src/bs_warning.h
    src/bs_platform_shims.cpp
    src/register_types.cpp
    src/register_types.h
    src/barista_script.cpp
    src/barista_script.h
    src/barista_script_language.cpp
    src/barista_script_language.h
    src/barista_script_resource_loader.cpp
    src/barista_script_resource_loader.h
)

# Fetch a list of the xml files to use for documentation and add to our target
file(GLOB_RECURSE DOC_XML LIST_DIRECTORIES NO CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/doc_classes/*.xml")

# conditionally add doc data to compile output
if(DOC_XML)
    if(GODOTCPP_TARGET MATCHES "editor|template_debug")
        target_doc_sources(${LIBNAME} "${DOC_XML}")
    endif()
endif()

target_link_libraries(${LIBNAME} PRIVATE godot-cpp)

# Require at least C++17 for this target
set_property(TARGET ${LIBNAME} PROPERTY CXX_STANDARD 17)

set_target_properties(${LIBNAME}
    PROPERTIES
    # The generator expression here prevents msvc from adding a Debug or Release subdir.
    RUNTIME_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"

    OUTPUT_NAME "${LIBNAME}${BARISTA_SCRIPT_SUFFIX}"
)

set(GODOT_PROJECT_BINARY_DIR "${PROJECT_SOURCE_DIR}/${GODOT_PROJECT_DIR}/bin/${GODOTCPP_PLATFORM}")

add_custom_command(TARGET ${LIBNAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${LIBNAME}>" "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>"
)
